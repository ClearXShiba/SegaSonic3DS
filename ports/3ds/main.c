/**
 *    Name:        main.c
 *    Description: the game on the Nintendo 3DS: libctru shows its frames on the top screen, ndsp plays its
 *                 sound, the pad and buttons are the controls, around the same game and board as the other ports
 *    Version:     0.1.0
 *    Created:     04-11-2026
 *    Modified:    04-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    Usage: copy segasonic.3dsx to sdmc:/3ds/ and run it from the Homebrew Launcher.
 *
 *    The executable carries nothing of the game's. Its files are in sdmc:/3ds/segasonic/:
 *      segasonic.assets   the assets pack. If it is not there the first run makes it from the ROM set,
 *                         sonic.zip (MAME's 'sonic', Japan rev. C) or a folder named rom/ with its files
 *      sonic.eeprom       the game's settings and bookkeeping (written by the game)
 *      sdmc:/3ds/dspfirm.cdc is needed for sound (dsp1 dumps it)
 *
 *    This follows ports/sdl2/main.c: where the board has its hooks the port does its part. When VBLANK
 *    starts the video draws the frame and the port puts it on the screen; when the board reads the controls
 *    the port hands it the pad; when the game finishes a frame the port takes the frame's samples, mixes
 *    them to 32 kHz and queues them, and waits for the frame's time (60 a second).
 *
 *    The picture is the board's 320 x 224 (or 416 x 224) one, at 1:1 on the top screen's 400 x 240, in
 *    RGB565. The video library draws it pixel by pixel in software, which is the part the 3DS may not
 *    keep up with: when a frame comes late the next one is not drawn (the game itself goes on at 60).
 *
 *    Controls (player 1):
 *      circle pad          the trackball (a speed: the ball rolls as far as the pad is pushed)
 *      D-pad               the trackball, at a steady speed
 *      A, B, X or Y        the button
 *      SELECT              coin 1             START   start 1
 *      L                   service            R       test (in the operator's menu L moves the cursor
 *                                                       and R picks)
 *    Hold START and SELECT together to leave (HOME works as well).
 */
#include "s32/machine.h"
#include "s32/program.h"
#include "s32/video.h"
#include "game/data.h"
#include "game/extract.h"
#include "sound/chips.h"

#include <3ds.h>

#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define PLAYERS            3
#define OUTPUT_RATE        32000
#define AUDIO_BUFFERS      6
#define AUDIO_BUFFER_SAMPLES 1024         /* stereo samples in each buffer, a frame is about 533 */
#define STACK_SIZE         (1024 * 1024)  /* the game's stack: the main thread's is small */
#define SCREEN_WIDTH       400
#define SCREEN_HEIGHT      240
#define MAX_SKIPPED        2              /* frames in a row that are not drawn */

#define FOLDER             "sdmc:/3ds/segasonic/"
#define ASSETS_FILE        FOLDER "segasonic.assets"
#define EEPROM_FILE        FOLDER "sonic.eeprom"
#define ROM_ZIP            FOLDER "sonic.zip"
#define ROM_FOLDER         FOLDER "rom"
#define LOG_FILE           FOLDER "log.txt"    /* what the port was doing, line by line: read it after a crash */

/* The sdl2 port's settings, here fixed */
#define PAD_ROLL           30.0           /* trackball counts a frame at full push */
#define DPAD_ROLL          30
#define PAD_DEADZONE       12             /* of the circle pad's +-156 */
#define VOLUME             1.0

static const double FM_RATE = S32_SOUND_CLOCK / 144.0;
static const double PCM_RATE = S32_PCM_CLOCK / 384.0;

static jmp_buf finished;
static bool audio_on;
static ndspWaveBuf wave_buffers[AUDIO_BUFFERS];
static int16_t *audio_memory;             /* linear memory the DSP reads */
static uint64_t next_frame;               /* system tick the next frame waits for */
static int skip_next;                     /* frames to leave undrawn */
static int skipped_in_row;
static int clear_screens;                 /* screens still to black out after a change of width */
static int shown_width;
static uint32_t drawn_frames;
static uint64_t counted_from;

