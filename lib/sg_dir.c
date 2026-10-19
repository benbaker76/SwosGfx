/*
 * sg_dir.c -- folder listings and path tests (see sg_dir.h).
 */
#if !defined(_WIN32) && !defined(_XOPEN_SOURCE)
#define _XOPEN_SOURCE 700     /* realpath */
#endif
#include "sg_dir.h"
#include "sg_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <limits.h>
#include <sys/stat.h>
#endif

static int32_t Upper(int32_t c)
{
    return c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c;
}

int32_t Sg_CompareNoCase(const char *a, const char *b)
{
    const uint8_t *p = (const uint8_t *)a, *q = (const uint8_t *)b;

    for (;; p++, q++) {
        int32_t x = Upper(*p), y = Upper(*q);
        if (x != y || !x)
            return x - y;
    }
}

int32_t Sg_EndsWithNoCase(const char *name, const char *suffix)
{
    size_t n = strlen(name), s = strlen(suffix);
    return n >= s && Sg_CompareNoCase(name + n - s, suffix) == 0;
}

int32_t Sg_StartsWithNoCase(const char *name, const char *prefix)
{
    for (; *prefix; name++, prefix++)
        if (Upper((uint8_t)*name) != Upper((uint8_t)*prefix))
            return 0;
    return 1;
}

static int CompareNames(const void *a, const void *b)
{
    return (int)Sg_CompareNoCase(*(const char *const *)a, *(const char *const *)b);
}

void Sg_ListDirFree(char **names, int32_t count)
{
    if (!names)
        return;
    for (int32_t i = 0; i < count; i++)
        free(names[i]);
    free(names);
}

static int32_t Push(char ***names, int32_t *count, int32_t *cap, const char *name)
{
    char *copy;

    if (*count == *cap) {
        int32_t nc = *cap ? *cap * 2 : 32;
        char **n = realloc(*names, (size_t)nc * sizeof *n);
        if (!n)
            return -1;
        *names = n;
        *cap = nc;
    }
    copy = malloc(strlen(name) + 1);
    if (!copy)
        return -1;
    strcpy(copy, name);
    (*names)[(*count)++] = copy;
    return 0;
}

int32_t Sg_ListDir(const char *dir, char ***names, int32_t *count)
{
    int32_t cap = 0;

    *names = NULL;
    *count = 0;
#ifdef _WIN32
    {
        char pattern[1024];
        WIN32_FIND_DATAA fd;
        HANDLE h;

        Sg_PathJoin(pattern, sizeof pattern, dir[0] ? dir : ".", "*");
        h = FindFirstFileA(pattern, &fd);
        if (h == INVALID_HANDLE_VALUE)
            return GetLastError() == ERROR_FILE_NOT_FOUND ? 0 : -1;
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;
            if (Push(names, count, &cap, fd.cFileName) != 0) {
                FindClose(h);
                Sg_ListDirFree(*names, *count);
                *names = NULL;
                *count = 0;
                return -1;
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
#else
    {
        DIR *d = opendir(dir[0] ? dir : ".");
        struct dirent *e;

        if (!d)
            return -1;
        while ((e = readdir(d)) != NULL) {
            char path[1024];
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
                continue;
            Sg_PathJoin(path, sizeof path, dir, e->d_name);
            if (!Sg_IsFile(path))
                continue;
            if (Push(names, count, &cap, e->d_name) != 0) {
                closedir(d);
                Sg_ListDirFree(*names, *count);
                *names = NULL;
                *count = 0;
                return -1;
            }
        }
        closedir(d);
    }
#endif
    if (*count > 1)
        qsort(*names, (size_t)*count, sizeof **names, CompareNames);
    return 0;
}

int32_t Sg_IsDir(const char *path)
{
#ifdef _WIN32
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
#else
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode) ? 1 : 0;
#endif
}

int32_t Sg_IsFile(const char *path)
{
#ifdef _WIN32
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
#else
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode) ? 1 : 0;
#endif
}

const char *Sg_PathFileName(const char *path)
{
    const char *base = path;

    for (const char *p = path; *p; p++)
        if (*p == '/' || *p == '\\')
            base = p + 1;
    return base;
}

void Sg_PathChangeExt(char *out, uint32_t cap, const char *path, const char *ext)
{
    const char *name = Sg_PathFileName(path);
    const char *dot = strrchr(name, '.');
    size_t stem = dot ? (size_t)(dot - name) : strlen(name);

    snprintf(out, cap, "%.*s%s", (int)stem, name, ext);
}

void Sg_PathFull(char *out, uint32_t cap, const char *path)
{
#ifdef _WIN32
    if (!_fullpath(out, path, cap))
        snprintf(out, cap, "%s", path);
#else
    char buf[PATH_MAX];
    if (realpath(path, buf))
        snprintf(out, cap, "%s", buf);
    else
        snprintf(out, cap, "%s", path);
#endif
}
