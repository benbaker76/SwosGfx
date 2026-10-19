/*
 * tiled_map.c -- Tiled map files (see tiled_map.h).
 *
 * Ported from SwosGfx TiledMap.cs: Write is its StringBuilder sequence line for
 * line, so the bytes match; ReadJson / ReadTmx keep its checks and messages in
 * order. The JSON comes through sg_json.c; the legacy .tmx through the small XML
 * reader at the bottom of this file, which does what XDocument.Parse did for
 * these files and no more.
 */
#include "tiled_map.h"
#include "sg_json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FORMAT_VERSION "1.10"
#define TILED_VERSION  "1.10.2"
#define LAYER_NAME     "Tile Layer 1"
#define TILESET_NAME   "tiles"

/* ------------------------------------------------------------------------ */
/* A growing byte buffer for the writer                                     */
/* ------------------------------------------------------------------------ */

typedef struct {
    char    *data;
    uint32_t size, cap;
    int32_t  oom;
} Sb;

static void SbPutN(Sb *sb, const char *s, uint32_t n)
{
    if (sb->oom)
        return;
    if ((uint64_t)sb->size + n + 1 > sb->cap) {
        uint64_t cap = sb->cap ? sb->cap : 4096;
        char *nd;
        while (cap < (uint64_t)sb->size + n + 1)
            cap *= 2;
        if (cap > 0xFFFFFFF0u || !(nd = realloc(sb->data, (size_t)cap))) {
            sb->oom = 1;
            return;
        }
        sb->data = nd;
        sb->cap = (uint32_t)cap;
    }
    memcpy(sb->data + sb->size, s, n);
    sb->size += n;
    sb->data[sb->size] = 0;
}

static void SbPut(Sb *sb, const char *s) { SbPutN(sb, s, (uint32_t)strlen(s)); }

/* Num(): int.ToString(InvariantCulture) is plain %d. */
static void SbNum(Sb *sb, int32_t v)
{
    char t[16];
    snprintf(t, sizeof t, "%d", (int)v);
    SbPut(sb, t);
}

/* Str(): a JSON string the way TiledMap.Str escapes it -- the two that must be,
 * the five short escapes, other C0 controls as \u00xx (lower-case hex), and
 * everything else (DEL, UTF-8) as it is. `n` bytes, which may include NULs. */
static void SbStrN(Sb *sb, const char *s, uint32_t n)
{
    SbPut(sb, "\"");
    for (uint32_t i = 0; i < n; i++) {
        uint8_t c = (uint8_t)s[i];
        switch (c) {
        case '"':  SbPut(sb, "\\\""); break;
        case '\\': SbPut(sb, "\\\\"); break;
        case '\b': SbPut(sb, "\\b");  break;
        case '\f': SbPut(sb, "\\f");  break;
        case '\n': SbPut(sb, "\\n");  break;
        case '\r': SbPut(sb, "\\r");  break;
        case '\t': SbPut(sb, "\\t");  break;
        default:
            if (c < 0x20) {
                char t[8];
                snprintf(t, sizeof t, "\\u%04x", (unsigned)c);
                SbPut(sb, t);
            } else {
                SbPutN(sb, (const char *)&c, 1);
            }
            break;
        }
    }
    SbPut(sb, "\"");
}

static void SbStr(Sb *sb, const char *s) { SbStrN(sb, s ? s : "", s ? (uint32_t)strlen(s) : 0); }

/* ------------------------------------------------------------------------ */
/* The model                                                                */
/* ------------------------------------------------------------------------ */

void SgTiledMap_Init(SgTiledMap *map)
{
    memset(map, 0, sizeof *map);
}

static void PropsFree(SgTiledProps *props)
{
    for (int32_t i = 0; i < props->count; i++) {
        free(props->items[i].name);
        free(props->items[i].type);
        free(props->items[i].value);
    }
    free(props->items);
    props->items = NULL;
    props->count = 0;
}

static void LayersFree(SgTiledMap *map)
{
    for (int32_t i = 0; i < map->extraCount; i++) {
        free(map->extraLayers[i].name);
        free(map->extraLayers[i].gids);
        PropsFree(&map->extraLayers[i].props);
    }
    free(map->extraLayers);
    map->extraLayers = NULL;
    map->extraCount = 0;
}

void SgTiledMap_Free(SgTiledMap *map)
{
    free(map->imageSource);
    free(map->gids);
    free(map->firstLayerName);
    PropsFree(&map->props);
    LayersFree(map);
    SgTiledMap_Init(map);
}

int32_t SgTiled_SetString(char **dst, const char *src)
{
    char *copy = NULL;
    if (src) {
        size_t n = strlen(src) + 1;
        copy = malloc(n);
        if (!copy)
            return -1;
        memcpy(copy, src, n);
    }
    free(*dst);
    *dst = copy;
    return 0;
}

/* width*height, or -1 if either is negative or the product leaves int32 --
 * where C#'s `new int[cells]` would have thrown. */
static int64_t Cells(int32_t width, int32_t height)
{
    int64_t cells;
    if (width < 0 || height < 0)
        return -1;
    cells = (int64_t)width * height;
    return cells > 0x7FFFFFFF ? -1 : cells;
}

static int32_t *NewGids(int64_t cells)
{
    return calloc((size_t)(cells > 0 ? cells : 1), sizeof(int32_t));
}

int32_t SgTiledMap_SetSize(SgTiledMap *map, int32_t width, int32_t height,
                           int32_t tileWidth, int32_t tileHeight, char *err)
{
    int64_t cells = Cells(width, height);
    int32_t *gids;

    if (cells < 0) {
        snprintf(err, 256, "Tiled map: %dx%d is not a valid map size.", (int)width, (int)height);
        return -1;
    }
    gids = NewGids(cells);
    if (!gids) {
        snprintf(err, 256, "Tiled map: out of memory for a %dx%d map.", (int)width, (int)height);
        return -1;
    }
    free(map->gids);
    LayersFree(map);
    map->gids = gids;
    map->width = width;
    map->height = height;
    map->tileWidth = tileWidth;
    map->tileHeight = tileHeight;
    return 0;
}