#define FRAME_TICKS        (SYSCLOCK_ARM11 / S32_FRAME_RATE)

/* What the controls say, gathered between frames */
static struct
{
	bool button;
	bool coin;
	bool start;
	bool service;
	bool test;
	int roll_x;
	int roll_y;
} controls;

/* A chip's samples on their way to the output rate: what is left of the last frame and where the next
   output sample falls between two of them */
typedef struct resampler
{
	double step;            /* source samples an output sample */
	double position;        /* of the next output sample, from the first sample kept */
	int32_t *samples;       /* left and right */
	size_t count;           /* samples (pairs) kept */
	size_t capacity;
} resampler;

static resampler chips[3];

/* ---- the log ---- */

/* A line of what the port is doing, in log.txt: opened and closed every time, so a crash cannot lose it */
static void trace(const char *format, ...)
{
	static int lines;
	FILE *log;
	va_list arguments;

	if (lines++ > 500)
		return;
	log = fopen(LOG_FILE, lines == 1 ? "w" : "a");
	if (!log)
		return;
	va_start(arguments, format);
	vfprintf(log, format, arguments);
	va_end(arguments);
	fputc('\n', log);
	fclose(log);
}

/* ---- files ---- */

static uint8_t *load_file(const char *path, size_t *size)
{
	FILE *input = fopen(path, "rb");
	uint8_t *data;
	long length;

	if (!input)
		return NULL;
	fseek(input, 0, SEEK_END);
	length = ftell(input);
	fseek(input, 0, SEEK_SET);
	data = length > 0 ? (uint8_t *)malloc((size_t)length) : NULL;
	if (data && fread(data, 1, (size_t)length, input) != (size_t)length)
	{
		free(data);
		data = NULL;
	}
	fclose(input);
	if (size)
		*size = data ? (size_t)length : 0;
	return data;
}

static void save_eeprom(void)
{
	FILE *output;

	if (!s32_board.eeprom.dirty)
		return;
	output = fopen(EEPROM_FILE, "wb");
	if (!output)
		return;
	fwrite(s32_board.eeprom.data, 1, sizeof(s32_board.eeprom.data), output);
	fclose(output);
	s32_board.eeprom.dirty = false;
}

static void load_eeprom(void)
{
	size_t size;
	uint8_t *data = load_file(EEPROM_FILE, &size);

	if (data && size == sizeof(s32_board.eeprom.data))
		memcpy(s32_board.eeprom.data, data, size);
	free(data);
}

static bool exists(const char *path)
{
	struct stat info;

	return stat(path, &info) == 0;
}

/* ---- the picture ---- */

/* The board's frame on the top screen. The 3DS's framebuffer is turned a quarter: a column of 240 pixels
   a time, the bottom of the screen first. The board's pixels are 0x00RRGGBB with five bits a channel in
   the top bits of each byte. */
static void put_frame(const uint32_t *pixels, int width)
{
	u16 rows, columns;
	u16 *screen = (u16 *)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &rows, &columns);
	const int shown = width > SCREEN_WIDTH ? SCREEN_WIDTH : width;
	const int first_source = (width - shown) / 2;
	const int first_column = (SCREEN_WIDTH - shown) / 2;
	const int top = (SCREEN_HEIGHT - S32_VIDEO_HEIGHT) / 2;

	if (width != shown_width)
	{
		shown_width = width;
		clear_screens = 2;    /* (both buffers) */
	}
	if (clear_screens > 0)
	{
		memset(screen, 0, (size_t)rows * columns * sizeof(u16));
		clear_screens--;
	}
	for (int x = 0; x < shown; x++)
	{
		u16 *column = screen + (size_t)(first_column + x) * rows + (rows - 1 - top);
		const uint32_t *source = pixels + first_source + x;

		for (int y = 0; y < S32_VIDEO_HEIGHT; y++, source += S32_VIDEO_MAX_WIDTH)
		{
			const uint32_t pixel = *source;

			column[-y] = (u16)(((pixel >> 8) & 0xF800u) | ((pixel >> 5) & 0x07C0u) | ((pixel >> 10) & 0x0020u)
				| ((pixel >> 3) & 0x001Fu));
		}
	}
}

