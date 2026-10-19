/*
 * tiled_map_test.c -- the JSON reader (sg_json) and Tiled maps (tiled_map).
 *
 *   tiled_map_test            edge cases, a write/read round trip, a fuzz pass
 *   tiled_map_test DIR...     also: every .tmj under each DIR (recursively) is
 *                             read and written back, and must come out
 *                             byte-identical -- run it over maps SwosGfx wrote
 *
 * Exit status 0 when everything passed.
 */
#include "sg_json.h"
#include "tiled_map.h"
#include "sg_util.h"

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

#define CHECK(cond, ...) do {                                   \
        gChecks++;                                              \
        if (!(cond)) {                                          \
            gFailures++;                                        \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);         \
            printf(__VA_ARGS__);                                \
            printf("\n");                                       \
        }                                                       \
    } while (0)

/* ------------------------------------------------------------------------ */
/* JSON                                                                     */
/* ------------------------------------------------------------------------ */

static int32_t Parses(const char *s, uint32_t len, int32_t relaxed, char *err)
{
    SgJsonOptions opt = { relaxed, relaxed, 0 };
    SgJsonDoc d;
    int32_t rc = Sg_JsonParse(&d, s, len, &opt, err);
    Sg_JsonFree(&d);
    return rc == 0;
}

static void TestJsonGrammar(void)
{
    static const struct { const char *text; int32_t strict, relaxed; } kCases[] = {
        /* text                              strict relaxed */
        { "{}",                                  1, 1 },
        { "[]",                                  1, 1 },
        { " \t\r\n[1] \n",                       1, 1 },
        { "0",                                   1, 1 },
        { "-0",                                  1, 1 },
        { "-12.5e+3",                            1, 1 },
        { "1E-2",                                1, 1 },
        { "\"\"",                                1, 1 },
        { "true",                                1, 1 },
        { "null",                                1, 1 },
        { "{\"a\":[1,{\"b\":null}],\"c\":false}", 1, 1 },
        { "",                                    0, 0 },
        { "   ",                                 0, 0 },
        { "{",                                   0, 0 },
        { "}",                                   0, 0 },
        { "[1,]",                                0, 1 },
        { "{\"a\":1,}",                          0, 1 },
        { "[1,,2]",                              0, 0 },
        { "[,]",                                 0, 0 },
        { "{,}",                                 0, 0 },
        { "[1 2]",                               0, 0 },
        { "{\"a\"}",                             0, 0 },
        { "{\"a\" 1}",                           0, 0 },
        { "{a:1}",                               0, 0 },
        { "{'a':1}",                             0, 0 },
        { "{\"a\":}",                            0, 0 },
        { "01",                                  0, 0 },
        { "-",                                   0, 0 },
        { "+1",                                  0, 0 },
        { "1.",                                  0, 0 },
        { ".5",                                  0, 0 },
        { "1e",                                  0, 0 },
        { "1e+",                                 0, 0 },
        { "0x10",                                0, 0 },
        { "NaN",                                 0, 0 },
        { "Infinity",                            0, 0 },
        { "tru",                                 0, 0 },
        { "nulll",                               0, 0 },
        { "\"abc",                               0, 0 },
        { "\"\\x\"",                             0, 0 },
        { "\"\\u12\"",                           0, 0 },
        { "\"\\u12G4\"",                         0, 0 },
        { "\"\\ud83d\"",                         0, 0 },   /* high surrogate alone */
        { "\"\\ude00\"",                         0, 0 },   /* low surrogate alone */
        { "\"\\ud83d\\u0041\"",                  0, 0 },   /* high + not-low */
        { "\"tab\there\"",                       0, 0 },   /* raw control char */
        { "\"\xC3\"",                            0, 0 },   /* truncated UTF-8 */
        { "\"\xC0\xAF\"",                        0, 0 },   /* overlong */
        { "\"\xED\xA0\x80\"",                    0, 0 },   /* UTF-8 surrogate */
        { "\"\xF4\x90\x80\x80\"",                0, 0 },   /* past U+10FFFF */
        { "\"\xFF\"",                            0, 0 },
        { "{} x",                                0, 0 },
        { "{}{}",                                0, 0 },
        { "// c\n{}",                            0, 1 },
        { "{} // c",                             0, 1 },
        { "/* c */ [1, /* in */ 2] /**/",        0, 1 },
        { "[1 /* open",                          0, 0 },
        { "/",                                   0, 0 },
        { "[1] /",                               0, 0 },
        { "/* */",                               0, 0 },   /* comment, no value */
    };

    char err[256];
    for (uint32_t i = 0; i < sizeof kCases / sizeof kCases[0]; i++) {
        const char *t = kCases[i].text;
        int32_t s = Parses(t, (uint32_t)strlen(t), 0, err);
        int32_t r = Parses(t, (uint32_t)strlen(t), 1, err);
        CHECK(s == kCases[i].strict, "strict parse of [%s]: got %d", t, (int)s);
        CHECK(r == kCases[i].relaxed, "relaxed parse of [%s]: got %d", t, (int)r);
    }

    /* a NUL byte is not whitespace */
    CHECK(!Parses("[1]\0", 4, 1, err), "trailing NUL accepted");
    CHECK(!Parses("[\0]", 3, 1, err), "NUL inside an array accepted");
    /* reading stops at len: no NUL terminator needed, nothing read past it */
    {
        char *exact = malloc(3);
        memcpy(exact, "[1]", 3);
        CHECK(Parses(exact, 3, 0, err), "unterminated buffer: %s", err);
        CHECK(!Parses(exact, 2, 0, err), "truncated buffer accepted");
        free(exact);
    }
    /* the error names where */
    Parses("{\n  \"a\": tru }", 14, 0, err);
    CHECK(strstr(err, "line 2") != NULL, "error has no line: %s", err);
}