SgTiledLayer *SgTiledMap_AddLayer(SgTiledMap *map, const char *name, int32_t visible, char *err)
{
    int64_t cells = Cells(map->width, map->height);
    SgTiledLayer *layers, *l;

    if (cells < 0) {
        snprintf(err, 256, "Tiled map: %dx%d is not a valid map size.", (int)map->width, (int)map->height);
        return NULL;
    }
    if (map->extraCount >= 0x10000) {
        snprintf(err, 256, "Tiled map: too many layers.");
        return NULL;
    }
    layers = realloc(map->extraLayers, (size_t)(map->extraCount + 1) * sizeof *layers);
    if (!layers) {
        snprintf(err, 256, "Tiled map: out of memory.");
        return NULL;
    }
    map->extraLayers = layers;
    l = &layers[map->extraCount];
    memset(l, 0, sizeof *l);
    l->visible = visible ? 1 : 0;
    l->gids = NewGids(cells);
    if (!l->gids || SgTiled_SetString(&l->name, name ? name : "") != 0) {
        free(l->gids);
        free(l->name);
        snprintf(err, 256, "Tiled map: out of memory.");
        return NULL;
    }
    map->extraCount++;
    return l;
}

int32_t SgTiledProps_AddRaw(SgTiledProps *props, const char *name, const char *type, const char *jsonValue)
{
    SgTiledProperty *items, *p;

    if (props->count >= 0x10000)
        return -1;
    items = realloc(props->items, (size_t)(props->count + 1) * sizeof *items);
    if (!items)
        return -1;
    props->items = items;
    p = &items[props->count];
    memset(p, 0, sizeof *p);
    if (SgTiled_SetString(&p->name, name ? name : "") != 0 ||
        SgTiled_SetString(&p->type, type ? type : "") != 0 ||
        SgTiled_SetString(&p->value, jsonValue ? jsonValue : "") != 0) {
        free(p->name);
        free(p->type);
        free(p->value);
        return -1;
    }
    props->count++;
    return 0;
}

int32_t SgTiledProps_AddInt(SgTiledProps *props, const char *name, int32_t value)
{
    char t[16];
    snprintf(t, sizeof t, "%d", (int)value);
    return SgTiledProps_AddRaw(props, name, "int", t);
}

/* A string value already escaped, as LayerProperty.Text does with Str(). */
static int32_t AddTextN(SgTiledProps *props, const char *name, const char *value, uint32_t len)
{
    Sb sb = { 0 };
    int32_t rc;
    SbStrN(&sb, value, len);
    if (sb.oom) {
        free(sb.data);
        return -1;
    }
    rc = SgTiledProps_AddRaw(props, name, "string", sb.data);
    free(sb.data);
    return rc;
}

int32_t SgTiledProps_AddText(SgTiledProps *props, const char *name, const char *value)
{
    return AddTextN(props, name, value ? value : "", value ? (uint32_t)strlen(value) : 0);
}

/* ------------------------------------------------------------------------ */
/* Write (JSON)                                                             */
/* ------------------------------------------------------------------------ */

/* One layer object, in Tiled's own key order (alphabetical, which is what its
 * writer produces, so a file we write and a file Tiled writes diff cleanly). */
static void AppendLayer(Sb *sb, const char *name, const int32_t *gids, int32_t visible,
                        const SgTiledProps *props, int32_t id,
                        int32_t width, int32_t height, int32_t last)
{
    int64_t cells = (int64_t)width * height;

    SbPut(sb, "        {\n");

    /* One map row per line - a 42x55 pitch is 2310 GIDs, and Tiled reads it
     * back just the same whether it is wrapped or not. */
    SbPut(sb, "         \"data\":[");
    for (int32_t y = 0; y < height; y++) {
        if (y > 0)
            SbPut(sb, "\n                ");
        for (int32_t x = 0; x < width; x++) {
            int64_t i = (int64_t)y * width + x;
            SbNum(sb, gids[i]);
            if (i < cells - 1)
                SbPut(sb, ", ");
        }
    }
    SbPut(sb, "],\n");

    SbPut(sb, "         \"height\":"); SbNum(sb, height); SbPut(sb, ",\n");
    SbPut(sb, "         \"id\":");     SbNum(sb, id);     SbPut(sb, ",\n");
    SbPut(sb, "         \"name\":");   SbStr(sb, name);   SbPut(sb, ",\n");
    SbPut(sb, "         \"opacity\":1,\n");

    if (props && props->count > 0) {
        SbPut(sb, "         \"properties\":[\n");
        for (int32_t i = 0; i < props->count; i++) {
            const SgTiledProperty *pr = &props->items[i];
            SbPut(sb, "                {\"name\":"); SbStr(sb, pr->name);
            SbPut(sb, ",\"type\":");                 SbStr(sb, pr->type);
            SbPut(sb, ",\"value\":");                SbPut(sb, pr->value ? pr->value : "");
            SbPut(sb, "}");
            SbPut(sb, i == props->count - 1 ? "\n" : ",\n");
        }
        SbPut(sb, "         ],\n");
    }

    SbPut(sb, "         \"type\":\"tilelayer\",\n");
    SbPut(sb, "         \"visible\":"); SbPut(sb, visible ? "true" : "false"); SbPut(sb, ",\n");
    SbPut(sb, "         \"width\":"); SbNum(sb, width); SbPut(sb, ",\n");
    SbPut(sb, "         \"x\":0,\n");
    SbPut(sb, "         \"y\":0\n");
    SbPut(sb, last ? "        }],\n" : "        },\n");
}

