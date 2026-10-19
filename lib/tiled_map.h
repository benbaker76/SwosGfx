/*
 * tiled_map.h -- Tiled map files: the JSON map format (.tmj) SwosGfx writes, and
 * the XML .tmx it used to write (read only).
 *
 *   https://doc.mapeditor.org/en/stable/reference/json-map-format/
 *
 * Ported from SwosGfx TiledMap.cs. SWOS only needs the simplest shape Tiled
 * supports: an orthogonal map of tile layers all the size of the map, ONE
 * embedded tileset backed by ONE external image, and tile data as a plain array
 * of GIDs. The writer emits Tiled's own key order and SwosGfx's exact bytes
 * (indentation, one map row per line, "\n" line endings, no BOM), so a file
 * written here is byte for byte the file SwosGfx writes for the same map.
 *
 * The tileset image is an 8bpp BMP beside the map (`image` is its file name,
 * relative to the map's folder): read and write it with SgImage (sg_image.h).
 * This module never opens it.
 *
 * Who uses it (SwosGfx's two callers):
 *
 *   AmigaTools.ConvertPitchMapToTiled  (-amiga -map -output=tmj): one layer,
 *     42x55 cells of 16x16 tiles, gid = tile index + 1, 16 tileset columns.
 *     Fill an SgTiledMap with SgTiledMap_Init, set the fields, SgTiledMap_Write.
 *   AmigaTools.ConvertTiledToPitchMap  (-amiga -tmj -output=map): SgTiledMap_Read,
 *     then check tileWidth/height == 16, width/height == 42x55, and use gids
 *     (first tile layer only), tileCount, columns, imageSource/Width/Height.
 *   DosPitch.SavePitchAsTiled          (-dos -output=tmj): the same, plus -- when
 *     the pitch has a crowd -- firstLayerName "pitch", two properties on the first
 *     layer (SgTiledProps_AddText "swos:role" "pitch", SgTiledProps_AddInt
 *     "swos:z" -100) and two extra layers from SgTiledMap_AddLayer ("crowd-sit"
 *     visible, "crowd-stand" hidden, each with its own swos:role / swos:z).
 *
 * Ownership: an SgTiledMap owns every pointer in it (malloc'd); SgTiledMap_Free
 * releases them all. Set strings with SgTiled_SetString, never by pointing at a
 * literal.
 *
 * Reading, compared with TiledMap.Read:
 *   - The format is sniffed from the content (first non-whitespace byte '<' =
 *     TMX), not the extension, as SwosGfx does. A UTF-8 BOM is skipped.
 *   - Every check TiledMap.ReadJson / ReadTmx makes is made here, in the same
 *     order, with the same messages: orthogonal only, not infinite, an embedded
 *     tileset (no "source") with firstgid 1, a tile layer whose "data" is an array
 *     of exactly width*height GIDs. GIDs have the flip flags (top 4 bits) masked.
 *   - JSON is read with trailing commas and comments allowed, as SwosGfx asked
 *     System.Text.Json to (sg_json.h).
 *   - Beyond SwosGfx, which kept only the first tile layer's GIDs: the first tile
 *     layer's name and properties, and every LATER tile layer (name, visibility,
 *     properties, GIDs), are read too, so a map SwosGfx wrote reads back and
 *     writes out byte-identical -- the DOS crowd layers included. A later layer
 *     that is not a plain GID array of the map's size (base64, chunked) is
 *     skipped and counted in skippedLayers, never an error: SwosGfx ignored it
 *     too. TMX reads only what ReadTmx read.
 */
#ifndef SWOSGFX_TILED_MAP_H
#define SWOSGFX_TILED_MAP_H

#include <stdint.h>
#include "sg_util.h"

#define SG_TILED_EXTENSION     ".tmj"         /* TiledMap.Extension */
#define SG_TILED_GID_MASK      0x0FFFFFFFu    /* GID without Tiled's flip flags */

/* One custom property, in Tiled's own {name,type,value} shape. */
typedef struct {
    char *name;
    char *type;     /* "int" | "string" | "bool" | "float" | ... */
    char *value;    /* ALREADY IN JSON FORM: -100, "pitch" (quotes and escapes
                       included), true. Written as it is. */
} SgTiledProperty;

typedef struct {
    SgTiledProperty *items;
    int32_t          count;
} SgTiledProps;

/* An additional tile layer: same dimensions as the map. */
typedef struct {
    char        *name;
    int32_t     *gids;      /* width*height, row-major, 1-based (0 = no tile) */
    int32_t      visible;   /* 1 / 0 */
    SgTiledProps props;
} SgTiledLayer;

/* A single-tileset orthogonal map: everything SwosGfx reads or writes. */
typedef struct {
    int32_t  width, height;          /* in cells */
    int32_t  tileWidth, tileHeight;  /* in pixels */

    int32_t  tileCount;              /* tiles in the tileset */
    int32_t  columns;                /* tileset columns */

    char    *imageSource;            /* tileset image file name, relative to the map */
    int32_t  imageWidth, imageHeight;

    int32_t *gids;                   /* first layer: width*height, row-major, 1-based */

    char        *firstLayerName;     /* NULL = Tiled's default, "Tile Layer 1" */
    SgTiledProps props;              /* custom properties of the FIRST layer */

    SgTiledLayer *extraLayers;       /* written after the first, in order */
    int32_t       extraCount;

    int32_t  skippedLayers;          /* read only: later tile layers not understood */
} SgTiledMap;

/* An empty map (all zero, no allocations). */
void    SgTiledMap_Init(SgTiledMap *map);
void    SgTiledMap_Free(SgTiledMap *map);

/* Set the size and allocate gids (all 0): width*height cells of tw x th pixels.
 * Frees any gids / extra layers already there. 0 on success. */
int32_t SgTiledMap_SetSize(SgTiledMap *map, int32_t width, int32_t height,
                           int32_t tileWidth, int32_t tileHeight, char *err);

/* Append an extra layer with gids allocated (all 0) at the map's size; NULL on
 * failure (out of memory). The pointer is valid until the next AddLayer. */
SgTiledLayer *SgTiledMap_AddLayer(SgTiledMap *map, const char *name, int32_t visible, char *err);

/* Replace *dst with a malloc'd copy of src (NULL clears). 0 on success. */
int32_t SgTiled_SetString(char **dst, const char *src);

/* LayerProperty.Int / LayerProperty.Text. 0 on success. */
int32_t SgTiledProps_AddInt(SgTiledProps *props, const char *name, int32_t value);
int32_t SgTiledProps_AddText(SgTiledProps *props, const char *name, const char *value);
/* Any type, the value given already in JSON form. */
int32_t SgTiledProps_AddRaw(SgTiledProps *props, const char *name, const char *type, const char *jsonValue);

/* TiledMap.Write: the map as JSON. To memory (out->data malloc'd) or to a file
 * (its folders created). Fails if a size is negative or overflows. 0 on success. */
int32_t SgTiledMap_WriteMem(const SgTiledMap *map, SgBuf *out, char *err);
int32_t SgTiledMap_Write(const SgTiledMap *map, const char *path, char *err);

/* TiledMap.Read: JSON or TMX, from memory or a file. `map` is overwritten
 * without being freed (pass a fresh or freed one); on success SgTiledMap_Free it
 * when done, on failure it is left empty. 0 on success. The first layer's
 * "visible" is not kept: the writer always writes it visible, as SwosGfx did. */
int32_t SgTiledMap_ReadMem(SgTiledMap *map, const uint8_t *data, uint32_t size, char *err);
int32_t SgTiledMap_Read(SgTiledMap *map, const char *path, char *err);

#endif