static void TestJsonDepth(void)
{
    char err[256];
    uint32_t n = 1000000;
    char *s = malloc(n * 2);

    for (uint32_t depth = 63; depth <= 65; depth++) {
        for (uint32_t i = 0; i < depth; i++) { s[i] = '['; s[depth + i] = ']'; }
        CHECK(Parses(s, depth * 2, 0, err) == (depth <= 64), "depth %u", (unsigned)depth);
    }
    for (uint32_t i = 0; i < n; i++) { s[i] = '{'; }
    CHECK(!Parses(s, n, 1, err), "a million '{' accepted");
    for (uint32_t i = 0; i < n; i++) { s[i] = '['; s[n + i] = ']'; }
    CHECK(!Parses(s, n * 2, 1, err), "a million nested arrays accepted");
    free(s);
}

static void TestJsonValues(void)
{
    static const struct { const char *text; int32_t okI; int32_t i32; int32_t okU; uint32_t u32; } kNums[] = {
        { "0",           1, 0,           1, 0 },
        { "-0",          1, 0,           1, 0 },
        { "42",          1, 42,          1, 42 },
        { "-1",          1, -1,          0, 0 },
        { "2147483647",  1, 2147483647,  1, 2147483647u },
        { "2147483648",  0, 0,           1, 2147483648u },
        { "-2147483648", 1, (-2147483647 - 1), 0, 0 },
        { "-2147483649", 0, 0,           0, 0 },
        { "4294967295",  0, 0,           1, 4294967295u },
        { "4294967296",  0, 0,           0, 0 },
        { "99999999999999999999999999999999", 0, 0, 0, 0 },
        { "1.0",         0, 0,           0, 0 },
        { "1e2",         0, 0,           0, 0 },
        { "1e999999",    0, 0,           0, 0 },
    };
    char err[256];

    for (uint32_t i = 0; i < sizeof kNums / sizeof kNums[0]; i++) {
        SgJsonDoc d;
        int32_t iv = 0;
        uint32_t uv = 0;
        int32_t rc = Sg_JsonParse(&d, kNums[i].text, (uint32_t)strlen(kNums[i].text), NULL, err);
        CHECK(rc == 0, "number %s did not parse: %s", kNums[i].text, err);
        if (rc != 0)
            continue;
        CHECK((Sg_JsonInt32(&d, Sg_JsonRoot(&d), &iv) == 0) == kNums[i].okI && (!kNums[i].okI || iv == kNums[i].i32),
              "Int32(%s)", kNums[i].text);
        CHECK((Sg_JsonUInt32(&d, Sg_JsonRoot(&d), &uv) == 0) == kNums[i].okU && (!kNums[i].okU || uv == kNums[i].u32),
              "UInt32(%s)", kNums[i].text);
        Sg_JsonFree(&d);
    }

    /* escapes decode to UTF-8 */
    {
        const char *t = "[\"a\\\"b\\\\c\\/d\\b\\f\\n\\r\\t\", \"\\u00e9\\u20AC\", \"\\ud83d\\ude00\", \"x\\u0000y\", \"\xC3\xA9\"]";
        SgJsonDoc d;
        const SgJsonNode *n;
        const char *s;
        uint32_t len;

        CHECK(Sg_JsonParse(&d, t, (uint32_t)strlen(t), NULL, err) == 0, "escapes: %s", err);
        n = Sg_JsonFirst(&d, Sg_JsonRoot(&d));
        s = Sg_JsonStr(&d, n, &len);
        CHECK(len == 12 && memcmp(s, "a\"b\\c/d\b\f\n\r\t", 12) == 0, "short escapes");
        n = Sg_JsonNext(&d, n);
        s = Sg_JsonStr(&d, n, &len);
        CHECK(len == 5 && memcmp(s, "\xC3\xA9\xE2\x82\xAC", 5) == 0, "\\u BMP");
        n = Sg_JsonNext(&d, n);
        s = Sg_JsonStr(&d, n, &len);
        CHECK(len == 4 && memcmp(s, "\xF0\x9F\x98\x80", 4) == 0, "\\u surrogate pair");
        n = Sg_JsonNext(&d, n);
        s = Sg_JsonStr(&d, n, &len);
        CHECK(len == 3 && memcmp(s, "x\0y", 3) == 0, "\\u0000");
        n = Sg_JsonNext(&d, n);
        s = Sg_JsonStr(&d, n, &len);
        CHECK(len == 2 && memcmp(s, "\xC3\xA9", 2) == 0, "raw UTF-8");
        CHECK(Sg_JsonNext(&d, n) == NULL, "array length");
        CHECK(Sg_JsonRoot(&d)->count == 5, "array count");
        Sg_JsonFree(&d);
    }

    /* objects: last duplicate wins, escaped keys match, kinds */
    {
        const char *t = "{\"a\":1, \"a\":2, \"k\\u0065y\":\"v\", \"t\":true, \"n\":null, \"o\":{}}";
        SgJsonDoc d;
        const SgJsonNode *root;
        int32_t v = 0;

        CHECK(Sg_JsonParse(&d, t, (uint32_t)strlen(t), NULL, err) == 0, "object: %s", err);
        root = Sg_JsonRoot(&d);
        CHECK(Sg_JsonInt32(&d, Sg_JsonGet(&d, root, "a"), &v) == 0 && v == 2, "duplicate key: last wins");
        CHECK(Sg_JsonStr(&d, Sg_JsonGet(&d, root, "key"), NULL) &&
              strcmp(Sg_JsonStr(&d, Sg_JsonGet(&d, root, "key"), NULL), "v") == 0, "escaped key");
        CHECK(Sg_JsonGet(&d, root, "t")->kind == SG_JSON_TRUE, "true");
        CHECK(Sg_JsonGet(&d, root, "n")->kind == SG_JSON_NULL, "null");
        CHECK(Sg_JsonGet(&d, root, "o")->kind == SG_JSON_OBJECT, "object");
        CHECK(Sg_JsonGet(&d, root, "missing") == NULL, "missing key");
        CHECK(Sg_JsonGet(&d, Sg_JsonGet(&d, root, "t"), "x") == NULL, "Get on a non-object");
        CHECK(Sg_JsonInt32(&d, Sg_JsonGet(&d, root, "key"), &v) != 0, "Int32 of a string");
        Sg_JsonFree(&d);
    }
}