int32_t SgTiledMap_WriteMem(const SgTiledMap *map, SgBuf *out, char *err)
{
    int64_t cells = Cells(map->width, map->height);
    Sb sb = { 0 };

    out->data = NULL;
    out->size = 0;

    if (cells < 0) {
        snprintf(err, 256, "Tiled map: %dx%d is not a valid map size.", (int)map->width, (int)map->height);
        return -1;
    }
    if (cells > 0 && !map->gids) {
        snprintf(err, 256, "Expected %lld tile IDs for a %dx%d map, got 0.",
                 (long long)cells, (int)map->width, (int)map->height);
        return -1;
    }
    for (int32_t li = 0; li < map->extraCount; li++)
        if (cells > 0 && !map->extraLayers[li].gids) {
            snprintf(err, 256, "Layer '%s' has 0 tile IDs, expected %lld.",
                     map->extraLayers[li].name ? map->extraLayers[li].name : "", (long long)cells);
            return -1;
        }

    SbPut(&sb, "{ \"compressionlevel\":-1,\n");
    SbPut(&sb, " \"height\":"); SbNum(&sb, map->height); SbPut(&sb, ",\n");
    SbPut(&sb, " \"infinite\":false,\n");
    SbPut(&sb, " \"layers\":[\n");

    /* The map's own layer is first -- the reader and AmigaTools both take the
     * FIRST tile layer, so a multi-layer map still round-trips as the pitch it
     * always was. */
    AppendLayer(&sb, map->firstLayerName ? map->firstLayerName : LAYER_NAME, map->gids, 1,
                &map->props, 1, map->width, map->height, map->extraCount == 0);

    for (int32_t li = 0; li < map->extraCount; li++) {
        const SgTiledLayer *extra = &map->extraLayers[li];
        AppendLayer(&sb, extra->name ? extra->name : "", extra->gids, extra->visible, &extra->props,
                    li + 2, map->width, map->height, li == map->extraCount - 1);
    }

    SbPut(&sb, " \"nextlayerid\":"); SbNum(&sb, map->extraCount + 2); SbPut(&sb, ",\n");
    SbPut(&sb, " \"nextobjectid\":1,\n");
    SbPut(&sb, " \"orientation\":\"orthogonal\",\n");
    SbPut(&sb, " \"renderorder\":\"right-down\",\n");
    SbPut(&sb, " \"tiledversion\":"); SbStr(&sb, TILED_VERSION); SbPut(&sb, ",\n");
    SbPut(&sb, " \"tileheight\":"); SbNum(&sb, map->tileHeight); SbPut(&sb, ",\n");
    SbPut(&sb, " \"tilesets\":[\n");
    SbPut(&sb, "        {\n");
    SbPut(&sb, "         \"columns\":"); SbNum(&sb, map->columns); SbPut(&sb, ",\n");
    SbPut(&sb, "         \"firstgid\":1,\n");
    SbPut(&sb, "         \"image\":"); SbStr(&sb, map->imageSource); SbPut(&sb, ",\n");
    SbPut(&sb, "         \"imageheight\":"); SbNum(&sb, map->imageHeight); SbPut(&sb, ",\n");
    SbPut(&sb, "         \"imagewidth\":"); SbNum(&sb, map->imageWidth); SbPut(&sb, ",\n");
    SbPut(&sb, "         \"margin\":0,\n");
    SbPut(&sb, "         \"name\":"); SbStr(&sb, TILESET_NAME); SbPut(&sb, ",\n");
    SbPut(&sb, "         \"spacing\":0,\n");
    SbPut(&sb, "         \"tilecount\":"); SbNum(&sb, map->tileCount); SbPut(&sb, ",\n");
    SbPut(&sb, "         \"tileheight\":"); SbNum(&sb, map->tileHeight); SbPut(&sb, ",\n");
    SbPut(&sb, "         \"tilewidth\":"); SbNum(&sb, map->tileWidth); SbPut(&sb, "\n");
    SbPut(&sb, "        }],\n");
    SbPut(&sb, " \"tilewidth\":"); SbNum(&sb, map->tileWidth); SbPut(&sb, ",\n");
    SbPut(&sb, " \"type\":\"map\",\n");
    SbPut(&sb, " \"version\":"); SbStr(&sb, FORMAT_VERSION); SbPut(&sb, ",\n");
    SbPut(&sb, " \"width\":"); SbNum(&sb, map->width); SbPut(&sb, "\n");
    SbPut(&sb, "}\n");

    if (sb.oom) {
        free(sb.data);
        snprintf(err, 256, "Tiled map: out of memory writing the map.");
        return -1;
    }
    out->data = (uint8_t *)sb.data;
    out->size = sb.size;
    return 0;
}

int32_t SgTiledMap_Write(const SgTiledMap *map, const char *path, char *err)
{
    SgBuf buf;
    int32_t rc;

    if (SgTiledMap_WriteMem(map, &buf, err) != 0)
        return -1;
    /* UTF-8 without a BOM, as UTF8Encoding(false) writes. */
    rc = Sg_WriteFile(path, buf.data, buf.size, err);
    Sg_BufFree(&buf);
    return rc;
}

/* ------------------------------------------------------------------------ */
/* Read (JSON)                                                              */
/* ------------------------------------------------------------------------ */

static int32_t EqualsIgnoreCase(const char *a, uint32_t alen, const char *b)
{
    uint32_t blen = (uint32_t)strlen(b);
    if (alen != blen)
        return 0;
    for (uint32_t i = 0; i < alen; i++) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
        if (x != y)
            return 0;
    }
    return 1;
}

/* A message that quotes user text: keep it printable and short. */
static void Quote(char *out, uint32_t cap, const char *s, uint32_t len)
{
    uint32_t o = 0;
    for (uint32_t i = 0; i < len && o + 4 < cap; i++)
        out[o++] = ((uint8_t)s[i] < 0x20) ? '?' : s[i];
    if (len > 0 && o + 4 >= cap && cap > 4) {
        out[o++] = '.'; out[o++] = '.'; out[o++] = '.';
    }
    out[o] = 0;
}

static int32_t JInt(const SgJsonDoc *d, const SgJsonNode *obj, const char *name, int32_t *out, char *err)
{
    const SgJsonNode *v = Sg_JsonGet(d, obj, name);
    if (!v || v->kind != SG_JSON_NUMBER) {
        snprintf(err, 256, "Tiled map: missing or non-numeric \"%s\".", name);
        return -1;
    }
    if (Sg_JsonInt32(d, v, out) != 0) {
        snprintf(err, 256, "Tiled map: \"%s\" is not a 32-bit integer.", name);
        return -1;
    }
    return 0;
}

static const char *JOptString(const SgJsonDoc *d, const SgJsonNode *obj, const char *name, uint32_t *len)
{
    const SgJsonNode *v = Sg_JsonGet(d, obj, name);
    return (v && v->kind == SG_JSON_STRING) ? Sg_JsonStr(d, v, len) : NULL;
}

/* A string field into a malloc'd copy (strings with an embedded NUL are cut
 * there: a NUL cannot be in a file name or a layer name anyway). */
static int32_t CopyString(char **dst, const char *s, char *err)
{
    if (SgTiled_SetString(dst, s) != 0) {
        snprintf(err, 256, "Tiled map: out of memory.");
        return -1;
    }
    return 0;
}

/* FirstOfKind: the first entry of root[arrayName] whose "type" matches `type`
 * (case-insensitively), or the first entry when `type` is NULL. */
