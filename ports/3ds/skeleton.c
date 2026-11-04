/**
 *    Name:        skeleton.c
 *    Description: a tiny 3DS program to tell what the console lets this port do, with none of the game in it
 *    Version:     0.1.0
 *    Created:     04-11-2026
 *    Modified:    04-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    Usage: make SKELETON=1 -> segasonic_test.3dsx. It shows on the screen: that a .3dsx made with this
 *    Makefile's flags loads and runs, how much memory the application has, whether the 24 MB the game
 *    keeps can be allocated, whether a thread with a 1 MB stack starts, and whether the audio DSP opens.
 *    START leaves.
 */
#include <3ds.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MB (1024u * 1024u)

static volatile int thread_ran;

static void worker(void *argument)
{
	volatile char big[200 * 1024];    /* a stack the main thread's default one could not hold */

	(void)argument;
	memset((void *)big, 1, sizeof(big));
	thread_ran = big[sizeof(big) - 1];
}

int main(void)
{
	static const size_t sizes[4] = { 2 * MB, 2 * MB, 16 * MB, 4 * MB };    /* the game's four regions */
	void *blocks[4] = { NULL };
	Thread thread;
	s32 priority = 0x30;
	int allocated = 0;

	osSetSpeedupEnable(true);
	gfxInitDefault();
	consoleInit(GFX_TOP, NULL);
	printf("segasonic 3DS test\n\nboot: ok (the .3dsx loaded and runs)\n");
	printf("application memory free: %u MB\n", (unsigned)(osGetMemRegionFree(MEMREGION_APPLICATION) / MB));
	printf("linear memory free:      %u MB\n", (unsigned)(linearSpaceFree() / MB));

	for (int index = 0; index < 4; index++)
	{
		blocks[index] = malloc(sizes[index]);
		if (blocks[index])
		{
			memset(blocks[index], 0xA5, sizes[index]);    /* (touched: the pages really exist) */
			allocated++;
		}
	}
	printf("heap: %d of 4 game regions (24 MB) allocated\n", allocated);

	svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
	thread = threadCreate(worker, NULL, 1 * MB, priority - 1, -2, false);
	if (thread)
	{
		threadJoin(thread, U64_MAX);
		threadFree(thread);
	}
	printf("thread with a 1 MB stack: %s\n", thread ? (thread_ran ? "ok" : "started, did not run") : "FAILED");

	if (R_SUCCEEDED(ndspInit()))
	{
		printf("audio DSP: ok\n");
		ndspExit();
	}
	else
		printf("audio DSP: no (sdmc:/3ds/dspfirm.cdc missing?)\n");

	printf("\nSTART to exit\n");
	while (aptMainLoop())
	{
		hidScanInput();
		if (hidKeysDown() & KEY_START)
			break;
		gfxFlushBuffers();
		gfxSwapBuffers();
		gspWaitForVBlank();
	}
	for (int index = 0; index < 4; index++)
		free(blocks[index]);
	gfxExit();
	return 0;
}
