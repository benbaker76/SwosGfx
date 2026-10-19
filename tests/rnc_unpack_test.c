/*
 * rnc_unpack_test.c -- Sg_RncUnpack: round trips through Sg_RncPack (methods 1
 * and 2), and damaged files (truncated, bit-flipped, garbage behind a correct
 * packed CRC) that must fail with an error and never crash.
 *
 *   rnc_unpack_test            the built-in data only
 *   rnc_unpack_test DIR        also unpacks every RNC file under DIR (any
 *                              depth) and requires it to unpack with both CRCs
 *                              right, then packs it again both ways and back
 */
#include "sg_util.h"
#include "rnc_pack.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

static int32_t gFailures, gChecks;

#define CHECK(cond, ...)                                    \
    do {                                                    \
        gChecks++;                                          \
        if (!(cond)) {                                      \
            gFailures++;                                    \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);     \
            printf(__VA_ARGS__);                            \
            printf("\n");                                   \
        }                                                   \
    } while (0)

static uint32_t gRand = 12345;
static uint32_t Rand(void)
{
    gRand = gRand * 1103515245u + 12345u;
    return gRand >> 16;
}

static uint16_t Crc16(const uint8_t *p, uint32_t n)
{
    uint16_t crc = 0;
    int32_t bit;
    while (n--) {
        crc ^= *p++;
        for (bit = 0; bit < 8; ++bit)
            crc = (uint16_t)((crc & 1) ? ((crc >> 1) ^ 0xA001) : (crc >> 1));
    }
    return crc;
}

/* Pack `data` with `method`, unpack it, compare. Returns 1 when it packed. */
static int32_t RoundTrip(const char *name, const uint8_t *data, uint32_t size, int32_t method)
{
    SgBuf in, packed, back;
    char err[256];

    in.data = (uint8_t *)data;
    in.size = size;
    if (Sg_RncPack(&in, method, &packed, err) != 0) {
        /* Up to 18 bytes, or data that will not shrink: the encoder refuses,
         * and the unpacker passes an unpacked buffer through. */
        CHECK(Sg_RncUnpack(&in, &back, err) == 0 && back.size == size &&
                  (size == 0 || memcmp(back.data, data, size) == 0),
              "%s: unpacked input not passed through", name);
        Sg_BufFree(&back);
        return 0;
    }
    CHECK(Sg_IsRnc(&packed) && packed.data[3] == method, "%s m%d: not RNC", name, (int)method);
    if (Sg_RncUnpack(&packed, &back, err) != 0) {
        CHECK(0, "%s m%d: unpack failed: %s", name, (int)method, err);
    } else {
        CHECK(back.size == size && memcmp(back.data, data, size) == 0,
              "%s m%d: round trip differs", name, (int)method);
        Sg_BufFree(&back);
    }
    Sg_BufFree(&packed);
    return 1;
}

/* Every way of damaging one packed file: each must fail, or (a flip the CRCs
 * cannot see, which does not happen here but is not an error) succeed. What it
 * must never do is crash or read out of bounds -- run under ASan to see that. */
static void Damage(const char *name, const SgBuf *packed)
{
    SgBuf bad, out;
    char err[256];
    uint32_t i, n, cut, bit;
    int32_t failed = 0, tries = 0;

    bad.data = (uint8_t *)malloc(packed->size);

    /* Truncated: every length, which leaves the header saying more than is there. */
    for (cut = 0; cut < packed->size; ++cut) {
        memcpy(bad.data, packed->data, cut);
        bad.size = cut;
        err[0] = 0;
        if (Sg_RncUnpack(&bad, &out, err) == 0) {
            /* Only possible below 18 bytes, where it is not RNC: copied. */
            CHECK(!Sg_IsRnc(&bad) && out.size == cut, "%s: truncated to %u unpacked", name, (unsigned)cut);
            Sg_BufFree(&out);
        } else
            CHECK(err[0] != 0 && out.data == NULL, "%s: truncated to %u failed without an error", name, (unsigned)cut);
    }

    /* Bit flips, at most 4000 of them spread over the file. */
    n = packed->size * 8;
    for (i = 0; i < n; i += n / 4000 + 1) {
        memcpy(bad.data, packed->data, packed->size);
        bad.size = packed->size;
        bad.data[i / 8] ^= (uint8_t)(1u << (i % 8));
        err[0] = 0;
        tries++;
        if (Sg_RncUnpack(&bad, &out, err) == 0)
            Sg_BufFree(&out);
        else {
            CHECK(err[0] != 0, "%s: flip %u failed without an error", name, (unsigned)i);
            failed++;
        }
    }
    /* Every flip outside the method byte's low bits and the two pad bytes is a
     * CRC failure; nearly all must fail. */
    CHECK(failed >= tries * 9 / 10, "%s: only %d of %d flips failed", name, (int)failed, (int)tries);

    /* Garbage behind a correct packed CRC: the decoder itself meets bad data.
     * Random bytes, random flips, and runs of 0x00 / 0xFF. */
    for (i = 0; i < 600; ++i) {
        uint32_t data = packed->size - 18, j;
        memcpy(bad.data, packed->data, packed->size);
        bad.size = packed->size;
        if (i < 200) {
            for (j = 0; j < 1 + i % 8; ++j) {
                bit = Rand() % (data * 8);
                bad.data[18 + bit / 8] ^= (uint8_t)(1u << (bit % 8));
            }
        } else if (i < 400) {
            for (j = 18; j < bad.size; ++j)
                bad.data[j] = (uint8_t)Rand();
        } else {
            uint32_t at = 18 + Rand() % data, len = 1 + Rand() % data;
            for (j = at; j < bad.size && j < at + len; ++j)
                bad.data[j] = (i & 1) ? 0xFF : 0x00;
        }
        if (i % 3 == 0) {   /* and sometimes a lying unpacked size */
            uint32_t sz = (i % 2) ? Rand() * 7 : Rand() % 64;
            bad.data[4] = (uint8_t)(sz >> 24); bad.data[5] = (uint8_t)(sz >> 16);
            bad.data[6] = (uint8_t)(sz >> 8);  bad.data[7] = (uint8_t)sz;
        }
        {
            uint16_t c = Crc16(bad.data + 18, data);
            bad.data[14] = (uint8_t)(c >> 8);
            bad.data[15] = (uint8_t)c;
        }
        err[0] = 0;
        if (Sg_RncUnpack(&bad, &out, err) == 0)
            Sg_BufFree(&out);
        else
            CHECK(err[0] != 0, "%s: garbage %u failed without an error", name, (unsigned)i);
    }

    free(bad.data);
}