static void on_frame_shown(void)
{
	static int calls;
	int width;

	if (calls < 3)
		trace("frame_shown #%d: start", calls + 1);
	if (skip_next > 0)
	{
		skip_next--;
		skipped_in_row++;
		return;
	}
	skipped_in_row = 0;
	s32_video_draw_frame();
	if (calls < 3)
		trace("frame_shown #%d: drawn, width %d", calls + 1, s32_video_frame(&width) ? width : -1);
	put_frame(s32_video_frame(&width), width);
	if (calls < 3)
		trace("frame_shown #%d: put on screen", calls + 1);
	gfxFlushBuffers();
	gfxSwapBuffers();
	drawn_frames++;
	if (calls < 3)
		trace("frame_shown #%d: swapped", calls + 1);
	calls++;
}

/* ---- the controls ---- */

static int pad_axis(int value)
{
	if (value > -PAD_DEADZONE && value < PAD_DEADZONE)
		return 0;
	return value;
}

/* the state of every control once a frame, where the board reads them */
static void on_controls_update(void)
{
	static int calls;
	s32_inputs *input = &s32_board.input;
	circlePosition pad;
	u32 held;
	int roll_x, roll_y;

	if (calls < 3)
		trace("controls_update #%d", calls + 1);
	calls++;
	hidScanInput();
	held = hidKeysHeld();
	hidCircleRead(&pad);

	if ((held & KEY_START) && (held & KEY_SELECT))
		longjmp(finished, 1);

	/* the board's X counts the other way, and the pad's Y is up */
	roll_x = -(int)lround(pad_axis(pad.dx) / 156.0 * PAD_ROLL);
	roll_y = -(int)lround(pad_axis(pad.dy) / 156.0 * PAD_ROLL);
	roll_x += ((held & KEY_DLEFT) ? 1 : 0) * DPAD_ROLL - ((held & KEY_DRIGHT) ? 1 : 0) * DPAD_ROLL;
	roll_y += ((held & KEY_DDOWN) ? 1 : 0) * DPAD_ROLL - ((held & KEY_DUP) ? 1 : 0) * DPAD_ROLL;

	controls.button = (held & (KEY_A | KEY_B | KEY_X | KEY_Y)) != 0;
	controls.coin = (held & KEY_SELECT) != 0;
	controls.start = (held & KEY_START) != 0;
	controls.service = (held & KEY_L) != 0;
	controls.test = (held & KEY_R) != 0;

	/* the board's ports are active low: a cleared bit is a pressed button */
	input->port_a = (uint8_t)(0xff & ~(controls.button ? 0x01 : 0));
	input->port_b = 0xff;
	input->port_e = (uint8_t)(0xff
		& ~(controls.service ? 0x01 : 0) & ~(controls.test ? 0x02 : 0)
		& ~(controls.coin ? 0x04 : 0) & ~(controls.start ? 0x10 : 0));
	input->trackball_x[0] = (uint16_t)(input->trackball_x[0] + roll_x);
	input->trackball_y[0] = (uint16_t)(input->trackball_y[0] + roll_y);
}

/* ---- the sound ---- */

static void keep(resampler *chip, const int32_t *samples, size_t count)
{
	if (chip->count + count > chip->capacity)
	{
		chip->capacity = (chip->count + count) * 2;
		chip->samples = (int32_t *)realloc(chip->samples, chip->capacity * 2 * sizeof(int32_t));
	}
	memcpy(chip->samples + chip->count * 2, samples, count * 2 * sizeof(int32_t));
	chip->count += count;
}

