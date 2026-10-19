/*
 * sg_dir.h -- folder listings and path tests, for the modes that walk a folder
 * the way SwosGfx does with Directory.GetFiles (every *.256 picture, every
 * <bank>_PATCH_<x>_<y>.bmp crop).
 */
#ifndef SWOSGFX_DIR_H
#define SWOSGFX_DIR_H

#include <stdint.h>

/* The plain files in `dir` (no folders, no "." or ".."), names only, sorted by
 * Sg_CompareNoCase. *names is malloc'd, each name too; Sg_ListDirFree frees
 * them. 0 on success (an empty folder is a success with *count 0), -1 when the
 * folder cannot be read. */
int32_t Sg_ListDir(const char *dir, char ***names, int32_t *count);
void    Sg_ListDirFree(char **names, int32_t count);

/* StringComparer.OrdinalIgnoreCase for ASCII: byte by byte, a-z as A-Z (so '_'
 * sorts AFTER the letters, as it does in SwosGfx). <0, 0, >0. */
int32_t Sg_CompareNoCase(const char *a, const char *b);

/* Does `name` end in `suffix`, any case? Does it start with `prefix`? */
int32_t Sg_EndsWithNoCase(const char *name, const char *suffix);
int32_t Sg_StartsWithNoCase(const char *name, const char *prefix);

/* 1 when the path is an existing folder / an existing file, else 0. */
int32_t Sg_IsDir(const char *path);
int32_t Sg_IsFile(const char *path);

/* Path.GetFileName: the part after the last '/' (or '\'). */
const char *Sg_PathFileName(const char *path);

/* Path.ChangeExtension(Path.GetFileName(path), ext): the file name with what
 * follows its last '.' replaced by `ext` (".bmp"), or ext appended when it has
 * none. */
void Sg_PathChangeExt(char *out, uint32_t cap, const char *path, const char *ext);

/* An absolute form of `path` (Path.GetFullPath), for messages. Falls back to
 * the path as given. */
void Sg_PathFull(char *out, uint32_t cap, const char *path);

#endif
