/*
 * dos_sprite.h -- the PC game's sprites: SPRITE.DAT and the *.DAT containers.
 *
 * A port of SwosGfx's DosSprite.cs / DosSpriteCodec.cs, the reading half (the
 * writing half, sprNNNN.bmp export and import, is dos_sprite_io.h). The PC game numbers its 1334 sprites
 * across seven containers; a container is a plain run of [24-byte header]
 * [pixels] records, and SPRITE.DAT is the offset index the game builds at run
 * time, which reading does not need (its size is checked, as SwosGfx does, to
 * know the folder is the game's).
 *
 * The pixels are stored "chained": per line, four bitplanes of wquads 16-bit
 * words each, one after the other. Sg_DosSpriteDecode de-chains them (the PC
 * game's own ChainSprite, an emulation of its rcl sequence) and unpacks the
 * 4bpp result to one byte per pixel, 0..15, 0 = transparent.
 */
#ifndef SWOSGFX_DOS_SPRITE_H
#define SWOSGFX_DOS_SPRITE_H

#include <stdint.h>

#include "sg_util.h"

#define SG_DOS_NUM_SPRITES      1334
#define SG_DOS_SPRITE_DAT_SIZE  5340
#define SG_DOS_NUM_DAT_FILES    7
#define SG_DOS_TEAM_SPRITES     303    /* TEAM1/TEAM2/TEAM3.DAT */

/* One sprite. `data` points into the container it was read from. */
typedef struct {
    const uint8_t *data;       /* height * wquads * 8 bytes, chained */
    uint32_t       size;
    int16_t        width;      /* pixels */
    int16_t        height;     /* lines */
    int16_t        wquads;     /* 16-pixel units per line */
    int16_t        centerX, centerY;
    uint8_t        unk4;
    uint8_t        heightDiv4;
    int16_t        ordinal;
} SgDosSprite;

/* One container, read whole. */
typedef struct {
    SgBuf        buf;
    int32_t      count;
    SgDosSprite *spr;          /* count entries */
} SgDosDat;

/* The game's whole sprite set, numbered as the PC game numbers it:
 *   0..226 CHARSET.DAT, 227..340 SCORE.DAT, 341..643 TEAM1.DAT,
 *   644..946 TEAM3.DAT, 947..1062 and 1063..1178 GOAL1.DAT (twice),
 *   1179..1333 BENCH.DAT. */
typedef struct {
    SgDosDat           dat[SG_DOS_NUM_DAT_FILES];
    const SgDosSprite *spr[SG_DOS_NUM_SPRITES];
} SgDosSprites;

/* The seven containers, in the order they number the sprites. GOAL1.DAT is
 * listed twice: the game reads its 116 records twice over (sprites 947.. and
 * 1063..), and an import that changes both copies writes the file twice, the
 * second copy winning -- as SwosGfx does. */
typedef struct {
    const char *name;
    int32_t     entries;
    int32_t     start;     /* number of its first sprite */
} SgDosDatSpec;

extern const SgDosDatSpec Sg_DosDatSpecs[SG_DOS_NUM_DAT_FILES];

/* Read one container `name` (any case) from `dosDir`, `count` records.
 * 0 on success; on failure `err` (256 bytes) says why. */
int32_t Sg_DosDatLoad(SgDosDat *d, const char *dosDir, const char *name, int32_t count, char *err);
void    Sg_DosDatFree(SgDosDat *d);

/* Read SPRITE.DAT (size check) and the seven containers. 0 on success. */
int32_t Sg_DosSpritesLoad(SgDosSprites *s, const char *dosDir, char *err);
void    Sg_DosSpritesFree(SgDosSprites *s);

/* Decode a sprite to width*height bytes, top row first, values 0..15.
 * `out` must hold width*height bytes. 0 on success, -1 when the record's
 * geometry does not fit its data (SwosGfx would throw there). A sprite of zero
 * width or height decodes to nothing and returns 0. */
int32_t Sg_DosSpriteDecode(const SgDosSprite *spr, uint8_t *out);

/* The PC game's two palettes, 0x00RRGGBB (SwosGfx DosPalette.Menu / .Game). */
extern const uint32_t Sg_DosSpritePalMenu[256];
extern const uint32_t Sg_DosSpritePalGame[256];

#endif