static const SgJsonNode *FirstOfKind(const SgJsonDoc *d, const SgJsonNode *root, const char *arrayName,
                                     const char *type, const char *what, char *err)
{
    const SgJsonNode *array = Sg_JsonGet(d, root, arrayName);

    if (array && array->kind == SG_JSON_ARRAY) {
        for (const SgJsonNode *e = Sg_JsonFirst(d, array); e; e = Sg_JsonNext(d, e)) {
            const char *t;
            uint32_t tlen;

            if (e->kind != SG_JSON_OBJECT) {
                /* JsonElement.TryGetProperty throws on a non-object */
                snprintf(err, 256, "Tiled map: an entry of \"%s\" is not an object.", arrayName);
                return NULL;
            }
            if (!type)
                return e;
            t = JOptString(d, e, "type", &tlen);
            if (t && EqualsIgnoreCase(t, tlen, type))
                return e;
        }
    }
    snprintf(err, 256, "Tiled map: no %s found in \"%s\".", what, arrayName);
    return NULL;
}

/* A layer's "properties", kept in the {name,type,value} form they were in.
 * Entries that are not that shape are passed over (SwosGfx never read them). */
static int32_t ReadProps(const SgJsonDoc *d, const SgJsonNode *layer, SgTiledProps *props, char *err)
{
    const SgJsonNode *array = Sg_JsonGet(d, layer, "properties");

    if (!array || array->kind != SG_JSON_ARRAY)
        return 0;
    for (const SgJsonNode *e = Sg_JsonFirst(d, array); e; e = Sg_JsonNext(d, e)) {
        const char *name, *type;
        const SgJsonNode *value;
        int32_t rc;

        if (e->kind != SG_JSON_OBJECT)
            continue;
        name = JOptString(d, e, "name", NULL);
        value = Sg_JsonGet(d, e, "value");
        if (!name || !value)
            continue;
        type = JOptString(d, e, "type", NULL);
        if (!type)
            type = "string";            /* Tiled's default property type */

        if (value->kind == SG_JSON_STRING) {
            /* re-escaped as LayerProperty.Text would write it */
            uint32_t len;
            const char *s = Sg_JsonStr(d, value, &len);
            rc = AddTextN(props, name, s, len);
            if (rc == 0) {
                /* AddTextN says "string"; keep the type the file gave */
                SgTiledProperty *p = &props->items[props->count - 1];
                rc = SgTiled_SetString(&p->type, type);
            }
        } else {
            uint32_t len;
            const char *raw = Sg_JsonRaw(d, value, &len);
            char *copy = malloc((size_t)len + 1);
            if (!copy) {
                rc = -1;
            } else {
                memcpy(copy, raw, len);
                copy[len] = 0;
                rc = SgTiledProps_AddRaw(props, name, type, copy);
                free(copy);
            }
        }
        if (rc != 0) {
            snprintf(err, 256, "Tiled map: out of memory.");
            return -1;
        }
    }
    return 0;
}

/* A layer's "data" as `cells` GIDs, flip flags masked. 1 if it is exactly
 * that, 0 if not (with `why` set when asked). */
static int32_t ReadGids(const SgJsonDoc *d, const SgJsonNode *data, int64_t cells, int32_t *gids,
                        char *why)
{
    int64_t i = 0;

    for (const SgJsonNode *cell = Sg_JsonFirst(d, data); cell; cell = Sg_JsonNext(d, cell)) {
        uint32_t v;
        if (i >= cells)
            return 0;               /* the caller checked the count; never write past it */
        if (Sg_JsonUInt32(d, cell, &v) != 0) {
            if (why)
                snprintf(why, 256, "Tiled map: tile ID %lld is not an unsigned 32-bit number.", (long long)i);
            return 0;
        }
        /* GIDs carry the flip flags in the top bits; SWOS has no flipped tiles,
         * but mask them off rather than reading a nonsense tile index. */
        gids[i++] = (int32_t)(v & SG_TILED_GID_MASK);
    }
    return 1;
}

