/*
 * dos_pitch_render.c -- the PC game's pitches as BMPs and Tiled maps
 * (SwosGfx DosPitch.cs and Program.cs RunDos, DosMode.Pitch).
 */
#include "dos_pitch_render.h"
#include "amiga_palette.h"
#include "dos_palette.h"
#include "sg_dir.h"
#include "sg_image.h"
#include "sg_util.h"
#include "tiled_map.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PATTERN_SIZE  16
#define TILE_BYTES    (PATTERN_SIZE * PATTERN_SIZE)

/* The two stands: the engine only ever animates fans here. */
#define FAN_POINTER_TABLE_ROW   0
#define LAST_TOP_STAND_ROW      4
#define FIRST_BOTTOM_STAND_ROW  50

const char *const Sg_DosPitchTypeNames[SG_DOS_PITCH_TYPE_COUNT] = {
    "frozen", "muddy", "wet", "soft", "normal", "dry", "hard"
};

void Sg_DosPitchTiledOptionsInit(SgDosPitchTiledOptions *o)
{
    memset(o, 0, sizeof *o);
    o->tilesetWidth = 256;
    o->splitCrowdLayers = 1;
    o->firstFanPattern = SG_DOS_PITCH_DEFAULT_FIRST_FAN;
    o->lastFanPattern = SG_DOS_PITCH_DEFAULT_LAST_FAN;
}

static int32_t CheckPitch(const SgDosPitchPatternSet *set, int32_t pitchIndex, int32_t pitchType,
                          char *err)
{
    if (pitchIndex < 0 || pitchIndex >= set->pitchCount) {
        snprintf(err, 256, "Specified argument was out of the range of valid values. (Parameter 'pitchIndex')");
        return -1;
    }
    if (pitchType < 0 || pitchType >= SG_DOS_PITCH_TYPE_COUNT) {
        snprintf(err, 256, "Specified argument was out of the range of valid values. (Parameter 'pitchType')");
        return -1;
    }
    return 0;
}

int32_t Sg_DosPitchBuildImage(const SgDosPitchPatternSet *set, int32_t pitchIndex, int32_t pitchType,
                              uint8_t indices[SG_DOS_PITCH_W * SG_DOS_PITCH_H], uint32_t palette[256],
                              char *err)
{
    if (CheckPitch(set, pitchIndex, pitchType, err) != 0)
        return -1;
    Sg_DosGetPitchPalette(pitchType, palette);
    memset(indices, 0, (size_t)SG_DOS_PITCH_W * SG_DOS_PITCH_H);

    int32_t pointerCount = set->pitches[pitchIndex].numPointers;
    for (int32_t tileY = 0; tileY < SG_DOS_PITCH_ROWS; tileY++) {
        for (int32_t tileX = 0; tileX < SG_DOS_PITCH_COLUMNS; tileX++) {
            int32_t pointerIndex = SG_DOS_PITCH_COLUMNS * tileY + tileX, patternIndex;

            /* Guard against short pointer tables */
            if (pointerIndex >= pointerCount)
                continue;
            if (Sg_DosPitchPatternIndex(set, pitchIndex, pointerIndex, &patternIndex, err) != 0)
                return -1;
            const uint8_t *pattern = Sg_DosPitchPatternBytes(set, pitchIndex, patternIndex, err);
            if (!pattern)
                return -1;
            /* BlitPattern */
            for (int32_t y = 0; y < PATTERN_SIZE; y++)
                memcpy(indices + (size_t)(tileY * PATTERN_SIZE + y) * SG_DOS_PITCH_W + tileX * PATTERN_SIZE,
                       pattern + y * PATTERN_SIZE, PATTERN_SIZE);
        }
    }
    return 0;
}

/* Write8bppIndexedBmp: quantize a copy of the indices to colorCount (QuantizeBmp
 * cuts the palette to its first colorCount entries) and write it; entries past
 * the cut palette are written as the C#'s FallbackMagenta. */