static void BuiltIn(void)
{
    static const char *text =
        "Sensible World of Soccer, 1994. Sensible Software. The ball is round, the game "
        "lasts ninety minutes, and everything else is theory. ";
    uint32_t sizes[] = { 0, 1, 2, 17, 18, 19, 100, 4096, 0x3000, 0x3001, 70000, 200000 };
    uint32_t s, i;
    int32_t m, packedCount = 0;
    uint8_t *buf = (uint8_t *)malloc(200000);
    char name[64];

    for (s = 0; s < sizeof(sizes) / sizeof(sizes[0]); ++s) {
        uint32_t n = sizes[s];
        for (m = 1; m <= 2; ++m) {
            /* random: does not pack; zeros, one byte value; text; a mix of
             * literal runs and long matches at every distance. */
            for (i = 0; i < n; ++i) buf[i] = (uint8_t)Rand();
            snprintf(name, sizeof name, "random %u", (unsigned)n);
            packedCount += RoundTrip(name, buf, n, m);

            memset(buf, 0, n);
            snprintf(name, sizeof name, "zeros %u", (unsigned)n);
            packedCount += RoundTrip(name, buf, n, m);

            for (i = 0; i < n; ++i) buf[i] = (uint8_t)text[i % strlen(text)];
            snprintf(name, sizeof name, "text %u", (unsigned)n);
            packedCount += RoundTrip(name, buf, n, m);

            for (i = 0; i < n; ++i)
                buf[i] = (i / 37) % 3 == 0 ? (uint8_t)Rand()
                       : (i >= 0x7000 ? buf[i - 0x7000 + (i % 5)] : (uint8_t)(i * 7));
            snprintf(name, sizeof name, "mixed %u", (unsigned)n);
            packedCount += RoundTrip(name, buf, n, m);

            for (i = 0; i < n; ++i) buf[i] = (uint8_t)((i % 13) == 0 ? Rand() & 3 : i % 251);
            snprintf(name, sizeof name, "sparse %u", (unsigned)n);
            packedCount += RoundTrip(name, buf, n, m);
        }
    }
    printf("built-in round trips: %d packed and back\n", (int)packedCount);

    /* Damage a packed file of each method. */
    for (m = 1; m <= 2; ++m) {
        SgBuf in, packed;
        char err[256];
        for (i = 0; i < 6000; ++i)
            buf[i] = (i / 50) % 2 ? (uint8_t)text[i % strlen(text)] : (uint8_t)(Rand() & 0x0F);
        in.data = buf;
        in.size = 6000;
        if (Sg_RncPack(&in, m, &packed, err) != 0) {
            CHECK(0, "pack m%d for the damage test: %s", (int)m, err);
            continue;
        }
        snprintf(name, sizeof name, "damaged m%d", (int)m);
        Damage(name, &packed);
        Sg_BufFree(&packed);
    }
    free(buf);
}

/* --- a folder of the game's files ------------------------------------- */

static int32_t gDirFiles, gDirRoundTrips;

static void CheckFile(const char *path)
{
    SgBuf in, out;
    char err[256];
    int32_t m;

    if (Sg_ReadFile(path, &in, err) != 0)
        return;
    if (Sg_IsRnc(&in)) {
        gDirFiles++;
        if (Sg_RncUnpack(&in, &out, err) != 0)
            CHECK(0, "%s: %s", path, err);
        else {
            CHECK(out.size == Sg_Be32(in.data + 4), "%s: size", path);
            for (m = 1; m <= 2; ++m)
                gDirRoundTrips += RoundTrip(path, out.data, out.size, m);
            Sg_BufFree(&out);
        }
    }
    Sg_BufFree(&in);
}

static void Walk(const char *dir)
{
    char path[1024];
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    HANDLE h;
    snprintf(path, sizeof path, "%s\\*", dir);
    h = FindFirstFileA(path, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return;
    do {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, ".."))
            continue;
        Sg_PathJoin(path, sizeof path, dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            Walk(path);
        else
            CheckFile(path);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(dir);
    struct dirent *e;
    struct stat st;
    if (!d)
        return;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        Sg_PathJoin(path, sizeof path, dir, e->d_name);
        if (stat(path, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode))
            Walk(path);
        else if (S_ISREG(st.st_mode))
            CheckFile(path);
    }
    closedir(d);
#endif
}

int main(int argc, char **argv)
{
    BuiltIn();
    if (argc > 1) {
        Walk(argv[1]);
        printf("%s: %d RNC files unpacked, %d packed again and back\n", argv[1],
               (int)gDirFiles, (int)gDirRoundTrips);
    }
    printf("%d checks, %d failed\n", (int)gChecks, (int)gFailures);
    return gFailures ? 1 : 0;
}
