/**
 *    Name:        data.h
 *    Description: the game's data in the board's memory, loaded from its assets pack
 *    Version:     0.2.0
 *    Created:     13-10-2026
 *    Modified:    20-10-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    No executable carries the ROM or anything from it. game_data_load fills the regions from the assets
 *    pack (tools/make_pack.py writes it from the assets extract_assets.py takes out of the player's ROM
 *    set): every asset where the board's memory has it, then the board's mirrors of its chips. The original
 *    code is in no region: the C replaces it. The reference (tools/reference) fills them from the ROM set,
 *    which it runs, when it starts.
 */
#ifndef GAME_DATA_H
#define GAME_DATA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GAME_PROGRAM_SIZE   0x200000     /* the V60's program space, as it sees it at 000000: the data */
#define GAME_TILES_SIZE     0x200000     /* 16x16 tiles at 4 bits a pixel */
#define GAME_SPRITES_SIZE   0x1000000    /* sprite pixels, every 32-bit word big endian */
#define GAME_SOUND_SIZE     0x400000     /* the Z80's program space (its data), then three 1 MB banks of
                                            PCM samples */

/* The regions are static arrays. A port that cannot spare 24 MB of static memory (the 3DS: its loader
   maps a program's whole .bss before it starts) builds with SONIC_DYNAMIC_DATA: they are then pointers to
   memory game_data_allocate() takes from the heap, once, before anything is loaded. */
#ifdef SONIC_DYNAMIC_DATA
extern uint8_t *game_program;
extern uint8_t *game_tiles;
extern uint8_t *game_sprites;
extern uint8_t *game_sound;
#else
extern uint8_t game_program[GAME_PROGRAM_SIZE];
extern uint8_t game_tiles[GAME_TILES_SIZE];
extern uint8_t game_sprites[GAME_SPRITES_SIZE];
extern uint8_t game_sound[GAME_SOUND_SIZE];
#endif

/* Makes the regions ready (nothing to do for static arrays): 1, or 0 when the memory is not there. The
   loaders call it themselves; it can be called again. */
int game_data_allocate(void);

typedef enum
{
	GAME_DATA_LOADED,
	GAME_DATA_MISSING,    /* no pack at the path */
	GAME_DATA_DAMAGED,    /* a pack the game cannot read: another version, cut short, or out of bounds */
	GAME_DATA_NO_MEMORY   /* the regions could not be allocated (SONIC_DYNAMIC_DATA) */
} game_data_result;

game_data_result game_data_load(const char *path);

/* The board's mirrors of its chips: what the regions repeat where the board decodes the same chip again */
void game_data_mirror(void);

#ifdef __cplusplus
}
#endif

#endif