static int32_t Write8bppIndexedBmp(const char *path, int32_t width, int32_t height, const uint8_t *indices,
                                   const uint32_t palette[256], int32_t colorCount, char *err)
{
    SgImage img;

    /* Not SgImage_Init: an empty sheet is a 0-high BMP, which the C# writes. */
    memset(&img, 0, sizeof img);
    img.w = width;
    img.h = height;
    img.px = (uint8_t *)calloc((size_t)width * (size_t)height + 1, 1);
    if (!img.px) {
        snprintf(err, 256, "out of memory");
        return -1;
    }
    memcpy(img.px, indices, (size_t)width * (size_t)height);
    if (Sg_AmigaQuantizeToCount(img.px, width, height, palette, 256, colorCount, err) != 0) {
        SgImage_Free(&img);
        return -1;
    }
    SgImage_SetPaletteArgb(&img, palette, colorCount);
    for (int32_t i = colorCount; i < 256; i++) {
        img.pal[i][0] = 0xFF;
        img.pal[i][1] = 0x00;
        img.pal[i][2] = 0xFF;
    }
    int32_t rc = SgImage_WriteBmp(&img, path, err);
    SgImage_Free(&img);
    return rc;
}

int32_t Sg_DosPitchSaveBmp(const SgDosPitchPatternSet *set, int32_t pitchIndex, int32_t pitchType,
                           const char *path, int32_t colorCount, char *err)
{
    uint32_t palette[256];
    uint8_t *indices = (uint8_t *)malloc((size_t)SG_DOS_PITCH_W * SG_DOS_PITCH_H);

    if (!indices) {
        snprintf(err, 256, "out of memory");
        return -1;
    }
    int32_t rc = Sg_DosPitchBuildImage(set, pitchIndex, pitchType, indices, palette, err);
    if (rc == 0)
        rc = Write8bppIndexedBmp(path, SG_DOS_PITCH_W, SG_DOS_PITCH_H, indices, palette, colorCount, err);
    free(indices);
    return rc;
}

/* ---- the tile sheet: Dictionary<byte[], int>(TilePixelComparer) + List<byte[]> ---- */

typedef struct {
    uint8_t *pixels;        /* count * 256, in id order */
    int32_t  count, cap;
    int32_t *slots;         /* open-addressed hash of ids, -1 empty */
    int32_t  slotCount;     /* power of two */
} TileSheet;

/* TilePixelComparer.GetHashCode: FNV-1a. */
static uint32_t TileHash(const uint8_t *tile)
{
    uint32_t hash = 2166136261u;
    for (int32_t i = 0; i < TILE_BYTES; i++) {
        hash ^= tile[i];
        hash *= 16777619u;
    }
    return hash;
}

static void SheetFree(TileSheet *s)
{
    free(s->pixels);
    free(s->slots);
    memset(s, 0, sizeof *s);
}

static int32_t SheetInit(TileSheet *s)
{
    memset(s, 0, sizeof *s);
    s->slotCount = 8192;    /* the grid is 2310 cells, so never more than ~half full */
    s->slots = (int32_t *)malloc((size_t)s->slotCount * sizeof(int32_t));
    if (!s->slots)
        return -1;
    for (int32_t i = 0; i < s->slotCount; i++)
        s->slots[i] = -1;
    return 0;
}

/* The id of `tile`, adding it as the next id when it is new. -1 out of memory. */
static int32_t SheetIdFor(TileSheet *s, const uint8_t *tile)
{
    uint32_t mask = (uint32_t)s->slotCount - 1, h = TileHash(tile) & mask;

    for (;;) {
        int32_t id = s->slots[h];
        if (id < 0)
            break;
        if (memcmp(s->pixels + (size_t)id * TILE_BYTES, tile, TILE_BYTES) == 0)
            return id;
        h = (h + 1) & mask;
    }
    if (s->count * 2 >= s->slotCount)   /* cannot happen for a 42x55 grid; never loop forever */
        return -1;
    if (s->count == s->cap) {
        int32_t cap = s->cap ? s->cap * 2 : 256;
        uint8_t *p = (uint8_t *)realloc(s->pixels, (size_t)cap * TILE_BYTES);
        if (!p)
            return -1;
        s->pixels = p;
        s->cap = cap;
    }
    memcpy(s->pixels + (size_t)s->count * TILE_BYTES, tile, TILE_BYTES);
    s->slots[h] = s->count;
    return s->count++;
}

