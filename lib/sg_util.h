/*
 * sg_util.h -- files, folders and buffers.
 *
 * Plain C99 with fixed-width types, so it builds with GCC, Clang and MSVC: the
 * library is linked by the command-line tool here and by the Slint setup app.
 */
#ifndef SWOSGFX_UTIL_H
#define SWOSGFX_UTIL_H

#include <stdint.h>

/* A whole file in memory. data is malloc'd; Sg_BufFree frees it. */
typedef struct {
    uint8_t  *data;
    uint32_t  size;
} SgBuf;

void     Sg_BufFree(SgBuf *b);

/* Read a whole file. 0 on success; on failure `err` (256 bytes) says why. */
int32_t  Sg_ReadFile(const char *path, SgBuf *out, char *err);

/* Write a whole file, creating its folders. 0 on success. */
int32_t  Sg_WriteFile(const char *path, const void *data, uint32_t size, char *err);

/* mkdir -p. 0 on success. */
int32_t  Sg_MakeDirs(const char *path);

/* Join two path parts with '/' into out (cap bytes). */
void     Sg_PathJoin(char *out, uint32_t cap, const char *a, const char *b);

/* Find `rel` ('/'-separated, any case) under `dir`, matching each part without
 * regard to case -- the PC game's files are upper case on a GOG install and
 * may be anything after a copy. Writes the real path to out. 0 when found. */
int32_t  Sg_FindNoCase(const char *dir, const char *rel, char *out, uint32_t cap);

/* RNC ProPack: is it packed, and unpack it (v1 and v2) into a new buffer.
 * A buffer that is not packed is copied as it is. 0 on success. */
int32_t  Sg_IsRnc(const SgBuf *b);
int32_t  Sg_RncUnpack(const SgBuf *in, SgBuf *out, char *err);

/* Big-endian reads, for Amiga data. */
static inline uint16_t Sg_Be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static inline uint32_t Sg_Be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static inline uint16_t Sg_Le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline uint32_t Sg_Le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

#endif