/* the chip's output at the next output sample, between the two samples around it */
static void sample_at(const resampler *chip, double position, double out[2])
{
	const size_t index = (size_t)position;
	const double fraction = position - (double)index;

	for (int side = 0; side < 2; side++)
	{
		const double here = chip->samples[index * 2 + side];
		const double next = chip->samples[(index + 1) * 2 + side];
		out[side] = here + (next - here) * fraction;
	}
}

static void drop_used(resampler *chip)
{
	const size_t used = (size_t)chip->position;

	if (!used)
		return;
	memmove(chip->samples, chip->samples + used * 2, (chip->count - used) * 2 * sizeof(int32_t));
	chip->count -= used;
	chip->position -= (double)used;
}

/* the frame's samples of the three chips, mixed as MAME's routes do (each YM3438 at 0.30, the RF5C68
   at 0.40) at the output rate */
static void play_frame(void)
{
	static int16_t mix[AUDIO_BUFFER_SAMPLES * 2];
	size_t made = 0;

	sound_chips_end_frame(s32_board.cycles);
	for (int chip = 0; chip < 3; chip++)
	{
		size_t count;
		const int32_t *samples = sound_chips_frame_samples(chip, &count);
		keep(&chips[chip], samples, count);
	}
	while (made < AUDIO_BUFFER_SAMPLES)
	{
		double fm0[2], fm1[2], pcm[2];

		if (chips[0].position + 1 >= (double)chips[0].count || chips[1].position + 1 >= (double)chips[1].count
			|| chips[2].position + 1 >= (double)chips[2].count)
			break;
		sample_at(&chips[0], chips[0].position, fm0);
		sample_at(&chips[1], chips[1].position, fm1);
		sample_at(&chips[2], chips[2].position, pcm);
		for (int side = 0; side < 2; side++)
		{
			double value = VOLUME * (0.30 * (fm0[side] + fm1[side]) + 0.40 * pcm[side]);
			mix[made * 2 + side] = (int16_t)(value > 32767 ? 32767 : (value < -32768 ? -32768 : value));
		}
		for (int chip = 0; chip < 3; chip++)
			chips[chip].position += chips[chip].step;
		made++;
	}
	for (int chip = 0; chip < 3; chip++)
		drop_used(&chips[chip]);
	if (!audio_on || made == 0)
		return;
	for (int index = 0; index < AUDIO_BUFFERS; index++)
	{
		ndspWaveBuf *buffer = &wave_buffers[index];
		int16_t *memory = audio_memory + (size_t)index * AUDIO_BUFFER_SAMPLES * 2;

		if (buffer->status != NDSP_WBUF_FREE && buffer->status != NDSP_WBUF_DONE)
			continue;
		memcpy(memory, mix, made * 2 * sizeof(int16_t));
		DSP_FlushDataCache(memory, made * 2 * sizeof(int16_t));
		buffer->data_vaddr = memory;
		buffer->nsamples = (u32)made;
		buffer->looping = false;
		ndspChnWaveBufAdd(0, buffer);
		return;
	}
	/* every buffer is still queued: the frame's sound is dropped */
}

static bool start_audio(void)
{
	if (R_FAILED(ndspInit()))
		return false;
	audio_memory = (int16_t *)linearAlloc((size_t)AUDIO_BUFFERS * AUDIO_BUFFER_SAMPLES * 2 * sizeof(int16_t));
	if (!audio_memory)
	{
		ndspExit();
		return false;
	}
	ndspSetOutputMode(NDSP_OUTPUT_STEREO);
	ndspChnReset(0);
	ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
	ndspChnSetRate(0, (float)OUTPUT_RATE);
	ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
	memset(wave_buffers, 0, sizeof(wave_buffers));
	return true;
}

