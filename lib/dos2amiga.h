/*
 * dos2amiga.h -- rebuild the Amiga game's 320x256 sprite banks from the PC
 * game's sprites (SwosGfx -dos2amiga).
 *
 * The Amiga game cuts its sprites out of 320x256 banks (CHARSET.RAW,
 * CJCGRAFS.RAW, ...) by running "image command scripts": a load opcode names
 * the bank, every draw opcode after it says which sprite slot is cut from
 * which (x, y) of the bank, and how big. Run the same scripts the other way --
 * for every draw, put the PC game's version of that sprite where the Amiga
 * game will cut it from -- and you have banks with the Amiga layout and the PC
 * pixels, at 256 colours.
 *
 * The scripts are the Amiga game's own, word for word (amiga_scripts.c).
 *
 * Which PC sprite fills an Amiga slot:
 *   - gameplay banks (menuColorAndTextScript): PC index = Amiga index + 114,
 *     except the kit banks, whose slot 227+n is sprite n of TEAMn.DAT;
 *   - CHARSET, MENUS, MENUS2: PC index = Amiga index, slots >= 227 (the font
 *     extension) have no PC sprite and are left empty -- Sg_FontExtend draws them.
 * SOCCER_S (the big R/S sprites) is drawn in the game palette's 0x70..0x7F.
 */
#ifndef SWOSGFX_DOS2AMIGA_H
#define SWOSGFX_DOS2AMIGA_H

#include <stdint.h>

#include "dos_sprite.h"
#include "sg_image.h"
#include "sg_log.h"

#define SG_BANK_W 320
#define SG_BANK_H 256

typedef enum {
    SG_BANK_CJCGRAFS, SG_BANK_CJCTEAM1, SG_BANK_CJCTEAM2, SG_BANK_CJCTEAM3,
    SG_BANK_CJCTEAMG, SG_BANK_CJCBITS, SG_BANK_SOCCER_S, SG_BANK_CJCBENCH,
    SG_BANK_CHARSET, SG_BANK_MENUS, SG_BANK_MENUS2,
    SG_BANK_COUNT
} SgBank;

/* "CJCGRAFS", ... -- the bank's file name without ".RAW". */
extern const char *const Sg_BankNames[SG_BANK_COUNT];

/* ---- the image-command scripts and their interpreter ------------------ */

typedef enum {
    SG_SCRIPT_INTRO,            /* introMenuImageScript: CHARSET + font extension */
    SG_SCRIPT_MENU_BG,          /* menuBGImageCmdStream: MENUS, MENUS2 */
    SG_SCRIPT_MENU_COLOR_TEXT,  /* menuColorAndTextScript: the gameplay banks */
    SG_SCRIPT_COUNT
} SgScript;

/* The script's words (bank-load pointer words are 0). */
const uint16_t *Sg_ScriptWords(SgScript script, int32_t *count);

/* Where the interpreter says a sprite slot is cut from. */
typedef struct {
    int32_t amigaIndex;         /* sprite slot, opcode & 0x0FFF */
    int32_t x0, y0;             /* top left in the bank (x rounded down to 16) */
    int32_t winW, winH;         /* the cut window: width rounded up to 16 */
} SgPlacement;

/* Every draw after one load opcode, in script order. */
typedef struct {
    int32_t      loadOffset;    /* word index of the 0x7000/0x8000 opcode */
    int32_t      count, cap;
    SgPlacement *items;
} SgPlacementList;

typedef struct {
    int32_t         numLoads;
    SgPlacementList loads[16];
} SgScriptPlacements;

/* Walk a script (SwosGfx DosToAmiga.Interpret, after the game's
 * ProcessImageCommandStream). 0 on success; -1 on an unknown opcode. */
int32_t Sg_ScriptInterpret(const uint16_t *words, int32_t count, SgScriptPlacements *out, char *err);
void    Sg_ScriptPlacementsFree(SgScriptPlacements *p);
/* The draws after the load opcode at word `loadOffset`, or NULL. */
const SgPlacementList *Sg_ScriptFindLoad(const SgScriptPlacements *p, int32_t loadOffset);

/* How each bank is built from the scripts. */
typedef enum { SG_MAP_GAMEPLAY_PLUS114, SG_MAP_MENU_IDENTITY, SG_MAP_TEAM } SgBankMap;

typedef struct {
    SgScript     script;
    int32_t      loadOffset;    /* word index of the bank's load opcode */
    SgBankMap    map;
    int32_t      orMask;        /* OR'd into every pixel drawn (0x70 for SOCCER_S) */
    int32_t      gamePalette;   /* 1: Sg_DosSpritePalGame, 0: Sg_DosSpritePalMenu */
    const char  *teamFile;      /* SG_MAP_TEAM: the container to draw from */
} SgBankInfo;

extern const SgBankInfo Sg_BankInfo[SG_BANK_COUNT];

/* Draw a decoded sprite (spr x sprH, 0 = transparent) into a bank at a
 * placement, clipped to its window and the bank, each pixel OR orMask. */
void Sg_BankBlit(SgImage *bank, const uint8_t *px, int32_t sprW, int32_t sprH,
                 const SgPlacement *p, int32_t orMask);

/* Decode a PC sprite and Sg_BankBlit it. 0 on success, -1 when the sprite's
 * record is broken. A NULL or empty sprite draws nothing. */
int32_t Sg_BankBlitDosSprite(SgImage *bank, const SgDosSprite *spr, const SgPlacement *p, int32_t orMask);

/* Build all eleven banks from the PC game in `dosDir`: each SgImage is set up
 * here (320x256, the PC palette it was drawn in) and freed by the caller with
 * SgImage_Free. CHARSET is returned WITHOUT the font extension (Sg_FontExtend).
 * 0 on success; on failure nothing is left allocated and `err` says why. */
int32_t Sg_DosToAmigaBanks(const char *dosDir, SgImage banks[SG_BANK_COUNT], char *err);

/* The same for only the banks whose bit (1u << SgBank) is set in `mask`; the
 * others are left zeroed (SgImage_Free on them is harmless). */
int32_t Sg_DosToAmigaBanksSel(const char *dosDir, uint32_t mask, SgImage banks[SG_BANK_COUNT], char *err);

/* SwosGfx -dos -dos2amiga [outDir]: the eleven banks written as
 * <outDir>/<NAME>.bmp (8bpp, SgImage_WriteBmp -- byte for byte what
 * DosToAmiga.WriteBankBmp writes), the folder made if need be. CHARSET is
 * without the font extension there too (that is -font-extend's job). Then the
 * one line SwosGfx prints, "DOS -> Amiga: wrote 11 banks to <outDir>", goes to
 * `log` (may be NULL). 0 on success; on failure `err` says why. */
int32_t Sg_Dos2AmigaRun(const char *dosDir, const char *outDir, SgLogFn log, void *user, char *err);

#endif
