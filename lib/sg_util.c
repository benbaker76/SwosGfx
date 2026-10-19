/*
 * sg_util.c -- files, folders and buffers.
 */
#include "sg_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define sg_mkdir(p) _mkdir(p)
#else
#include <dirent.h>
#include <sys/stat.h>
#define sg_mkdir(p) mkdir((p), 0755)
#endif

void Sg_BufFree(SgBuf *b)
{
    free(b->data);
    b->data = NULL;
    b->size = 0;
}

int32_t Sg_ReadFile(const char *path, SgBuf *out, char *err)
{
    out->data = NULL;
    out->size = 0;
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(err, 256, "cannot open %s", path);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) {
        fclose(f);
        snprintf(err, 256, "cannot read %s", path);
        return -1;
    }
    out->data = malloc(size ? (size_t)size : 1);
    if (size && fread(out->data, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        Sg_BufFree(out);
        snprintf(err, 256, "cannot read %s", path);
        return -1;
    }
    fclose(f);
    out->size = (uint32_t)size;
    return 0;
}

int32_t Sg_MakeDirs(const char *path)
{
    char tmp[1024];
    size_t len = strlen(path);
    if (len >= sizeof tmp)
        return -1;
    memcpy(tmp, path, len + 1);
    for (size_t i = 1; i <= len; i++) {
        if (tmp[i] == '/' || tmp[i] == '\\' || tmp[i] == '\0') {
            char c = tmp[i];
            tmp[i] = '\0';
            /* Skip a drive ("C:"), which mkdir refuses. */
            if (!(i == 2 && tmp[1] == ':'))
                if (sg_mkdir(tmp) != 0 && errno != EEXIST)
                    return -1;
            tmp[i] = c;
        }
    }
    return 0;
}

int32_t Sg_WriteFile(const char *path, const void *data, uint32_t size, char *err)
{
    char dir[1024];
    snprintf(dir, sizeof dir, "%s", path);
    char *slash = strrchr(dir, '/');
#ifdef _WIN32
    char *bslash = strrchr(dir, '\\');
    if (bslash && (!slash || bslash > slash))
        slash = bslash;
#endif
    if (slash) {
        *slash = '\0';
        if (dir[0] && Sg_MakeDirs(dir) != 0) {
            snprintf(err, 256, "cannot create the folder %.200s", dir);
            return -1;
        }
    }
    FILE *f = fopen(path, "wb");
    if (!f) {
        snprintf(err, 256, "cannot write %s", path);
        return -1;
    }
    if (size && fwrite(data, 1, size, f) != size) {
        fclose(f);
        snprintf(err, 256, "cannot write %s", path);
        return -1;
    }
    fclose(f);
    return 0;
}

void Sg_PathJoin(char *out, uint32_t cap, const char *a, const char *b)
{
    size_t la = strlen(a);
    if (la && (a[la - 1] == '/' || a[la - 1] == '\\'))
        snprintf(out, cap, "%s%s", a, b);
    else if (la)
        snprintf(out, cap, "%s/%s", a, b);
    else
        snprintf(out, cap, "%s", b);
}

static int32_t eq_nocase(const char *a, const char *b, size_t blen)
{
    if (strlen(a) != blen)
        return 0;
    for (size_t i = 0; i < blen; i++)
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i]))
            return 0;
    return 1;
}

/* Find one name in one folder, any case. */
static int32_t find_entry(const char *dir, const char *name, size_t len, char *out, uint32_t cap)
{
#ifdef _WIN32
    /* Windows file names do not care about case already. */
    char part[260];
    if (len >= sizeof part)
        return -1;
    memcpy(part, name, len);
    part[len] = '\0';
    Sg_PathJoin(out, cap, dir, part);
    return GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES ? 0 : -1;
#else
    DIR *d = opendir(dir[0] ? dir : ".");
    if (!d)
        return -1;
    struct dirent *e;
    int32_t rc = -1;
    while ((e = readdir(d)) != NULL) {
        if (eq_nocase(e->d_name, name, len)) {
            Sg_PathJoin(out, cap, dir, e->d_name);
            rc = 0;
            break;
        }
    }
    closedir(d);
    return rc;
#endif
}

int32_t Sg_FindNoCase(const char *dir, const char *rel, char *out, uint32_t cap)
{
    char cur[1024];
    snprintf(cur, sizeof cur, "%s", dir);
    const char *p = rel;
    while (*p) {
        while (*p == '/')
            p++;
        const char *end = strchr(p, '/');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (!len)
            break;
        char next[1024];
        if (find_entry(cur, p, len, next, sizeof next) != 0)
            return -1;
        snprintf(cur, sizeof cur, "%s", next);
        p += len;
    }
    snprintf(out, cap, "%s", cur);
    return 0;
}

int32_t Sg_IsRnc(const SgBuf *b)
{
    return b->size >= 18 && b->data[0] == 'R' && b->data[1] == 'N' && b->data[2] == 'C' &&
           (b->data[3] == 1 || b->data[3] == 2);
}