/* ---- the frame ---- */

static void show_status(void)
{
	const uint64_t now = svcGetSystemTick();
	const double seconds = (double)(now - counted_from) / SYSCLOCK_ARM11;

	if (seconds > 0.0)
		printf("\x1b[5;1H%2.0f frames drawn a second (of 60)  ", drawn_frames / seconds);
	drawn_frames = 0;
	counted_from = now;
}

static void wait_for_frame(void)
{
	uint64_t now = svcGetSystemTick();

	next_frame += FRAME_TICKS;
	if (now > next_frame + FRAME_TICKS / 8 && skipped_in_row < MAX_SKIPPED)
		skip_next = 1;    /* late: the next picture is not drawn, that time goes to the game */
	if (now > next_frame + FRAME_TICKS * 8)
		next_frame = now;    /* far behind (the console slept, the game was paused): start over */
	while ((now = svcGetSystemTick()) < next_frame)
	{
		const uint64_t left = next_frame - now;

		if (left > SYSCLOCK_ARM11 / 1000)
			svcSleepThread((s64)(left * 1000000000ull / SYSCLOCK_ARM11 / 2));
	}
}

static void on_frame_done(void)
{
	static int calls;

	if (calls < 3)
		trace("frame_done #%d: start (program frame %u)", calls + 1, (unsigned)program_frames());
	play_frame();
	if (calls < 3)
		trace("frame_done #%d: sound played", calls + 1);
	if (calls % 300 == 299)
		trace("frame %d", calls + 1);
	calls++;
	if (!aptMainLoop())
		longjmp(finished, 1);
	if (program_frames() % 600 == 0)
		save_eeprom();
	if (program_frames() % 120 == 0)
		show_status();
	wait_for_frame();
}

static void on_irq_changed(bool asserted)
{
	program_irq_line(asserted);
}

static void on_log(const char *message)
{
	(void)message;
}

/* ---- the assets ---- */

/* The assets pack, or from the ROM set the first time; false with what is wrong on the bottom screen */
static bool load_assets(void)
{
	game_data_result result = game_data_load(ASSETS_FILE);
	const char *problem;
	const char *source;

	trace("assets pack: result %d (0 loaded, 1 missing, 2 damaged, 3 no memory)", (int)result);
	if (result == GAME_DATA_LOADED)
		return true;
	if (result == GAME_DATA_DAMAGED)
	{
		printf("The assets pack cannot be read.\nDelete\n  " ASSETS_FILE "\nand start again to make it anew.\n");
		return false;
	}
	if (result == GAME_DATA_NO_MEMORY)
	{
		printf("Not enough memory for the game's data\n(about 24 MB). Close other apps and try again.\n");
		return false;
	}
	source = exists(ROM_ZIP) ? ROM_ZIP : (exists(ROM_FOLDER) ? ROM_FOLDER : NULL);
	if (!source)
	{
		printf("No game data.\n\nPut segasonic.assets (made on a PC: build/bin after the PC build) or\n"
			"MAME's sonic.zip (Japan rev. C) in\n  " FOLDER "\non the SD card and start again.\n");
		return false;
	}
	trace("first run: extracting from %s", source);
	printf("First run: making the assets pack\nfrom %s\nThis takes a moment...\n", source);
	gfxFlushBuffers();
	gfxSwapBuffers();
	problem = game_data_extract(source, ASSETS_FILE);
	if (problem)
	{
		printf("\nThe ROM set is not usable:\n  %s\n", problem);
		return false;
	}
	if (game_data_load(ASSETS_FILE) != GAME_DATA_LOADED)
	{
		printf("\nThe assets pack just written cannot be read.\n");
		return false;
	}
	printf("Done.\n");
	return true;
}

/* ---- the game ---- */

