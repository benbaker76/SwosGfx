/*
 * amiga_scripts.h -- the Amiga game's image-command scripts (amiga_scripts.c).
 */
#ifndef SWOSGFX_AMIGA_SCRIPTS_H
#define SWOSGFX_AMIGA_SCRIPTS_H

#include <stdint.h>

/* CHARSET load, the intro's glyphs and the font-extension glyphs, ending 0xFFFF. */
extern const uint16_t g_sgIntroScript[];
extern const int32_t  g_sgIntroScriptCount;
/* The menu backdrops: loads MENUS, then MENUS2. */
extern const uint16_t g_sgMenuBgScript[];
extern const int32_t  g_sgMenuBgScriptCount;
/* The gameplay banks. */
extern const uint16_t g_sgMenuColorTextScript[];
extern const int32_t  g_sgMenuColorTextScriptCount;

#endif