/* ------------------------------------------------------------------------ */
/* Tiled                                                                    */
/* ------------------------------------------------------------------------ */

static int32_t WriteReadWrite(const SgTiledMap *map, SgBuf *first, SgTiledMap *back, char *err)
{
    SgBuf second;
    int32_t same;

    if (SgTiledMap_WriteMem(map, first, err) != 0)
        return -1;
    if (SgTiledMap_ReadMem(back, first->data, first->size, err) != 0)
        return -1;
    if (SgTiledMap_WriteMem(back, &second, err) != 0)
        return -1;
    same = second.size == first->size && memcmp(second.data, first->data, first->size) == 0;
    Sg_BufFree(&second);
    if (!same) {
        snprintf(err, 256, "rewrite differs");
        return -1;
    }
    return 0;
}

static void TestTiledRoundTrip(void)
{
    char err[256];
    SgTiledMap map, back;
    SgTiledLayer *l;
    SgBuf out;

    /* the DOS pitch shape: named first layer, properties, two crowd layers */
    SgTiledMap_Init(&map);
    CHECK(SgTiledMap_SetSize(&map, 3, 2, 16, 16, err) == 0, "SetSize: %s", err);
    map.tileCount = 7;
    map.columns = 16;
    map.imageWidth = 256;
    map.imageHeight = 16;
    SgTiled_SetString(&map.imageSource, "pi\"tch\\1\t\x01.bmp");
    SgTiled_SetString(&map.firstLayerName, "pitch");
    for (int32_t i = 0; i < 6; i++)
        map.gids[i] = i;
    SgTiledProps_AddText(&map.props, "swos:role", "pitch");
    SgTiledProps_AddInt(&map.props, "swos:z", -100);
    l = SgTiledMap_AddLayer(&map, "crowd-sit", 1, err);
    CHECK(l != NULL, "AddLayer: %s", err);
    l->gids[0] = 7;
    SgTiledProps_AddText(&l->props, "swos:role", "crowd-sit");
    SgTiledProps_AddInt(&l->props, "swos:z", -90);
    l = SgTiledMap_AddLayer(&map, "crowd-stand", 0, err);
    l->gids[5] = 2;
    SgTiledProps_AddText(&l->props, "swos:role", "crowd-stand");
    SgTiledProps_AddInt(&l->props, "swos:z", -90);

    CHECK(WriteReadWrite(&map, &out, &back, err) == 0, "round trip: %s", err);
    CHECK(back.width == 3 && back.height == 2 && back.tileWidth == 16 && back.tileHeight == 16 &&
          back.tileCount == 7 && back.columns == 16 && back.imageWidth == 256 && back.imageHeight == 16,
          "numbers after the round trip");
    CHECK(back.imageSource && strcmp(back.imageSource, map.imageSource) == 0, "image name with escapes");
    CHECK(back.extraCount == 2 && back.extraLayers[1].visible == 0 && back.extraLayers[1].gids[5] == 2,
          "extra layers");
    CHECK(back.props.count == 2 && strcmp(back.props.items[1].value, "-100") == 0, "properties");
    CHECK(strstr((const char *)out.data, "\"image\":\"pi\\\"tch\\\\1\\t\\u0001.bmp\"") != NULL,
          "Str() escaping: %s", (const char *)out.data);
    /* the exact shape of a layer's data: one row per line, ", " after every
     * GID but the last, so rows end with a trailing space */
    CHECK(strstr((const char *)out.data, "\"data\":[0, 1, 2, \n                3, 4, 5],\n") != NULL,
          "data layout");
    Sg_BufFree(&out);
    SgTiledMap_Free(&back);

    /* the Amiga shape: one unnamed layer, no properties */
    SgTiledMap_Free(&map);
    SgTiledMap_Init(&map);
    SgTiledMap_SetSize(&map, 42, 55, 16, 16, err);
    for (int32_t i = 0; i < 42 * 55; i++)
        map.gids[i] = 1 + i % 284;
    map.tileCount = 284;
    map.columns = 16;
    map.imageWidth = 256;
    map.imageHeight = 288;
    SgTiled_SetString(&map.imageSource, "SWCPICH1.bmp");
    CHECK(WriteReadWrite(&map, &out, &back, err) == 0, "round trip 42x55: %s", err);
    CHECK(back.firstLayerName && strcmp(back.firstLayerName, "Tile Layer 1") == 0, "default layer name");
    CHECK(back.extraCount == 0 && back.props.count == 0, "no extras");
    CHECK(out.size > 0 && out.data[out.size - 1] == '\n' && memchr(out.data, '\r', out.size) == NULL,
          "LF line endings");
    Sg_BufFree(&out);
    SgTiledMap_Free(&back);
    SgTiledMap_Free(&map);

    /* an empty map writes and reads */
    SgTiledMap_Init(&map);
    CHECK(WriteReadWrite(&map, &out, &back, err) == 0, "round trip 0x0: %s", err);
    Sg_BufFree(&out);
    SgTiledMap_Free(&back);

    /* bad sizes are refused, not written */
    map.width = -1;
    CHECK(SgTiledMap_WriteMem(&map, &out, err) != 0, "negative width written");
    CHECK(SgTiledMap_SetSize(&map, 65536, 65536, 16, 16, err) != 0, "2^32 cells accepted");
    SgTiledMap_Free(&map);
}