/* TileIdForPattern: the tile id for a pattern index, adding its pixels to the
 * sheet if nothing has used them yet. The standing half of a fan pair is often
 * not referenced by the map at all -- the pitch ships with the whole ground
 * seated -- so it has to be brought in deliberately or the crowd would have
 * nothing to stand up into. */
static int32_t TileIdForPattern(const SgDosPitchPatternSet *set, int32_t pitchIndex, int32_t patternIndex,
                                TileSheet *sheet, char *err)
{
    const uint8_t *pattern = Sg_DosPitchPatternBytes(set, pitchIndex, patternIndex, err);
    if (!pattern)
        return -1;
    int32_t id = SheetIdFor(sheet, pattern);
    if (id < 0)
        snprintf(err, 256, "out of memory");
    return id;
}

/* FanPatterns: which pattern indices are fans, as a flag per index (0..size-1;
 * an index at or past `size` is not a fan). */
static int32_t FanPatterns(const SgDosPitchPatternSet *set, int32_t pitchIndex, int32_t pointerCount,
                           const SgDosPitchTiledOptions *options, uint8_t **outFlags, int32_t *outSize,
                           char *err)
{
    const SgDosPitchPatterns *pp = &set->pitches[pitchIndex];
    int32_t size = 0;
    uint8_t *result;

    /* Room for every index the range or the pointer table can name. */
    if (options->lastFanPattern + 2 > size)
        size = options->lastFanPattern + 2;
    for (int32_t i = 0; i < pointerCount && i < SG_DOS_PITCH_ROWS * SG_DOS_PITCH_COLUMNS; i++)
        if (pp->patternIndices[i] + 2 > size)
            size = pp->patternIndices[i] + 2;
    result = (uint8_t *)calloc((size_t)(size > 0 ? size : 1), 1);
    if (!result) {
        snprintf(err, 256, "out of memory");
        return -1;
    }
    *outFlags = result;
    *outSize = size;

    /* The ENGINE's rule by default, not a clever one of our own: fans are
     * pattern indices 1..22 (MAX_FAN_TILE_INDEX), which is what the crowd
     * animation actually tests, so a default export reproduces exactly the
     * crowd the game animates today. -derivefans instead infers them: patterns
     * used only in the stand bands that pair off odd/even -- which also picks
     * up the hoarding and barrier rows the engine never touches, so it is
     * opt-in. An explicit -fanrange always wins. */
    if (!options->deriveFans || options->fanRangeExplicit) {
        for (int32_t p = options->firstFanPattern; p <= options->lastFanPattern; p++)
            if (p >= 0 && p < size)
                result[p] = 1;
        return 0;
    }

    uint8_t *inStands = (uint8_t *)calloc((size_t)size, 1);
    uint8_t *inField = (uint8_t *)calloc((size_t)size, 1);
    if (!inStands || !inField) {
        free(inStands);
        free(inField);
        snprintf(err, 256, "out of memory");
        return -1;
    }
    for (int32_t pointerIndex = 0;
         pointerIndex < pointerCount && pointerIndex < SG_DOS_PITCH_ROWS * SG_DOS_PITCH_COLUMNS; pointerIndex++) {
        int32_t row = pointerIndex / SG_DOS_PITCH_COLUMNS;
        int32_t pattern = pp->patternIndices[pointerIndex];
        if (row <= LAST_TOP_STAND_ROW || row >= FIRST_BOTTOM_STAND_ROW)
            inStands[pattern] = 1;
        else
            inField[pattern] = 1;
    }
    for (int32_t p = 0; p < size; p++)     /* inStands.ExceptWith(inField) */
        if (inField[p])
            inStands[p] = 0;
    for (int32_t p = 0; p + 1 < size; p++)
        if (inStands[p] && (p & 1) != 0 && inStands[p + 1]) {
            result[p] = 1;          /* sitting */
            result[p + 1] = 1;      /* ... and the same fans standing */
        }
    free(inStands);
    free(inField);
    return 0;
}

