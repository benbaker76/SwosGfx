/*
 * dos_palette.h -- the PC game's palettes.
 *
 * A port of SwosGfx's DosPalette.cs. Every entry is 0xAARRGGBB with alpha 0xFF,
 * as the C# keeps them (amiga_raw.h and dos_sprite.h hold copies of Menu and Game
 * with the alpha dropped, for their own converters).
 */
#ifndef SWOSGFX_DOS_PALETTE_H
#define SWOSGFX_DOS_PALETTE_H

#include <stdint.h>

#define SG_DOS_PITCH_TYPES      7       /* Frozen, Muddy, Wet, Soft, Normal, Dry, Hard */
#define SG_DOS_PITCH_COLORS     9

/* The game palette entries a pitch type recolours. */
extern const uint8_t  Sg_DosPaletteIndices[SG_DOS_PITCH_COLORS];

/* Per pitch type (DosPitch.cs PitchType order), the nine colours as the PC game
 * stores them, 0x00RRGGBB at half scale; Sg_DosGetPitchPalette doubles them. */
extern const uint32_t Sg_DosPalettePitches[SG_DOS_PITCH_TYPES][SG_DOS_PITCH_COLORS];

extern const uint32_t Sg_DosPaletteMenu[256];     /* during menus */
extern const uint32_t Sg_DosPaletteGame[256];     /* during the game; entry + 128 is its darkened twin */
extern const uint32_t Sg_DosPaletteService[16];

/* DosPalette.GetPitchPalette: the game palette with the nine entries of pitch
 * type `pitchType` (0..6) replaced. -1 when the type is out of range. */
int32_t Sg_DosGetPitchPalette(int32_t pitchType, uint32_t out[256]);

#endif