/* A small valid map with `field` replaced; tests one check at a time. */
#define MAP_HEAD "{\"width\":2,\"height\":1,\"tilewidth\":16,\"tileheight\":16,"
#define MAP_TS   "\"tilesets\":[{\"columns\":16,\"firstgid\":1,\"image\":\"t.bmp\",\"imagewidth\":256,\"imageheight\":16,\"tilecount\":3}],"
#define MAP_LY   "\"layers\":[{\"type\":\"tilelayer\",\"data\":[1,2]}]"

static void TestTiledRead(void)
{
    static const struct { const char *text; int32_t ok; const char *msg; } kCases[] = {
        { MAP_HEAD MAP_TS MAP_LY "}",                                         1, NULL },
        { "\xEF\xBB\xBF" MAP_HEAD MAP_TS MAP_LY "}",                          1, NULL },
        { MAP_HEAD MAP_TS MAP_LY ", /* trailing */ }",                        1, NULL },
        { MAP_HEAD MAP_TS MAP_LY ", }",                                       1, NULL },
        { "// Tiled\n" MAP_HEAD MAP_TS MAP_LY "}",                            1, NULL },
        { MAP_HEAD "\"orientation\":\"ORTHOGONAL\"," MAP_TS MAP_LY "}",       1, NULL },
        { MAP_HEAD "\"orientation\":\"isometric\"," MAP_TS MAP_LY "}",        0, "orientation" },
        { MAP_HEAD "\"infinite\":true," MAP_TS MAP_LY "}",                    0, "infinite" },
        { MAP_HEAD "\"infinite\":1," MAP_TS MAP_LY "}",                       1, NULL },
        { "{\"height\":1,\"tilewidth\":16,\"tileheight\":16," MAP_TS MAP_LY "}", 0, "\"width\"" },
        { "{\"width\":\"2\",\"height\":1,\"tilewidth\":16,\"tileheight\":16," MAP_TS MAP_LY "}", 0, "\"width\"" },
        { "{\"width\":2.0,\"height\":1,\"tilewidth\":16,\"tileheight\":16," MAP_TS MAP_LY "}", 0, "\"width\"" },
        { "{\"width\":99999999999,\"height\":1,\"tilewidth\":16,\"tileheight\":16," MAP_TS MAP_LY "}", 0, "\"width\"" },
        { "{\"width\":-2,\"height\":1,\"tilewidth\":16,\"tileheight\":16," MAP_TS MAP_LY "}", 0, "size" },
        { "{\"width\":65536,\"height\":65536,\"tilewidth\":16,\"tileheight\":16," MAP_TS MAP_LY "}", 0, "size" },
        { "{\"width\":2000000,\"height\":1000,\"tilewidth\":16,\"tileheight\":16," MAP_TS MAP_LY "}", 0, "expected 2000000000" },
        { MAP_HEAD MAP_LY "}",                                                0, "tileset" },
        { MAP_HEAD "\"tilesets\":[]," MAP_LY "}",                             0, "tileset" },
        { MAP_HEAD "\"tilesets\":[1]," MAP_LY "}",                            0, "not an object" },
        { MAP_HEAD "\"tilesets\":[{\"source\":\"t.tsj\",\"firstgid\":1}]," MAP_LY "}", 0, "external" },
        { MAP_HEAD "\"tilesets\":[{\"columns\":16,\"firstgid\":2,\"image\":\"t.bmp\",\"imagewidth\":256,\"imageheight\":16,\"tilecount\":3}]," MAP_LY "}", 0, "firstgid" },
        { MAP_HEAD "\"tilesets\":[{\"columns\":16,\"image\":\"t.bmp\",\"imagewidth\":256,\"imageheight\":16,\"tilecount\":3}]," MAP_LY "}", 1, NULL },
        { MAP_HEAD "\"tilesets\":[{\"columns\":16,\"image\":5,\"imagewidth\":256,\"imageheight\":16,\"tilecount\":3}]," MAP_LY "}", 0, "\"image\"" },
        { MAP_HEAD MAP_TS "}",                                                0, "tile layer" },
        { MAP_HEAD MAP_TS "\"layers\":[{\"type\":\"objectgroup\"}]}",         0, "tile layer" },
        { MAP_HEAD MAP_TS "\"layers\":[{\"type\":\"objectgroup\"},{\"type\":\"TileLayer\",\"data\":[1,2]}]}", 1, NULL },
        { MAP_HEAD MAP_TS "\"layers\":[\"x\"]}",                              0, "not an object" },
        { MAP_HEAD MAP_TS "\"layers\":[{\"type\":\"tilelayer\"}]}",           0, "no \"data\"" },
        { MAP_HEAD MAP_TS "\"layers\":[{\"type\":\"tilelayer\",\"data\":\"AQAAAA==\",\"encoding\":\"base64\"}]}", 0, "CSV" },
        { MAP_HEAD MAP_TS "\"layers\":[{\"type\":\"tilelayer\",\"data\":[1]}]}",     0, "got 1" },
        { MAP_HEAD MAP_TS "\"layers\":[{\"type\":\"tilelayer\",\"data\":[1,2,3]}]}", 0, "got more" },
        { MAP_HEAD MAP_TS "\"layers\":[{\"type\":\"tilelayer\",\"data\":[1,-2]}]}",  0, "unsigned" },
        { MAP_HEAD MAP_TS "\"layers\":[{\"type\":\"tilelayer\",\"data\":[1,\"2\"]}]}", 0, "unsigned" },
        { MAP_HEAD MAP_TS "\"layers\":[{\"type\":\"tilelayer\",\"data\":[1,4294967296]}]}", 0, "unsigned" },
        /* a later layer the reader does not understand is skipped, not an error */
        { MAP_HEAD MAP_TS "\"layers\":[{\"type\":\"tilelayer\",\"data\":[1,2]},{\"type\":\"tilelayer\",\"data\":\"AQ==\"}]}", 1, NULL },
        { "[]",                                                               0, "object" },
        { "",                                                                 0, "empty" },
        { " \n\t ",                                                           0, "empty" },
        { "\xFF\xFE{",                                                        0, "UTF-16" },
        { MAP_HEAD MAP_TS MAP_LY,                                             0, "JSON" },
    };
    char err[256];

    for (uint32_t i = 0; i < sizeof kCases / sizeof kCases[0]; i++) {
        SgTiledMap map;
        const char *t = kCases[i].text;
        int32_t rc;
        err[0] = 0;
        rc = SgTiledMap_ReadMem(&map, (const uint8_t *)t, (uint32_t)strlen(t), err);
        CHECK((rc == 0) == kCases[i].ok, "case %u [%.60s...]: rc %d, %s", (unsigned)i, t, (int)rc, err);
        if (rc != 0 && kCases[i].msg)
            CHECK(strstr(err, kCases[i].msg) != NULL, "case %u: message '%s' lacks '%s'", (unsigned)i, err, kCases[i].msg);
        SgTiledMap_Free(&map);
    }

    /* flip flags are masked off; the skipped layer is counted */
    {
        const char *t = MAP_HEAD MAP_TS "\"layers\":[{\"type\":\"tilelayer\",\"data\":[2147483649,3221225474]},"
                                        "{\"type\":\"tilelayer\",\"data\":[1]}]}";
        SgTiledMap map;
        CHECK(SgTiledMap_ReadMem(&map, (const uint8_t *)t, (uint32_t)strlen(t), err) == 0, "flip flags: %s", err);
        CHECK(map.gids && map.gids[0] == 1 && map.gids[1] == 2, "flip flags not masked");
        CHECK(map.skippedLayers == 1 && map.extraCount == 0, "skipped layer not counted");
        SgTiledMap_Free(&map);
    }
}