int32_t Sg_DosPitchSaveTiled(const SgDosPitchPatternSet *set, int32_t pitchIndex, int32_t pitchType,
                             const char *path, int32_t colorCount, const SgDosPitchTiledOptions *options,
                             SgLogFn warn, void *user, char *err)
{
    SgDosPitchTiledOptions defaults;
    char tilesBmpName[512], mapDir[1024], tilesBmpPath[1024];
    uint32_t palette[256];
    TileSheet sheet;
    SgTiledMap map;
    uint8_t *fanFlags = NULL, *tilesetIndices = NULL;
    int32_t fanSize = 0, crowdCells = 0, blankGid = 0, rc = -1;
    int32_t *gidCrowdSit = NULL, *gidCrowdStand = NULL;
    const int32_t cells = SG_DOS_PITCH_ROWS * SG_DOS_PITCH_COLUMNS;

    if (path == NULL) {
        snprintf(err, 256, "Value cannot be null. (Parameter 'path')");
        return -1;
    }
    if (CheckPitch(set, pitchIndex, pitchType, err) != 0)
        return -1;
    if (!options) {
        Sg_DosPitchTiledOptionsInit(&defaults);
        options = &defaults;
    }

    int32_t tilesetColumns = options->tilesetWidth / PATTERN_SIZE;
    if (tilesetColumns < 1)
        tilesetColumns = 1;

    /* The tileset is <map name>.bmp in the map's folder. */
    {
        const char *name = Sg_PathFileName(path);
        size_t dirLen = (size_t)(name - path);
        if (dirLen >= sizeof mapDir)
            dirLen = sizeof mapDir - 1;
        memcpy(mapDir, path, dirLen);
        mapDir[dirLen] = 0;
        Sg_PathChangeExt(tilesBmpName, sizeof tilesBmpName, path, ".bmp");
        snprintf(tilesBmpPath, sizeof tilesBmpPath, "%.511s%.511s", mapDir, tilesBmpName);
    }

    Sg_DosGetPitchPalette(pitchType, palette);
    int32_t pointerCount = set->pitches[pitchIndex].numPointers;

    SgTiledMap_Init(&map);
    if (SheetInit(&sheet) != 0) {
        snprintf(err, 256, "out of memory");
        return -1;
    }
    gidCrowdSit = (int32_t *)calloc((size_t)cells, sizeof(int32_t));
    gidCrowdStand = (int32_t *)calloc((size_t)cells, sizeof(int32_t));
    if (!gidCrowdSit || !gidCrowdStand || SgTiledMap_SetSize(&map, SG_DOS_PITCH_COLUMNS, SG_DOS_PITCH_ROWS,
                                                             PATTERN_SIZE, PATTERN_SIZE, err) != 0) {
        snprintf(err, 256, "out of memory");
        goto done;
    }
    int32_t *gidMap = map.gids;

    /* -insertblanktile: tile id 0 is an all-index-0 tile, so cells with no
     * pattern (a short pointer table) reference it instead of leaving a hole. */
    if (options->insertBlankTile) {
        static const uint8_t blankTile[TILE_BYTES];
        if (SheetIdFor(&sheet, blankTile) < 0) {
            snprintf(err, 256, "out of memory");
            goto done;
        }
        blankGid = 1;
    }

    if (options->splitCrowdLayers &&
        FanPatterns(set, pitchIndex, pointerCount, options, &fanFlags, &fanSize, err) != 0)
        goto done;

    for (int32_t tileY = 0; tileY < SG_DOS_PITCH_ROWS; tileY++) {
        for (int32_t tileX = 0; tileX < SG_DOS_PITCH_COLUMNS; tileX++) {
            int32_t pointerIndex = SG_DOS_PITCH_COLUMNS * tileY + tileX, patternIndex;

            if (pointerIndex >= pointerCount) {
                gidMap[pointerIndex] = blankGid;
                continue;
            }
            if (Sg_DosPitchPatternIndex(set, pitchIndex, pointerIndex, &patternIndex, err) != 0)
                goto done;
            int32_t tileId = TileIdForPattern(set, pitchIndex, patternIndex, &sheet, err);
            if (tileId < 0)
                goto done;

            /* Classify on the PATTERN index, never on the tile id, and only in
             * the stands: DOS pitch6 uses 3, 4 and 10..21 as ordinary grass. Row
             * 0 is the fan POINTER TABLE the engine walks to recolour the fans
             * (and the camera never shows it), so it keeps its tiles in the
             * pitch layer and stays off the crowd layers. */
            int32_t inStandBand = (tileY > FAN_POINTER_TABLE_ROW && tileY <= LAST_TOP_STAND_ROW) ||
                                  tileY >= FIRST_BOTTOM_STAND_ROW;

            if (inStandBand && fanFlags && patternIndex >= 0 && patternIndex < fanSize && fanFlags[patternIndex]) {
                int32_t sitPattern = (patternIndex & 1) != 0 ? patternIndex : patternIndex - 1;
                int32_t sitId = TileIdForPattern(set, pitchIndex, sitPattern, &sheet, err);
                if (sitId < 0)
                    goto done;
                int32_t standId = TileIdForPattern(set, pitchIndex, sitPattern + 1, &sheet, err);
                if (standId < 0)
                    goto done;
                gidCrowdSit[pointerIndex] = sitId + 1;
                gidCrowdStand[pointerIndex] = standId + 1;
                gidMap[pointerIndex] = 0;      /* the crowd layer draws this cell */
                crowdCells++;
                continue;
            }
            gidMap[pointerIndex] = tileId + 1;
        }
    }

    /* A pitch whose stands are mostly NOT in the fan range is telling us the
     * range is wrong for it (DOS pitch6). Say so rather than ship a crowd layer
     * with holes in it. */
    if (options->splitCrowdLayers) {
        int32_t standCells = 0;
        for (int32_t ty = 0; ty < SG_DOS_PITCH_ROWS; ty++)
            if (ty <= LAST_TOP_STAND_ROW || ty >= FIRST_BOTTOM_STAND_ROW)
                standCells += SG_DOS_PITCH_COLUMNS;
        if (crowdCells * 2 < standCells && warn) {
            char line[256];
            snprintf(line, sizeof line,
                     "  warning: pitch%d: only %d of %d stand cells are fan patterns %d..%d - "
                     "the fan range looks wrong for this pitch (-fanrange=a-b)",
                     (int)(pitchIndex + 1), (int)crowdCells, (int)standCells, (int)options->firstFanPattern,
                     (int)options->lastFanPattern);
            warn(user, line);
        }
    }

    int32_t tileCount = sheet.count;
    int32_t tilesetRows = (tileCount + tilesetColumns - 1) / tilesetColumns;
    int32_t tilesetWidth = tilesetColumns * PATTERN_SIZE;
    int32_t tilesetHeight = tilesetRows * PATTERN_SIZE;

    /* An empty sheet (no pointers at all) is a 0-high BMP, as the C# writes. */
    tilesetIndices = (uint8_t *)calloc((size_t)tilesetWidth * (size_t)(tilesetHeight ? tilesetHeight : 1), 1);
    if (!tilesetIndices) {
        snprintf(err, 256, "out of memory");
        goto done;
    }
    for (int32_t tileId = 0; tileId < tileCount; tileId++) {
        const uint8_t *tileData = sheet.pixels + (size_t)tileId * TILE_BYTES;
        int32_t dstX0 = (tileId % tilesetColumns) * PATTERN_SIZE;
        int32_t dstY0 = (tileId / tilesetColumns) * PATTERN_SIZE;
        for (int32_t y = 0; y < PATTERN_SIZE; y++)
            memcpy(tilesetIndices + (size_t)(dstY0 + y) * tilesetWidth + dstX0, tileData + y * PATTERN_SIZE,
                   PATTERN_SIZE);
    }

    /* The tileset BMP first, then the map, as the C# does. */
    if (Write8bppIndexedBmp(tilesBmpPath, tilesetWidth, tilesetHeight, tilesetIndices, palette, colorCount, err) != 0)
        goto done;

    map.tileCount = tileCount;
    map.columns = tilesetColumns;
    map.imageWidth = tilesetWidth;
    map.imageHeight = tilesetHeight;
    if (SgTiled_SetString(&map.imageSource, tilesBmpName) != 0) {
        snprintf(err, 256, "out of memory");
        goto done;
    }
    if (crowdCells > 0) {
        static const char *const names[2] = { "crowd-sit", "crowd-stand" };
        int32_t *const layerGids[2] = { gidCrowdSit, gidCrowdStand };

        if (SgTiled_SetString(&map.firstLayerName, "pitch") != 0 ||
            SgTiledProps_AddText(&map.props, "swos:role", "pitch") != 0 ||
            SgTiledProps_AddInt(&map.props, "swos:z", -100) != 0) {
            snprintf(err, 256, "out of memory");
            goto done;
        }
        /* "crowd-stand" ships hidden: it is the other half of every fan cell,
         * which the engine swaps in as the ground stands up. */
        for (int32_t k = 0; k < 2; k++) {
            SgTiledLayer *layer = SgTiledMap_AddLayer(&map, names[k], k == 0, err);
            if (!layer)
                goto done;
            memcpy(layer->gids, layerGids[k], (size_t)cells * sizeof(int32_t));
            if (SgTiledProps_AddText(&layer->props, "swos:role", names[k]) != 0 ||
                SgTiledProps_AddInt(&layer->props, "swos:z", -90) != 0) {
                snprintf(err, 256, "out of memory");
                goto done;
            }
        }
    }
    rc = SgTiledMap_Write(&map, path, err);

done:
    SgTiledMap_Free(&map);
    SheetFree(&sheet);
    free(fanFlags);
    free(tilesetIndices);
    free(gidCrowdSit);
    free(gidCrowdStand);
    return rc;
}