static void run_game(void)
{
	trace("run_game: start");
	if (!load_assets())
	{
		trace("run_game: no assets, leaving");
		return;
	}

	audio_on = start_audio();
	trace("audio: %s", audio_on ? "on" : "off");
	if (!audio_on)
		printf("No sound: sdmc:/3ds/dspfirm.cdc is missing\n(run DSP1 to dump it from the console).\n");
	chips[0].step = chips[1].step = FM_RATE / OUTPUT_RATE;
	chips[2].step = PCM_RATE / OUTPUT_RATE;

	/* the board, the video, the sound board and the game, as the headless runs set them up */
	s32_board.irq_changed = on_irq_changed;
	s32_board.controls_update = on_controls_update;
	s32_board.log = on_log;
	s32_board.frame_timing = true;
	s32_board.sound_rom = game_sound;
	s32_video_init(game_tiles, GAME_TILES_SIZE, game_sprites, GAME_SPRITES_SIZE);
	s32_board.frame_shown = on_frame_shown;
	sound_chips_attach();
	sound_cpu_attach();
	trace("hooks set, s32_init");
	s32_init(game_program);
	trace("load_eeprom");
	load_eeprom();
	trace("program_reset");
	program_reset();
	program_frame_done = on_frame_done;

	consoleClear();
	printf("SegaSonic The Hedgehog\n\nSELECT coin   START start\nA/B/X/Y button   pad: trackball\nL service   L+R test\nSTART+SELECT quit\n");
	next_frame = counted_from = svcGetSystemTick();
	trace("program_run");
	if (!setjmp(finished))
		program_run();
	trace("program_run returned");

	save_eeprom();
	if (audio_on)
	{
		ndspChnWaveBufClear(0);
		ndspExit();
		linearFree(audio_memory);
	}
}

static void game_thread(void *argument)
{
	(void)argument;
	run_game();
}

int main(void)
{
	Thread thread;
	s32 priority = 0x30;

	trace("main: start");
#ifdef EARLY_EXIT
	/* make EARLY=1: the real program, but main() only shows it was reached (see the Makefile) */
	gfxInitDefault();
	consoleInit(GFX_TOP, NULL);
	printf("segasonic early test\n\nmain() reached\nlog.txt written: %s\n\nSTART to exit\n", exists(LOG_FILE) ? "yes" : "NO");
	while (aptMainLoop())
	{
		hidScanInput();
		if (hidKeysDown() & KEY_START)
			break;
		gfxFlushBuffers();
		gfxSwapBuffers();
		gspWaitForVBlank();
	}
	gfxExit();
	return 0;
#endif
	osSetSpeedupEnable(true);    /* a New 3DS runs at 804 MHz (it does nothing on the others) */
	gfxInitDefault();
	gfxSetScreenFormat(GFX_TOP, GSP_RGB565_OES);
	gfxSetDoubleBuffering(GFX_TOP, true);
	consoleInit(GFX_BOTTOM, NULL);
	mkdir("sdmc:/3ds", 0777);
	mkdir(FOLDER, 0777);
	trace("main: graphics up");

	/* the game runs on a thread of its own, with a stack big enough for it */
	svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
	thread = threadCreate(game_thread, NULL, STACK_SIZE, priority - 1, -2, false);
	trace("main: game thread %s (priority %d)", thread ? "started" : "NOT STARTED", (int)priority);
	if (thread)
	{
		threadJoin(thread, U64_MAX);
		threadFree(thread);
		trace("main: game thread ended");
	}
	else
		printf("The game thread could not be started\n(not enough memory for its stack).\n");

	printf("\nPress START to exit.\n");
	gfxFlushBuffers();
	gfxSwapBuffers();
	while (aptMainLoop())
	{
		hidScanInput();
		if (hidKeysDown() & KEY_START)
			break;
		gfxFlushBuffers();
		gfxSwapBuffers();
		gspWaitForVBlank();
	}
	gfxExit();
	return 0;
}