static int32_t ReadJson(SgTiledMap *map, const char *text, uint32_t len, char *err)
{
    SgJsonOptions opt = { 1, 1, 64 };   /* AllowTrailingCommas, CommentHandling.Skip */
    SgJsonDoc d;
    const SgJsonNode *root, *tileset, *layer, *data, *v;
    const char *s;
    uint32_t slen;
    int64_t cells;
    int32_t firstGid = 1;
    char quoted[64];

    if (Sg_JsonParse(&d, text, len, &opt, err) != 0)
        return -1;
    root = Sg_JsonRoot(&d);

    if (root->kind != SG_JSON_OBJECT) {
        snprintf(err, 256, "Tiled map: the document is not a JSON object.");
        goto fail;
    }
    if (JInt(&d, root, "width", &map->width, err) != 0 ||
        JInt(&d, root, "height", &map->height, err) != 0 ||
        JInt(&d, root, "tilewidth", &map->tileWidth, err) != 0 ||
        JInt(&d, root, "tileheight", &map->tileHeight, err) != 0)
        goto fail;

    s = JOptString(&d, root, "orientation", &slen);
    if (s && !EqualsIgnoreCase(s, slen, "orthogonal")) {
        Quote(quoted, sizeof quoted, s, slen);
        snprintf(err, 256, "Tiled map: orientation must be 'orthogonal', not '%s'.", quoted);
        goto fail;
    }

    v = Sg_JsonGet(&d, root, "infinite");
    if (v && v->kind == SG_JSON_TRUE) {
        snprintf(err, 256, "Tiled map: infinite maps are not supported.");
        goto fail;
    }

    /* --- tileset (the first embedded one) --- */
    tileset = FirstOfKind(&d, root, "tilesets", NULL, "tileset", err);
    if (!tileset)
        goto fail;

    if (Sg_JsonGet(&d, tileset, "source")) {
        snprintf(err, 256, "Tiled map: external tilesets (\"source\") are not supported; save the tileset inside the map.");
        goto fail;
    }

    if (JInt(&d, tileset, "tilecount", &map->tileCount, err) != 0 ||
        JInt(&d, tileset, "columns", &map->columns, err) != 0)
        goto fail;
    s = JOptString(&d, tileset, "image", NULL);
    if (!s) {
        snprintf(err, 256, "Tiled map: missing or non-string \"image\".");
        goto fail;
    }
    if (CopyString(&map->imageSource, s, err) != 0)
        goto fail;
    if (JInt(&d, tileset, "imagewidth", &map->imageWidth, err) != 0 ||
        JInt(&d, tileset, "imageheight", &map->imageHeight, err) != 0)
        goto fail;

    v = Sg_JsonGet(&d, tileset, "firstgid");
    if (v && Sg_JsonInt32(&d, v, &firstGid) != 0) {
        snprintf(err, 256, "Tiled map: tileset \"firstgid\" is not a 32-bit integer.");
        goto fail;
    }
    if (firstGid != 1) {
        snprintf(err, 256, "Tiled map: tileset firstgid must be 1, not %d.", (int)firstGid);
        goto fail;
    }

    /* --- layer (the first tile layer) --- */
    layer = FirstOfKind(&d, root, "layers", "tilelayer", "tile layer", err);
    if (!layer)
        goto fail;

    data = Sg_JsonGet(&d, layer, "data");
    if (!data) {
        snprintf(err, 256, "Tiled map: tile layer has no \"data\".");
        goto fail;
    }
    if (data->kind != SG_JSON_ARRAY) {
        snprintf(err, 256, "Tiled map: tile layer \"data\" must be an array of GIDs; re-save with \"Tile Layer Format: CSV\".");
        goto fail;
    }

    cells = Cells(map->width, map->height);
    if (cells < 0) {
        snprintf(err, 256, "Tiled map: %dx%d is not a valid map size.", (int)map->width, (int)map->height);
        goto fail;
    }
    /* Counted before anything is allocated, so a tiny file cannot ask for a
     * huge map. Same messages as the C#'s two checks. */
    if ((int64_t)data->count > cells) {
        snprintf(err, 256, "Tiled map: expected %lld tile IDs, got more.", (long long)cells);
        goto fail;
    }
    if ((int64_t)data->count != cells) {
        snprintf(err, 256, "Tiled map: expected %lld tile IDs, got %u.", (long long)cells, (unsigned)data->count);
        goto fail;
    }
    map->gids = NewGids(cells);
    if (!map->gids) {
        snprintf(err, 256, "Tiled map: out of memory.");
        goto fail;
    }
    if (!ReadGids(&d, data, cells, map->gids, err))
        goto fail;

    /* --- beyond TiledMap.ReadJson: what a rewrite needs to be identical --- */
    s = JOptString(&d, layer, "name", NULL);
    if (s && CopyString(&map->firstLayerName, s, err) != 0)
        goto fail;
    if (ReadProps(&d, layer, &map->props, err) != 0)
        goto fail;

    for (const SgJsonNode *e = Sg_JsonNext(&d, layer); e; e = Sg_JsonNext(&d, e)) {
        const SgJsonNode *edata, *vis;
        const char *t;
        uint32_t tlen;
        SgTiledLayer *l;

        if (e->kind != SG_JSON_OBJECT)
            continue;
        t = JOptString(&d, e, "type", &tlen);
        if (!t || !EqualsIgnoreCase(t, tlen, "tilelayer"))
            continue;                   /* object groups, images: not ours */
        edata = Sg_JsonGet(&d, e, "data");
        if (!edata || edata->kind != SG_JSON_ARRAY || (int64_t)edata->count != cells) {
            map->skippedLayers++;
            continue;
        }
        vis = Sg_JsonGet(&d, e, "visible");
        l = SgTiledMap_AddLayer(map, JOptString(&d, e, "name", NULL),
                                !(vis && vis->kind == SG_JSON_FALSE), err);
        if (!l)
            goto fail;
        if (!ReadGids(&d, edata, cells, l->gids, NULL)) {
            /* not GIDs after all: drop it again */
            map->extraCount--;
            free(l->name);
            free(l->gids);
            map->skippedLayers++;
            continue;
        }
        if (ReadProps(&d, e, &l->props, err) != 0)
            goto fail;
    }

    Sg_JsonFree(&d);
    return 0;

fail:
    Sg_JsonFree(&d);
    return -1;
}

/* ------------------------------------------------------------------------ */
/* Read (the .tmx SwosGfx used to write)                                    */
/* ------------------------------------------------------------------------ */

/*
 * A minimal XML reader: elements, attributes (either quote), character and the
 * five predefined entity references, comments, processing instructions, CDATA,
 * and a DOCTYPE without an internal subset (very old Tiled wrote one; it is
 * skipped -- XDocument would not have fetched it either). That covers every
 * .tmx Tiled or SwosGfx writes. An internal DTD subset, an undefined entity, a
 * mismatched end tag or a duplicate attribute is an error, as it is for
 * XDocument.Parse.
 *
 * XDocument's Element("map") only matches an element in no namespace: one with
 * a prefix, or under a non-empty default xmlns, does not -- kept, so a file the
 * C# refused is refused here too.
 */

#define XML_MAX_DEPTH 256

typedef struct XAttr {
    char *name;
    char *value;
} XAttr;

typedef struct XElem {
    char          *name;
    int32_t        inNamespace;
    XAttr         *attrs;
    int32_t        attrCount;
    struct XElem **kids;
    int32_t        kidCount;
    Sb             value;           /* XElement.Value: all descendant text */
} XElem;

typedef struct {
    const char *s;
    uint32_t    len, pos;
    char       *err;
    int32_t     failed;
} XParser;

static void XFail(XParser *x, const char *what)
{
    uint32_t line = 1, col = 1;
    if (x->failed)
        return;
    x->failed = 1;
    for (uint32_t i = 0; i < x->pos && i < x->len; i++) {
        if (x->s[i] == '\n') { line++; col = 1; }
        else col++;
    }
    snprintf(x->err, 256, "TMX: %s at line %u, column %u.", what, (unsigned)line, (unsigned)col);
}

static void XFree(XElem *e)
{
    if (!e)
        return;
    free(e->name);
    for (int32_t i = 0; i < e->attrCount; i++) {
        free(e->attrs[i].name);
        free(e->attrs[i].value);
    }
    free(e->attrs);
    for (int32_t i = 0; i < e->kidCount; i++)
        XFree(e->kids[i]);
    free(e->kids);
    free(e->value.data);
    free(e);
}

static int32_t XPeek(const XParser *x, uint32_t ofs)
{
    return (x->pos < x->len && ofs < x->len - x->pos) ? (uint8_t)x->s[x->pos + ofs] : -1;
}

static int32_t XStarts(const XParser *x, const char *lit)
{
    uint32_t n = (uint32_t)strlen(lit);
    return x->len - x->pos >= n && memcmp(x->s + x->pos, lit, n) == 0;
}

static int32_t XIsSpace(int32_t c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

static void XSkipSpace(XParser *x)
{
    while (XIsSpace(XPeek(x, 0)))
        x->pos++;
}

static int32_t XIsNameChar(int32_t c, int32_t first)
{
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == ':' || c >= 0x80)
        return 1;
    return !first && ((c >= '0' && c <= '9') || c == '-' || c == '.');
}