/* The .tmx SwosGfx wrote before the JSON switch (AmigaTools, first commit). */
static const char kTmx[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<map version=\"1.10\" tiledversion=\"1.10.2\" orientation=\"orthogonal\" renderorder=\"right-down\" "
    "width=\"3\" height=\"2\" tilewidth=\"16\" tileheight=\"16\" infinite=\"0\" nextlayerid=\"2\" nextobjectid=\"1\">\n"
    " <tileset firstgid=\"1\" name=\"tiles\" tilewidth=\"16\" tileheight=\"16\" tilecount=\"284\" columns=\"16\">\n"
    "  <image source=\"SWCPICH1.bmp\" width=\"256\" height=\"288\"/>\n"
    " </tileset>\n"
    " <layer id=\"1\" name=\"Tile Layer 1\" width=\"3\" height=\"2\">\n"
    "  <data encoding=\"csv\">\n"
    "1,2,3,\n"
    "4,5,2147483654\n"
    "  </data>\n"
    " </layer>\n"
    "</map>\n";

static void TestTmx(void)
{
    static const struct { const char *text; int32_t ok; const char *msg; } kCases[] = {
        { "<map width='1' height='1' tilewidth='16' tileheight='16'><tileset tilecount='1' columns='1'>"
          "<image source='a&amp;b&#x41;&#66;.bmp' width='16' height='16'/></tileset>"
          "<layer><data encoding='CSV'><![CDATA[ 7 ]]></data></layer></map>",           1, NULL },
        { "<!DOCTYPE map SYSTEM \"http://mapeditor.org/dtd/1.0/map.dtd\">\n"
          "<map width=' 1 ' height='+1' tilewidth='16' tileheight='16'><!-- c --><tileset tilecount='1' columns='1'>"
          "<image source='a.bmp' width='16' height='16'/></tileset>"
          "<layer><data encoding='csv'>-0</data></layer></map>",                         1, NULL },
        { "<!DOCTYPE map [ <!ENTITY x 'y'> ]><map/>",                                    0, "DOCTYPE" },
        { "<tmx/>",                                                                       0, "<map> root" },
        { "<map xmlns='urn:x' width='1'/>",                                               0, "<map> root" },
        { "<map height='1' tilewidth='16' tileheight='16'/>",                             0, "'width'" },
        { "<map width='x' height='1' tilewidth='16' tileheight='16'/>",                   0, "integer" },
        { "<map width='1' height='1' tilewidth='16' tileheight='16'></map>",              0, "<tileset>" },
        { "<map width='1' height='1' tilewidth='16' tileheight='16'><tileset tilecount='1' columns='1'/></map>", 0, "<image>" },
        { "<map width='1' height='1' tilewidth='16' tileheight='16'><tileset tilecount='1' columns='1'><image width='1' height='1'/></tileset></map>", 0, "'source'" },
        { "<map width='1' height='1' tilewidth='16' tileheight='16'><tileset tilecount='1' columns='1'><image source='a' width='1' height='1'/></tileset></map>", 0, "<layer>" },
        { "<map width='1' height='1' tilewidth='16' tileheight='16'><tileset tilecount='1' columns='1'><image source='a' width='1' height='1'/></tileset><layer/></map>", 0, "<data>" },
        { "<map width='1' height='1' tilewidth='16' tileheight='16'><tileset tilecount='1' columns='1'><image source='a' width='1' height='1'/></tileset><layer><data encoding='base64'>AQ==</data></layer></map>", 0, "csv" },
        { "<map width='2' height='1' tilewidth='16' tileheight='16'><tileset tilecount='1' columns='1'><image source='a' width='1' height='1'/></tileset><layer><data encoding='csv'>1</data></layer></map>", 0, "expected 2" },
        { "<map width='1' height='1' tilewidth='16' tileheight='16'><tileset tilecount='1' columns='1'><image source='a' width='1' height='1'/></tileset><layer><data encoding='csv'>-1</data></layer></map>", 0, "unsigned" },
        { "<map width='1' width='2'/>",                                                   0, "duplicate" },
        { "<map><a></b></map>",                                                           0, "end tag" },
        { "<map>",                                                                        0, "unterminated" },
        { "<map a='&nope;'/>",                                                            0, "entity" },
        { "<map a='&#0;'/>",                                                              0, "character reference" },
        { "<map a='&#99999999999;'/>",                                                    0, "character reference" },
        { "<map a='<'/>",                                                                 0, "'<'" },
        { "<map a=1/>",                                                                   0, "quoted" },
        { "<map/><map/>",                                                                 0, "after the root" },
        { "<map/> trailing",                                                              0, "after the root" },
        { "<!-- open",                                                                    0, "comment" },
        { "<",                                                                            0, "root element" },
    };
    char err[256];
    SgTiledMap map;

    CHECK(SgTiledMap_ReadMem(&map, (const uint8_t *)kTmx, (uint32_t)strlen(kTmx), err) == 0, "TMX: %s", err);
    CHECK(map.width == 3 && map.height == 2 && map.tileWidth == 16 && map.tileHeight == 16 &&
          map.tileCount == 284 && map.columns == 16 && map.imageWidth == 256 && map.imageHeight == 288 &&
          map.imageSource && strcmp(map.imageSource, "SWCPICH1.bmp") == 0, "TMX fields");
    CHECK(map.gids && map.gids[0] == 1 && map.gids[4] == 5 && map.gids[5] == 6, "TMX gids (flags masked)");
    SgTiledMap_Free(&map);

    for (uint32_t i = 0; i < sizeof kCases / sizeof kCases[0]; i++) {
        const char *t = kCases[i].text;
        int32_t rc;
        err[0] = 0;
        rc = SgTiledMap_ReadMem(&map, (const uint8_t *)t, (uint32_t)strlen(t), err);
        CHECK((rc == 0) == kCases[i].ok, "TMX case %u [%.50s...]: rc %d, %s", (unsigned)i, t, (int)rc, err);
        if (rc != 0 && kCases[i].msg)
            CHECK(strstr(err, kCases[i].msg) != NULL, "TMX case %u: message '%s' lacks '%s'", (unsigned)i, err, kCases[i].msg);
        if (rc == 0 && i == 0)
            CHECK(strcmp(map.imageSource, "a&bAB.bmp") == 0 && map.gids[0] == 7, "TMX entities / CDATA");
        SgTiledMap_Free(&map);
    }
}

/* Random damage to valid files: every result must be a clean success or a
 * clean error (run under ASan/UBSan to make that mean something). */
static uint32_t gRng = 12345;
static uint32_t Rand(void)
{
    gRng ^= gRng << 13;
    gRng ^= gRng >> 17;
    gRng ^= gRng << 5;
    return gRng;
}

static void Fuzz(const uint8_t *data, uint32_t size, int32_t rounds)
{
    static const char kBytes[] = "{}[],:\"\\-0123456789eE.tnfu/*< >&;=' \n\x80\xC3\xFF";
    uint8_t *buf = malloc(size + 64);
    char err[256];

    for (int32_t r = 0; r < rounds; r++) {
        uint32_t n = size, edits = 1 + Rand() % 4;
        SgTiledMap map;
        memcpy(buf, data, size);
        for (uint32_t e = 0; e < edits && n > 0; e++) {
            uint32_t at = Rand() % n;
            switch (Rand() % 4) {
            case 0: buf[at] = (uint8_t)kBytes[Rand() % (sizeof kBytes - 1)]; break;
            case 1: buf[at] = (uint8_t)Rand(); break;
            case 2: n = at; break;                                      /* truncate */
            default:                                                    /* delete one */
                memmove(buf + at, buf + at + 1, n - at - 1);
                n--;
                break;
            }
        }
        if (SgTiledMap_ReadMem(&map, buf, n, err) == 0) {
            SgBuf out;
            if (SgTiledMap_WriteMem(&map, &out, err) == 0)
                Sg_BufFree(&out);
        }
        SgTiledMap_Free(&map);
    }
    free(buf);
}

static void TestFuzz(void)
{
    char err[256];
    SgTiledMap map;
    SgBuf out;
    SgTiledLayer *l;

    SgTiledMap_Init(&map);
    SgTiledMap_SetSize(&map, 4, 3, 16, 16, err);
    SgTiled_SetString(&map.imageSource, "t.bmp");
    SgTiledProps_AddText(&map.props, "swos:role", "pitch");
    l = SgTiledMap_AddLayer(&map, "crowd", 0, err);
    SgTiledProps_AddInt(&l->props, "swos:z", -90);
    SgTiledMap_WriteMem(&map, &out, err);
    Fuzz(out.data, out.size, 20000);
    Sg_BufFree(&out);
    SgTiledMap_Free(&map);
    Fuzz((const uint8_t *)kTmx, (uint32_t)strlen(kTmx), 20000);
}

/* ------------------------------------------------------------------------ */
/* Every .tmj under a directory: read, write, same bytes                    */
/* ------------------------------------------------------------------------ */

static int32_t gFiles, gIdentical;

static int32_t EndsWithTmj(const char *name)
{
    size_t n = strlen(name);
    return n > 4 && (name[n - 4] == '.') &&
           (name[n - 3] | 0x20) == 't' && (name[n - 2] | 0x20) == 'm' && (name[n - 1] | 0x20) == 'j';
}

static void RoundTripFile(const char *path)
{
    char err[256];
    SgBuf in, out;
    SgTiledMap map;

    gFiles++;
    if (Sg_ReadFile(path, &in, err) != 0) {
        CHECK(0, "%s: %s", path, err);
        return;
    }
    if (SgTiledMap_ReadMem(&map, in.data, in.size, err) != 0) {
        CHECK(0, "%s: %s", path, err);
        Sg_BufFree(&in);
        return;
    }
    if (SgTiledMap_WriteMem(&map, &out, err) != 0) {
        CHECK(0, "%s: write: %s", path, err);
    } else {
        int32_t same = out.size == in.size && memcmp(out.data, in.data, in.size) == 0;
        CHECK(same, "%s: rewrite differs (%u vs %u bytes)", path, (unsigned)out.size, (unsigned)in.size);
        gIdentical += same;
        /* and damaged copies of a real map fail cleanly */
        Fuzz(in.data, in.size, 200);
        Sg_BufFree(&out);
    }
    SgTiledMap_Free(&map);
    Sg_BufFree(&in);
}

static void WalkDir(const char *dir)
{
#ifdef _WIN32
    char pattern[1024];
    WIN32_FIND_DATAA fd;
    HANDLE h;

    Sg_PathJoin(pattern, sizeof pattern, dir, "*");
    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return;
    do {
        char path[1024];
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
            continue;
        Sg_PathJoin(path, sizeof path, dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            WalkDir(path);
        else if (EndsWithTmj(fd.cFileName))
            RoundTripFile(path);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(dir);
    struct dirent *e;

    if (!d) {
        CHECK(0, "cannot open %s", dir);
        return;
    }
    while ((e = readdir(d)) != NULL) {
        char path[1024];
        struct stat st;
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;
        Sg_PathJoin(path, sizeof path, dir, e->d_name);
        if (stat(path, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode))
            WalkDir(path);
        else if (EndsWithTmj(e->d_name))
            RoundTripFile(path);
    }
    closedir(d);
#endif
}

int main(int argc, char **argv)
{
    TestJsonGrammar();
    TestJsonDepth();
    TestJsonValues();
    TestTiledRoundTrip();
    TestTiledRead();
    TestTmx();
    TestFuzz();

    for (int i = 1; i < argc; i++)
        WalkDir(argv[i]);
    if (argc > 1)
        printf("%d .tmj files, %d rewritten byte-identical\n", (int)gFiles, (int)gIdentical);

    printf("%d checks, %d failed\n", (int)gChecks, (int)gFailures);
    return gFailures ? 1 : 0;
}
