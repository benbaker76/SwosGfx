/*
 * sg_json.h -- a small, strict JSON reader.
 *
 * Enough JSON for the files swosgfx reads (Tiled maps): objects, arrays,
 * numbers, strings with every escape, true/false/null. The input comes from
 * users, so everything is bounds-checked and a malformed document fails with a
 * message in `err`; it never reads past the buffer, and nesting is limited (no
 * recursion without a bound).
 *
 * The grammar is RFC 8259's, with the two relaxations SwosGfx asked
 * System.Text.Json for when it read a map (JsonDocumentOptions
 * AllowTrailingCommas + CommentHandling.Skip), each switchable in SgJsonOptions:
 *
 *   - a trailing comma before ']' or '}':   [1, 2,]   {"a":1,}
 *   - comments wherever whitespace may go:  // to the end of the line, and
 *     block comments; an unterminated block comment is an error.
 *
 * Like System.Text.Json, nesting deeper than 64 arrays/objects is refused, as
 * is anything after the root value except whitespace (and comments).
 *
 * Strings are decoded to UTF-8 (escapes resolved, NUL-terminated). Raw
 * control characters inside a string, invalid UTF-8, a bad escape and a \u
 * surrogate that is not half of a pair are errors. (.NET reads a file with
 * File.ReadAllText, which would turn invalid UTF-8 into U+FFFD instead of
 * failing; refusing is the safer reading.)
 *
 * Numbers are kept as their literal text, so any length parses; Sg_JsonInt32 /
 * Sg_JsonUInt32 then convert the way JsonElement.GetInt32 / GetUInt32 do: an
 * integer literal (no fraction, no exponent) inside the type's range, or fail.
 *
 * Objects keep their members in file order. Sg_JsonGet returns the LAST member
 * of a repeated name, which is what JsonElement.TryGetProperty matches.
 */
#ifndef SWOSGFX_JSON_H
#define SWOSGFX_JSON_H

#include <stdint.h>

typedef enum {
    SG_JSON_NULL = 0,
    SG_JSON_FALSE,
    SG_JSON_TRUE,
    SG_JSON_NUMBER,
    SG_JSON_STRING,
    SG_JSON_ARRAY,
    SG_JSON_OBJECT
} SgJsonKind;

/* One value. Nodes live in SgJsonDoc.nodes and refer to each other by index
 * (-1 = none), so a document is two allocations however large it is. */
typedef struct {
    int32_t  kind;          /* SgJsonKind */
    int32_t  first;         /* array/object: first element / member value */
    int32_t  next;          /* next element of the parent array/object */
    uint32_t count;         /* array/object: number of elements / members */
    uint32_t key, keyLen;   /* object member: its name, offset into doc->text */
    uint32_t str, strLen;   /* string: decoded UTF-8; number: the literal */
    uint32_t srcOfs, srcLen;/* where the value is in the source */
} SgJsonNode;

typedef struct {
    SgJsonNode *nodes;      /* nodes[0] is the root */
    uint32_t    nodeCount, nodeCap;
    char       *text;       /* decoded strings, keys and number literals,
                               each NUL-terminated */
    uint32_t    textSize, textCap;
    const char *src;        /* the parsed text: the CALLER's buffer, used only by
                               Sg_JsonRaw, so keep it alive for that */
    uint32_t    srcLen;
} SgJsonDoc;

typedef struct {
    int32_t allowTrailingCommas;
    int32_t allowComments;
    int32_t maxDepth;       /* <= 0: 64, System.Text.Json's default */
} SgJsonOptions;

/* Parse `len` bytes of `src` (need not be NUL-terminated; a NUL byte is simply
 * invalid JSON). opt may be NULL: strict RFC 8259, depth 64. 0 on success;
 * on failure `err` (256 bytes) says what and at which line/column, and the doc
 * is left empty (Sg_JsonFree is still safe). */
int32_t Sg_JsonParse(SgJsonDoc *doc, const char *src, uint32_t len,
                     const SgJsonOptions *opt, char *err);
void    Sg_JsonFree(SgJsonDoc *doc);

const SgJsonNode *Sg_JsonRoot(const SgJsonDoc *doc);

/* The value of member `key` of an object (the last one if repeated), or NULL
 * when there is none or `obj` is not an object. */
const SgJsonNode *Sg_JsonGet(const SgJsonDoc *doc, const SgJsonNode *obj, const char *key);

/* Walk an array's elements or an object's member values:
 *   for (n = Sg_JsonFirst(d, a); n; n = Sg_JsonNext(d, n)) ...
 * Sg_JsonKey gives a member's name. */
const SgJsonNode *Sg_JsonFirst(const SgJsonDoc *doc, const SgJsonNode *n);
const SgJsonNode *Sg_JsonNext(const SgJsonDoc *doc, const SgJsonNode *n);
const char       *Sg_JsonKey(const SgJsonDoc *doc, const SgJsonNode *member, uint32_t *len);

/* A string's decoded UTF-8 (NUL-terminated; len may be NULL), or NULL if `n` is
 * not a string. A string may hold NUL bytes (\u0000); len counts them. */
const char *Sg_JsonStr(const SgJsonDoc *doc, const SgJsonNode *n, uint32_t *len);

/* A number as int32 / uint32, as GetInt32 / GetUInt32 would read it. 0 when it
 * is a number literal that converts, -1 otherwise (not a number, a fraction
 * or exponent, out of range). */
int32_t Sg_JsonInt32(const SgJsonDoc *doc, const SgJsonNode *n, int32_t *out);
int32_t Sg_JsonUInt32(const SgJsonDoc *doc, const SgJsonNode *n, uint32_t *out);

/* The value's source text, exactly as it appeared (doc->src must still be alive). */
const char *Sg_JsonRaw(const SgJsonDoc *doc, const SgJsonNode *n, uint32_t *len);

#endif