static char *XName(XParser *x)
{
    uint32_t start = x->pos;
    char *n;

    if (!XIsNameChar(XPeek(x, 0), 1)) {
        XFail(x, "name expected");
        return NULL;
    }
    while (XIsNameChar(XPeek(x, 0), 0))
        x->pos++;
    n = malloc(x->pos - start + 1);
    if (!n) {
        XFail(x, "out of memory");
        return NULL;
    }
    memcpy(n, x->s + start, x->pos - start);
    n[x->pos - start] = 0;
    return n;
}

/* Skip past `end`; error if it never comes. */
static void XSkipPast(XParser *x, const char *end, const char *what)
{
    uint32_t n = (uint32_t)strlen(end);
    while (x->pos < x->len) {
        if (x->len - x->pos >= n && memcmp(x->s + x->pos, end, n) == 0) {
            x->pos += n;
            return;
        }
        x->pos++;
    }
    XFail(x, what);
}

static void SbUtf8(Sb *sb, uint32_t cp)
{
    char b[4];
    uint32_t n;
    if (cp < 0x80)         { b[0] = (char)cp; n = 1; }
    else if (cp < 0x800)   { b[0] = (char)(0xC0 | (cp >> 6)); b[1] = (char)(0x80 | (cp & 0x3F)); n = 2; }
    else if (cp < 0x10000) { b[0] = (char)(0xE0 | (cp >> 12)); b[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
                             b[2] = (char)(0x80 | (cp & 0x3F)); n = 3; }
    else                   { b[0] = (char)(0xF0 | (cp >> 18)); b[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
                             b[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); b[3] = (char)(0x80 | (cp & 0x3F)); n = 4; }
    SbPutN(sb, b, n);
}

/* An entity reference; x->pos is on the '&'. */
static void XEntity(XParser *x, Sb *out)
{
    static const struct { const char *name; char ch; } kNamed[] = {
        { "lt;", '<' }, { "gt;", '>' }, { "amp;", '&' }, { "quot;", '"' }, { "apos;", '\'' },
    };

    x->pos++;
    if (XPeek(x, 0) == '#') {
        uint32_t cp = 0, digits = 0;
        int32_t hex = 0;
        x->pos++;
        if (XPeek(x, 0) == 'x') {
            hex = 1;
            x->pos++;
        }
        for (;;) {
            int32_t c = XPeek(x, 0), dv;
            if (c == ';')
                break;
            dv = (c >= '0' && c <= '9') ? c - '0'
               : (hex && c >= 'a' && c <= 'f') ? c - 'a' + 10
               : (hex && c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
            if (dv < 0 || cp > 0x10FFFF) {
                XFail(x, "bad character reference");
                return;
            }
            cp = cp * (hex ? 16u : 10u) + (uint32_t)dv;
            digits++;
            x->pos++;
        }
        x->pos++;   /* ';' */
        /* an XML Char: #x9 | #xA | #xD | [#x20-#xD7FF] | [#xE000-#xFFFD] | [#x10000-#x10FFFF] */
        if (digits == 0 || cp > 0x10FFFF || (cp < 0x20 && cp != 9 && cp != 10 && cp != 13) ||
            (cp >= 0xD800 && cp <= 0xDFFF) || cp == 0xFFFE || cp == 0xFFFF) {
            XFail(x, "bad character reference");
            return;
        }
        SbUtf8(out, cp);
        return;
    }
    for (uint32_t i = 0; i < sizeof kNamed / sizeof kNamed[0]; i++)
        if (XStarts(x, kNamed[i].name)) {
            x->pos += (uint32_t)strlen(kNamed[i].name);
            SbPutN(out, &kNamed[i].ch, 1);
            return;
        }
    XFail(x, "undefined entity reference");
}

static void XAttrs(XParser *x, XElem *e, int32_t *defaultNs)
{
    for (;;) {
        uint32_t before = x->pos;
        char *name;
        Sb value = { 0 };
        int32_t quote;
        XAttr *attrs;

        XSkipSpace(x);
        if (XPeek(x, 0) == '/' || XPeek(x, 0) == '>' || x->failed)
            return;
        if (x->pos == before) {
            XFail(x, "whitespace expected between attributes");
            return;
        }
        name = XName(x);
        if (!name)
            return;
        XSkipSpace(x);
        if (XPeek(x, 0) != '=') {
            free(name);
            XFail(x, "'=' expected after an attribute name");
            return;
        }
        x->pos++;
        XSkipSpace(x);
        quote = XPeek(x, 0);
        if (quote != '"' && quote != '\'') {
            free(name);
            XFail(x, "attribute value must be quoted");
            return;
        }
        x->pos++;
        SbPut(&value, "");
        for (;;) {
            int32_t c = XPeek(x, 0);
            if (c < 0) {
                XFail(x, "unterminated attribute value");
                break;
            }
            if (c == quote) {
                x->pos++;
                break;
            }
            if (c == '<') {
                XFail(x, "'<' in an attribute value");
                break;
            }
            if (c == '&') {
                XEntity(x, &value);
                if (x->failed)
                    break;
                continue;
            }
            /* attribute-value normalisation: literal whitespace becomes a space */
            if (c == '\r' && XPeek(x, 1) == '\n')
                x->pos++;
            SbPutN(&value, XIsSpace(c) ? " " : x->s + x->pos, 1);
            x->pos++;
        }
        if (x->failed || value.oom) {
            if (!x->failed)
                XFail(x, "out of memory");
            free(name);
            free(value.data);
            return;
        }
        for (int32_t i = 0; i < e->attrCount; i++)
            if (strcmp(e->attrs[i].name, name) == 0) {
                free(name);
                free(value.data);
                XFail(x, "duplicate attribute");
                return;
            }
        if (strcmp(name, "xmlns") == 0)
            *defaultNs = value.size > 0;
        attrs = realloc(e->attrs, (size_t)(e->attrCount + 1) * sizeof *attrs);
        if (!attrs) {
            free(name);
            free(value.data);
            XFail(x, "out of memory");
            return;
        }
        e->attrs = attrs;
        attrs[e->attrCount].name = name;
        attrs[e->attrCount].value = value.data;
        e->attrCount++;
    }
}

/* Comments, PIs and whitespace (the prolog and what follows the root). */
static void XMisc(XParser *x)
{
    for (;;) {
        XSkipSpace(x);
        if (XStarts(x, "<!--"))
            XSkipPast(x, "-->", "unterminated comment");
        else if (XStarts(x, "<?"))
            XSkipPast(x, "?>", "unterminated processing instruction");
        else
            return;
        if (x->failed)
            return;
    }
}

static XElem *XElement(XParser *x, int32_t depth, int32_t parentNs)
{
    XElem *e;
    int32_t defaultNs = parentNs;

    if (depth >= XML_MAX_DEPTH) {
        XFail(x, "nested too deeply");
        return NULL;
    }
    e = calloc(1, sizeof *e);
    if (!e) {
        XFail(x, "out of memory");
        return NULL;
    }
    x->pos++;                           /* '<' */
    e->name = XName(x);
    if (!e->name)
        goto fail;
    XAttrs(x, e, &defaultNs);
    if (x->failed)
        goto fail;
    e->inNamespace = defaultNs || strchr(e->name, ':') != NULL;
    SbPut(&e->value, "");

    if (XStarts(x, "/>")) {
        x->pos += 2;
        return e;
    }
    if (XPeek(x, 0) != '>') {
        XFail(x, "'>' expected");
        goto fail;
    }
    x->pos++;

    /* content */
    for (;;) {
        int32_t c = XPeek(x, 0);
        if (c < 0) {
            XFail(x, "unterminated element");
            goto fail;
        }
        if (c == '<') {
            if (XStarts(x, "</")) {
                char *end;
                x->pos += 2;
                end = XName(x);
                if (!end)
                    goto fail;
                if (strcmp(end, e->name) != 0) {
                    free(end);
                    XFail(x, "end tag does not match the start tag");
                    goto fail;
                }
                free(end);
                XSkipSpace(x);
                if (XPeek(x, 0) != '>') {
                    XFail(x, "'>' expected");
                    goto fail;
                }
                x->pos++;
                break;
            }
            if (XStarts(x, "<!--")) {
                XSkipPast(x, "-->", "unterminated comment");
            } else if (XStarts(x, "<?")) {
                XSkipPast(x, "?>", "unterminated processing instruction");
            } else if (XStarts(x, "<![CDATA[")) {
                uint32_t start;
                x->pos += 9;
                start = x->pos;
                XSkipPast(x, "]]>", "unterminated CDATA section");
                if (!x->failed)
                    SbPutN(&e->value, x->s + start, x->pos - 3 - start);
            } else if (XStarts(x, "<!")) {
                XFail(x, "unexpected markup");
            } else {
                XElem *kid = XElement(x, depth + 1, defaultNs), **kids;
                if (!kid)
                    goto fail;
                kids = realloc(e->kids, (size_t)(e->kidCount + 1) * sizeof *kids);
                if (!kids) {
                    XFree(kid);
                    XFail(x, "out of memory");
                    goto fail;
                }
                e->kids = kids;
                kids[e->kidCount++] = kid;
                SbPutN(&e->value, kid->value.data, kid->value.size);
            }
            if (x->failed)
                goto fail;
            continue;
        }
        if (c == '&') {
            XEntity(x, &e->value);
            if (x->failed)
                goto fail;
            continue;
        }
        SbPutN(&e->value, x->s + x->pos, 1);
        x->pos++;
    }
    if (e->value.oom) {
        XFail(x, "out of memory");
        goto fail;
    }
    return e;

fail:
    XFree(e);
    return NULL;
}

static XElem *XParse(const char *s, uint32_t len, char *err)
{
    XParser x = { s, len, 0, err, 0 };
    XElem *root;

    XMisc(&x);
    if (!x.failed && XStarts(&x, "<!DOCTYPE")) {
        /* skipped, unless it has an internal subset */
        while (x.pos < x.len && x.s[x.pos] != '>' && x.s[x.pos] != '[')
            x.pos++;
        if (XPeek(&x, 0) != '>') {
            XFail(&x, "a DOCTYPE with an internal subset is not supported");
            return NULL;
        }
        x.pos++;
        XMisc(&x);
    }
    if (x.failed)
        return NULL;
    if (XPeek(&x, 0) != '<' || !XIsNameChar(XPeek(&x, 1), 1)) {
        XFail(&x, "root element expected");
        return NULL;
    }
    root = XElement(&x, 0, 0);
    if (!root)
        return NULL;
    XMisc(&x);
    if (!x.failed && x.pos < x.len)
        XFail(&x, "text after the root element");
    if (x.failed) {
        XFree(root);
        return NULL;
    }
    return root;
}

/* XElement.Element(name): the first child element of that name in no namespace. */
static const XElem *XChild(const XElem *e, const char *name)
{
    for (int32_t i = 0; i < e->kidCount; i++)
        if (!e->kids[i]->inNamespace && strcmp(e->kids[i]->name, name) == 0)
            return e->kids[i];
    return NULL;
}

static const char *XAttrValue(const XElem *e, const char *name)
{
    for (int32_t i = 0; i < e->attrCount; i++)
        if (strcmp(e->attrs[i].name, name) == 0)
            return e->attrs[i].value;
    return NULL;
}

/* Int32.Parse / UInt32.Parse with NumberStyles.Integer (what XmlConvert.ToInt32
 * and uint.Parse use): optional white space either side (U+0009-000D, space),
 * one optional sign, then digits. "-0" is a valid unsigned zero. */
static int32_t NetParseInt(const char *s, uint32_t len, int64_t min, int64_t max, int64_t *out)
{
    uint32_t i = 0;
    int32_t neg = 0, digits = 0;
    int64_t v = 0;

#define NET_WS(c) ((c) == ' ' || ((c) >= 0x09 && (c) <= 0x0D))
    while (i < len && NET_WS(s[i])) i++;
    if (i < len && (s[i] == '-' || s[i] == '+')) {
        neg = s[i] == '-';
        i++;
    }
    while (i < len && s[i] >= '0' && s[i] <= '9') {
        v = v * 10 + (s[i] - '0');
        if (v > max + 1)
            return -1;
        digits++;
        i++;
    }
    while (i < len && NET_WS(s[i])) i++;
#undef NET_WS
    if (!digits || i != len)
        return -1;
    if (neg)
        v = -v;
    if (v < min || v > max)
        return -1;
    *out = v;
    return 0;
}

/* Attr(): (int)element.Attribute(name). */
static int32_t XInt(const XElem *e, const char *name, int32_t *out, char *err)
{
    const char *v = XAttrValue(e, name);
    int64_t n;
    char quoted[64];

    if (!v) {
        snprintf(err, 256, "TMX: <%s> missing '%s' attribute.", e->name, name);
        return -1;
    }
    if (NetParseInt(v, (uint32_t)strlen(v), INT32_MIN, INT32_MAX, &n) != 0) {
        Quote(quoted, sizeof quoted, v, (uint32_t)strlen(v));
        snprintf(err, 256, "TMX: <%s> '%s' attribute '%s' is not a 32-bit integer.", e->name, name, quoted);
        return -1;
    }
    *out = (int32_t)n;
    return 0;
}

static int32_t ReadTmx(SgTiledMap *map, const char *text, uint32_t len, char *err)
{
    XElem *doc = XParse(text, len, err);
    const XElem *mapElem, *tileset, *image, *layer, *data;
    const char *v;
    int64_t cells, count = 0;
    int32_t rc = -1;

    if (!doc)
        return -1;
    /* doc.Element("map"): the root, when it is <map> */
    mapElem = (!doc->inNamespace && strcmp(doc->name, "map") == 0) ? doc : NULL;
    if (!mapElem) {
        snprintf(err, 256, "TMX: missing <map> root element.");
        goto done;
    }
    if (XInt(mapElem, "width", &map->width, err) != 0 ||
        XInt(mapElem, "height", &map->height, err) != 0 ||
        XInt(mapElem, "tilewidth", &map->tileWidth, err) != 0 ||
        XInt(mapElem, "tileheight", &map->tileHeight, err) != 0)
        goto done;

    tileset = XChild(mapElem, "tileset");
    if (!tileset) {
        snprintf(err, 256, "TMX: missing <tileset> element.");
        goto done;
    }
    if (XInt(tileset, "tilecount", &map->tileCount, err) != 0 ||
        XInt(tileset, "columns", &map->columns, err) != 0)
        goto done;

    image = XChild(tileset, "image");
    if (!image) {
        snprintf(err, 256, "TMX: missing <image> inside <tileset>.");
        goto done;
    }
    v = XAttrValue(image, "source");
    if (!v) {
        snprintf(err, 256, "TMX: <image> missing 'source' attribute.");
        goto done;
    }
    if (CopyString(&map->imageSource, v, err) != 0)
        goto done;
    if (XInt(image, "width", &map->imageWidth, err) != 0 ||
        XInt(image, "height", &map->imageHeight, err) != 0)
        goto done;

    layer = XChild(mapElem, "layer");
    if (!layer) {
        snprintf(err, 256, "TMX: missing <layer> element.");
        goto done;
    }
    data = XChild(layer, "data");
    if (!data) {
        snprintf(err, 256, "TMX: missing <data> inside <layer>.");
        goto done;
    }
    v = XAttrValue(data, "encoding");
    if (!v || !EqualsIgnoreCase(v, (uint32_t)strlen(v), "csv")) {
        snprintf(err, 256, "TMX <data> encoding must be 'csv'.");
        goto done;
    }

    /* Split on , space CR LF TAB with empty entries removed, as the C# does:
     * count first, then parse, so the size is checked before allocating. */
#define TMX_SEP(c) ((c) == ',' || (c) == ' ' || (c) == '\r' || (c) == '\n' || (c) == '\t')
    {
        const char *s = data->value.data;
        uint32_t n = data->value.size;

        for (uint32_t i = 0; i < n;) {
            while (i < n && TMX_SEP(s[i])) i++;
            if (i < n) count++;
            while (i < n && !TMX_SEP(s[i])) i++;
        }

        /* cells is the C#'s `int cells = Width * Height` -- unchecked, so a
         * product past int32 wraps, and the count can then never match */
        cells = (int64_t)(int32_t)((uint32_t)map->width * (uint32_t)map->height);
        if (count != cells) {
            snprintf(err, 256, "TMX: expected %lld tile IDs, got %lld.", (long long)cells, (long long)count);
            goto done;
        }
        map->gids = NewGids(cells);
        if (!map->gids) {
            snprintf(err, 256, "Tiled map: out of memory.");
            goto done;
        }
        count = 0;
        for (uint32_t i = 0; i < n;) {
            uint32_t start;
            int64_t g;
            while (i < n && TMX_SEP(s[i])) i++;
            if (i >= n)
                break;
            start = i;
            while (i < n && !TMX_SEP(s[i])) i++;
            if (NetParseInt(s + start, i - start, 0, 0xFFFFFFFFu, &g) != 0) {
                char quoted[64];
                Quote(quoted, sizeof quoted, s + start, i - start);
                snprintf(err, 256, "TMX: tile ID '%s' is not an unsigned 32-bit number.", quoted);
                goto done;
            }
            map->gids[count++] = (int32_t)((uint32_t)g & SG_TILED_GID_MASK);
        }
    }
#undef TMX_SEP
    rc = 0;

done:
    XFree(doc);
    return rc;
}

/* ------------------------------------------------------------------------ */
/* Read                                                                     */
/* ------------------------------------------------------------------------ */

static int32_t ReadAny(SgTiledMap *map, const uint8_t *data, uint32_t size, const char *name, char *err)
{
    const char *text = (const char *)data;
    uint32_t i;
    int32_t rc;

    SgTiledMap_Init(map);

    /* File.ReadAllText drops a UTF-8 BOM. A UTF-16/32 one it would decode; that
     * is not something Tiled or SwosGfx writes. */
    if (size >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
        text += 3;
        size -= 3;
    } else if (size >= 2 && ((data[0] == 0xFF && data[1] == 0xFE) || (data[0] == 0xFE && data[1] == 0xFF))) {
        snprintf(err, 256, "Tiled map '%s' is UTF-16; save it as UTF-8.", name);
        return -1;
    }

    /* The format from the content, not the extension. */
    for (i = 0; i < size; i++) {
        uint8_t c = (uint8_t)text[i];
        if (c == ' ' || (c >= 0x09 && c <= 0x0D))
            continue;
        break;
    }
    if (i == size) {
        snprintf(err, 256, "Tiled map '%s' is empty.", name);
        return -1;
    }
    rc = (text[i] == '<') ? ReadTmx(map, text, size, err) : ReadJson(map, text, size, err);
    if (rc != 0)
        SgTiledMap_Free(map);
    return rc;
}

int32_t SgTiledMap_ReadMem(SgTiledMap *map, const uint8_t *data, uint32_t size, char *err)
{
    return ReadAny(map, data, size, "(memory)", err);
}

int32_t SgTiledMap_Read(SgTiledMap *map, const char *path, char *err)
{
    SgBuf buf;
    const char *name = path, *slash;
    int32_t rc;

    if (Sg_ReadFile(path, &buf, err) != 0) {
        SgTiledMap_Init(map);
        return -1;
    }
    slash = strrchr(path, '/');
    if (slash) name = slash + 1;
    slash = strrchr(name, '\\');
    if (slash) name = slash + 1;
    rc = ReadAny(map, buf.data, buf.size, name, err);
    Sg_BufFree(&buf);
    return rc;
}