int32_t Sg_DosPitchRun(const char *dosDir, const char *outDir, int32_t pitchType, int32_t colorCount,
                       int32_t tiled, int32_t insertBlankTile, int32_t tilesetWidth,
                       SgLogFn warn, void *user, char *err)
{
    SgDosPitchPatternSet set;
    SgDosPitchTiledOptions options;
    int32_t rc = 0;

    if (pitchType < 0 || pitchType >= SG_DOS_PITCH_TYPE_COUNT) {
        snprintf(err, 256, "Specified argument was out of the range of valid values. (Parameter 'pitchType')");
        return -1;
    }
    if (Sg_DosPitchPatternLoad(&set, dosDir, err) != 0) {
        Sg_DosPitchPatternFree(&set);
        return -1;
    }
    Sg_DosPitchTiledOptionsInit(&options);
    options.insertBlankTile = insertBlankTile;
    options.tilesetWidth = tilesetWidth;

    for (int32_t i = 0; i < 6 && rc == 0; i++) {
        char fileName[64], outPath[1024];
        snprintf(fileName, sizeof fileName, "pitch%d-%s%s", (int)(i + 1), Sg_DosPitchTypeNames[pitchType],
                 tiled ? SG_TILED_EXTENSION : ".bmp");
        if (outDir && outDir[0])
            Sg_PathJoin(outPath, sizeof outPath, outDir, fileName);
        else
            snprintf(outPath, sizeof outPath, "%s", fileName);
        rc = tiled ? Sg_DosPitchSaveTiled(&set, i, pitchType, outPath, colorCount, &options, warn, user, err)
                   : Sg_DosPitchSaveBmp(&set, i, pitchType, outPath, colorCount, err);
    }
    Sg_DosPitchPatternFree(&set);
    return rc;
}
