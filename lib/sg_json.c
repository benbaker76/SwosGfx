/*
 * sg_json.c -- a small, strict JSON reader (see sg_json.h).
 *
 * Recursive descent, one function per production of RFC 8259. Recursion is
 * bounded by the depth limit, which is checked before descending, so a file of
 * a million '[' fails with a message instead of overflowing the stack. Every
 * read goes through peek()/at(), which know where the buffer ends.
 */
#include "sg_json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_MAX_DEPTH 64     /* JsonDocumentOptions.MaxDepth = 0 means 64 */
#define HARD_MAX_DEPTH    1024   /* whatever a caller asks for: bounds the C stack */

typedef struct {
    SgJsonDoc  *doc;
    const char *src;
    uint32_t    len;
    uint32_t    pos;
    int32_t     trailingCommas;
    int32_t     comments;
    int32_t     maxDepth;
    char       *err;
    int32_t     failed;
} Parser;

/* ------------------------------------------------------------------------ */

static void Fail(Parser *p, uint32_t at, const char *what)
{
    uint32_t line = 1, col = 1;

    if (p->failed)
        return;          /* keep the first, innermost message */
    p->failed = 1;
    if (at > p->len)
        at = p->len;
    for (uint32_t i = 0; i < at; i++) {
        if (p->src[i] == '\n') { line++; col = 1; }
        else col++;
    }
    snprintf(p->err, 256, "JSON: %s at line %u, column %u", what, (unsigned)line, (unsigned)col);
}

static int32_t AtEnd(const Parser *p) { return p->pos >= p->len; }
static int32_t Peek(const Parser *p) { return p->pos < p->len ? (uint8_t)p->src[p->pos] : -1; }
static int32_t PeekAt(const Parser *p, uint32_t ofs)
{
    return (p->pos < p->len && ofs < p->len - p->pos) ? (uint8_t)p->src[p->pos + ofs] : -1;
}

static int32_t NewNode(Parser *p, int32_t kind)
{
    SgJsonDoc *d = p->doc;
    SgJsonNode *n;

    if (d->nodeCount == d->nodeCap) {
        uint32_t cap = d->nodeCap ? d->nodeCap * 2 : 64;
        SgJsonNode *nn;
        if (cap < d->nodeCap || cap > 0x7FFFFFFFu / sizeof *nn) {
            Fail(p, p->pos, "document too large");
            return -1;
        }
        nn = realloc(d->nodes, (size_t)cap * sizeof *nn);
        if (!nn) {
            Fail(p, p->pos, "out of memory");
            return -1;
        }
        d->nodes = nn;
        d->nodeCap = cap;
    }
    n = &d->nodes[d->nodeCount];
    memset(n, 0, sizeof *n);
    n->kind = kind;
    n->first = -1;
    n->next = -1;
    n->srcOfs = p->pos;
    return (int32_t)d->nodeCount++;
}

static int32_t TextReserve(Parser *p, uint32_t extra)
{
    SgJsonDoc *d = p->doc;
    uint64_t need = (uint64_t)d->textSize + extra;

    if (need > 0x7FFFFFFFu) {
        Fail(p, p->pos, "document too large");
        return -1;
    }
    if (need > d->textCap) {
        uint64_t cap = d->textCap ? d->textCap : 256;
        char *nt;
        while (cap < need)
            cap *= 2;
        if (cap > 0x7FFFFFFFu)
            cap = 0x7FFFFFFFu;
        nt = realloc(d->text, (size_t)cap);
        if (!nt) {
            Fail(p, p->pos, "out of memory");
            return -1;
        }
        d->text = nt;
        d->textCap = (uint32_t)cap;
    }
    return 0;
}

static int32_t TextPut(Parser *p, const void *bytes, uint32_t n)
{
    if (TextReserve(p, n) != 0)
        return -1;
    memcpy(p->doc->text + p->doc->textSize, bytes, n);
    p->doc->textSize += n;
    return 0;
}

/* Whitespace, and comments when allowed (CommentHandling.Skip). */
static void SkipSpace(Parser *p)
{
    for (;;) {
        int32_t c = Peek(p);
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            p->pos++;
            continue;
        }
        if (c == '/' && p->comments) {
            uint32_t start = p->pos;
            int32_t c2 = PeekAt(p, 1);
            if (c2 == '/') {
                p->pos += 2;
                while (!AtEnd(p) && Peek(p) != '\n' && Peek(p) != '\r')
                    p->pos++;
                continue;
            }
            if (c2 == '*') {
                p->pos += 2;
                for (;;) {
                    if (AtEnd(p)) {
                        Fail(p, start, "unterminated comment");
                        return;
                    }
                    if (Peek(p) == '*' && PeekAt(p, 1) == '/') {
                        p->pos += 2;
                        break;
                    }
                    p->pos++;
                }
                continue;
            }
            Fail(p, start, "unexpected '/'");
            return;
        }
        return;
    }
}

static int32_t HexDigit(int32_t c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Four hex digits after "\u"; p->pos is on the first. -1 on a bad digit. */
static int32_t Hex4(Parser *p)
{
    int32_t v = 0;
    for (uint32_t i = 0; i < 4; i++) {
        int32_t h = HexDigit(PeekAt(p, i));
        if (h < 0) {
            Fail(p, p->pos, "bad \\u escape (four hex digits expected)");
            return -1;
        }
        v = (v << 4) | h;
    }
    p->pos += 4;
    return v;
}

static int32_t PutUtf8(Parser *p, uint32_t cp)
{
    uint8_t b[4];
    uint32_t n;

    if (cp < 0x80)        { b[0] = (uint8_t)cp; n = 1; }
    else if (cp < 0x800)  { b[0] = (uint8_t)(0xC0 | (cp >> 6));  b[1] = (uint8_t)(0x80 | (cp & 0x3F)); n = 2; }
    else if (cp < 0x10000){ b[0] = (uint8_t)(0xE0 | (cp >> 12)); b[1] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
                            b[2] = (uint8_t)(0x80 | (cp & 0x3F)); n = 3; }
    else                  { b[0] = (uint8_t)(0xF0 | (cp >> 18)); b[1] = (uint8_t)(0x80 | ((cp >> 12) & 0x3F));
                            b[2] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F)); b[3] = (uint8_t)(0x80 | (cp & 0x3F)); n = 4; }
    return TextPut(p, b, n);
}

/* Length of the valid UTF-8 sequence starting at p->pos (lead byte >= 0x80),
 * or 0 if it is not one: no overlongs, no surrogates, nothing past U+10FFFF. */
static uint32_t Utf8SeqLen(const Parser *p)
{
    int32_t c0 = Peek(p), c1 = PeekAt(p, 1), c2, c3;

    if (c0 >= 0xC2 && c0 <= 0xDF)
        return (c1 >= 0x80 && c1 <= 0xBF) ? 2 : 0;
    if (c0 >= 0xE0 && c0 <= 0xEF) {
        int32_t lo = (c0 == 0xE0) ? 0xA0 : 0x80;
        int32_t hi = (c0 == 0xED) ? 0x9F : 0xBF;
        c2 = PeekAt(p, 2);
        return (c1 >= lo && c1 <= hi && c2 >= 0x80 && c2 <= 0xBF) ? 3 : 0;
    }
    if (c0 >= 0xF0 && c0 <= 0xF4) {
        int32_t lo = (c0 == 0xF0) ? 0x90 : 0x80;
        int32_t hi = (c0 == 0xF4) ? 0x8F : 0xBF;
        c2 = PeekAt(p, 2);
        c3 = PeekAt(p, 3);
        return (c1 >= lo && c1 <= hi && c2 >= 0x80 && c2 <= 0xBF && c3 >= 0x80 && c3 <= 0xBF) ? 4 : 0;
    }
    return 0;
}

/* A string; p->pos is on the opening quote. Decoded into doc->text, NUL
 * appended; *ofs / *len say where. */
static int32_t ParseString(Parser *p, uint32_t *ofs, uint32_t *len)
{
    uint32_t start = p->pos;
    uint32_t begin;

    p->pos++;                       /* '"' */
    begin = p->doc->textSize;

    for (;;) {
        int32_t c = Peek(p);

        if (c < 0) {
            Fail(p, start, "unterminated string");
            return -1;
        }
        if (c == '"') {
            p->pos++;
            break;
        }
        if (c < 0x20) {
            Fail(p, p->pos, "control character in string (must be escaped)");
            return -1;
        }
        if (c == '\\') {
            int32_t e = PeekAt(p, 1);
            char ch;
            p->pos += 2;
            switch (e) {
            case '"':  ch = '"';  break;
            case '\\': ch = '\\'; break;
            case '/':  ch = '/';  break;
            case 'b':  ch = '\b'; break;
            case 'f':  ch = '\f'; break;
            case 'n':  ch = '\n'; break;
            case 'r':  ch = '\r'; break;
            case 't':  ch = '\t'; break;
            case 'u': {
                int32_t u = Hex4(p), lo;
                uint32_t cp;
                if (u < 0)
                    return -1;
                if (u >= 0xDC00 && u <= 0xDFFF) {
                    Fail(p, p->pos - 6, "\\u escape is a lone low surrogate");
                    return -1;
                }
                if (u >= 0xD800 && u <= 0xDBFF) {
                    if (Peek(p) != '\\' || PeekAt(p, 1) != 'u') {
                        Fail(p, p->pos - 6, "\\u escape is a high surrogate without its low half");
                        return -1;
                    }
                    p->pos += 2;
                    lo = Hex4(p);
                    if (lo < 0)
                        return -1;
                    if (lo < 0xDC00 || lo > 0xDFFF) {
                        Fail(p, p->pos - 6, "\\u escape is a high surrogate without its low half");
                        return -1;
                    }
                    cp = 0x10000u + (((uint32_t)u - 0xD800u) << 10) + ((uint32_t)lo - 0xDC00u);
                } else {
                    cp = (uint32_t)u;
                }
                if (PutUtf8(p, cp) != 0)
                    return -1;
                continue;
            }
            default:
                Fail(p, p->pos - 2, "bad escape in string");
                return -1;
            }
            if (TextPut(p, &ch, 1) != 0)
                return -1;
            continue;
        }
        if (c >= 0x80) {
            uint32_t n = Utf8SeqLen(p);
            if (n == 0) {
                Fail(p, p->pos, "invalid UTF-8 in string");
                return -1;
            }
            if (TextPut(p, p->src + p->pos, n) != 0)
                return -1;
            p->pos += n;
            continue;
        }
        /* a run of plain ASCII in one copy */
        {
            uint32_t run = p->pos;
            while (run < p->len) {
                uint8_t b = (uint8_t)p->src[run];
                if (b == '"' || b == '\\' || b < 0x20 || b >= 0x80)
                    break;
                run++;
            }
            if (TextPut(p, p->src + p->pos, run - p->pos) != 0)
                return -1;
            p->pos = run;
        }
    }

    *ofs = begin;
    *len = p->doc->textSize - begin;
    return TextPut(p, "", 1);
}

static int32_t IsDigit(int32_t c) { return c >= '0' && c <= '9'; }

/* -? (0 | [1-9][0-9]*) (. [0-9]+)? ([eE] [+-]? [0-9]+)? -- copied as text. */
static int32_t ParseNumber(Parser *p, uint32_t *ofs, uint32_t *len)
{
    uint32_t start = p->pos;

    if (Peek(p) == '-')
        p->pos++;
    if (Peek(p) == '0') {
        p->pos++;
        if (IsDigit(Peek(p))) {
            Fail(p, start, "number with a leading zero");
            return -1;
        }
    } else if (IsDigit(Peek(p))) {
        while (IsDigit(Peek(p)))
            p->pos++;
    } else {
        Fail(p, start, "bad number");
        return -1;
    }
    if (Peek(p) == '.') {
        p->pos++;
        if (!IsDigit(Peek(p))) {
            Fail(p, start, "bad number (digits expected after '.')");
            return -1;
        }
        while (IsDigit(Peek(p)))
            p->pos++;
    }
    if (Peek(p) == 'e' || Peek(p) == 'E') {
        p->pos++;
        if (Peek(p) == '+' || Peek(p) == '-')
            p->pos++;
        if (!IsDigit(Peek(p))) {
            Fail(p, start, "bad number (digits expected in the exponent)");
            return -1;
        }
        while (IsDigit(Peek(p)))
            p->pos++;
    }

    *ofs = p->doc->textSize;
    *len = p->pos - start;
    if (TextPut(p, p->src + start, *len) != 0)
        return -1;
    return TextPut(p, "", 1);
}

static int32_t ParseLiteral(Parser *p, const char *word)
{
    uint32_t n = (uint32_t)strlen(word);
    if (p->len - p->pos < n || memcmp(p->src + p->pos, word, n) != 0) {
        Fail(p, p->pos, "unexpected character");
        return -1;
    }
    p->pos += n;
    return 0;
}

static int32_t ParseValue(Parser *p, int32_t depth);

/* Link `child` as the next element of `parent`, after `*last`. */
static void Append(Parser *p, int32_t parent, int32_t child, int32_t *last)
{
    SgJsonNode *nodes = p->doc->nodes;
    if (*last < 0) nodes[parent].first = child;
    else nodes[*last].next = child;
    nodes[parent].count++;
    *last = child;
}

static int32_t ParseArray(Parser *p, int32_t self, int32_t depth)
{
    int32_t last = -1;

    p->pos++;                       /* '[' */
    SkipSpace(p);
    if (p->failed)
        return -1;
    if (Peek(p) == ']') {
        p->pos++;
        return 0;
    }
    for (;;) {
        int32_t child = ParseValue(p, depth);
        if (child < 0)
            return -1;
        Append(p, self, child, &last);
        SkipSpace(p);
        if (p->failed)
            return -1;
        if (Peek(p) == ',') {
            p->pos++;
            SkipSpace(p);
            if (p->failed)
                return -1;
            if (Peek(p) == ']' && p->trailingCommas) {
                p->pos++;
                return 0;
            }
            continue;
        }
        if (Peek(p) == ']') {
            p->pos++;
            return 0;
        }
        Fail(p, p->pos, AtEnd(p) ? "unterminated array" : "',' or ']' expected");
        return -1;
    }
}

static int32_t ParseObject(Parser *p, int32_t self, int32_t depth)
{
    int32_t last = -1;

    p->pos++;                       /* '{' */
    SkipSpace(p);
    if (p->failed)
        return -1;
    if (Peek(p) == '}') {
        p->pos++;
        return 0;
    }
    for (;;) {
        uint32_t key, keyLen;
        int32_t child;

        if (Peek(p) != '"') {
            Fail(p, p->pos, AtEnd(p) ? "unterminated object" : "member name (a string) expected");
            return -1;
        }
        if (ParseString(p, &key, &keyLen) != 0)
            return -1;
        SkipSpace(p);
        if (p->failed)
            return -1;
        if (Peek(p) != ':') {
            Fail(p, p->pos, "':' expected after a member name");
            return -1;
        }
        p->pos++;
        child = ParseValue(p, depth);
        if (child < 0)
            return -1;
        p->doc->nodes[child].key = key;
        p->doc->nodes[child].keyLen = keyLen;
        Append(p, self, child, &last);

        SkipSpace(p);
        if (p->failed)
            return -1;
        if (Peek(p) == ',') {
            p->pos++;
            SkipSpace(p);
            if (p->failed)
                return -1;
            if (Peek(p) == '}' && p->trailingCommas) {
                p->pos++;
                return 0;
            }
            continue;
        }
        if (Peek(p) == '}') {
            p->pos++;
            return 0;
        }
        Fail(p, p->pos, AtEnd(p) ? "unterminated object" : "',' or '}' expected");
        return -1;
    }
}

/* One value, leading whitespace included. Returns its node index or -1. */
static int32_t ParseValue(Parser *p, int32_t depth)
{
    int32_t self = -1, c, rc = 0;

    SkipSpace(p);
    if (p->failed)
        return -1;
    c = Peek(p);

    switch (c) {
    case -1:
        Fail(p, p->pos, "value expected, end of input found");
        return -1;
    case '{':
    case '[':
        if (depth >= p->maxDepth) {
            Fail(p, p->pos, "nested too deeply");
            return -1;
        }
        self = NewNode(p, c == '{' ? SG_JSON_OBJECT : SG_JSON_ARRAY);
        if (self < 0)
            return -1;
        rc = (c == '{') ? ParseObject(p, self, depth + 1) : ParseArray(p, self, depth + 1);
        break;
    case '"':
        self = NewNode(p, SG_JSON_STRING);
        if (self < 0)
            return -1;
        {
            uint32_t ofs = 0, len = 0;
            rc = ParseString(p, &ofs, &len);
            p->doc->nodes[self].str = ofs;
            p->doc->nodes[self].strLen = len;
        }
        break;
    case 't':
        self = NewNode(p, SG_JSON_TRUE);
        rc = self < 0 ? -1 : ParseLiteral(p, "true");
        break;
    case 'f':
        self = NewNode(p, SG_JSON_FALSE);
        rc = self < 0 ? -1 : ParseLiteral(p, "false");
        break;
    case 'n':
        self = NewNode(p, SG_JSON_NULL);
        rc = self < 0 ? -1 : ParseLiteral(p, "null");
        break;
    default:
        if (c == '-' || IsDigit(c)) {
            self = NewNode(p, SG_JSON_NUMBER);
            if (self < 0)
                return -1;
            {
                uint32_t ofs = 0, len = 0;
                rc = ParseNumber(p, &ofs, &len);
                p->doc->nodes[self].str = ofs;
                p->doc->nodes[self].strLen = len;
            }
            break;
        }
        Fail(p, p->pos, "unexpected character");
        return -1;
    }

    if (self < 0 || rc != 0 || p->failed)
        return -1;
    p->doc->nodes[self].srcLen = p->pos - p->doc->nodes[self].srcOfs;
    return self;
}

/* ------------------------------------------------------------------------ */

int32_t Sg_JsonParse(SgJsonDoc *doc, const char *src, uint32_t len,
                     const SgJsonOptions *opt, char *err)
{
    Parser p;

    memset(doc, 0, sizeof *doc);
    memset(&p, 0, sizeof p);
    p.doc = doc;
    p.src = src;
    p.len = src ? len : 0;
    p.err = err;
    p.maxDepth = DEFAULT_MAX_DEPTH;
    if (opt) {
        p.trailingCommas = opt->allowTrailingCommas;
        p.comments = opt->allowComments;
        if (opt->maxDepth > 0)
            p.maxDepth = opt->maxDepth < HARD_MAX_DEPTH ? opt->maxDepth : HARD_MAX_DEPTH;
    }
    if (p.len > 0x7FFFFFF0u) {
        snprintf(err, 256, "JSON: document too large");
        return -1;
    }

    if (ParseValue(&p, 0) == 0) {
        SkipSpace(&p);
        if (!p.failed && !AtEnd(&p))
            Fail(&p, p.pos, "unexpected text after the end of the document");
    }
    if (p.failed) {
        Sg_JsonFree(doc);
        return -1;
    }
    doc->src = src;
    doc->srcLen = p.len;
    return 0;
}

void Sg_JsonFree(SgJsonDoc *doc)
{
    free(doc->nodes);
    free(doc->text);
    memset(doc, 0, sizeof *doc);
}

const SgJsonNode *Sg_JsonRoot(const SgJsonDoc *doc)
{
    return doc->nodeCount ? &doc->nodes[0] : NULL;
}

const SgJsonNode *Sg_JsonFirst(const SgJsonDoc *doc, const SgJsonNode *n)
{
    if (!n || (n->kind != SG_JSON_ARRAY && n->kind != SG_JSON_OBJECT) || n->first < 0)
        return NULL;
    return &doc->nodes[n->first];
}

const SgJsonNode *Sg_JsonNext(const SgJsonDoc *doc, const SgJsonNode *n)
{
    return (n && n->next >= 0) ? &doc->nodes[n->next] : NULL;
}

const char *Sg_JsonKey(const SgJsonDoc *doc, const SgJsonNode *member, uint32_t *len)
{
    if (len)
        *len = member->keyLen;
    return doc->text + member->key;
}

const SgJsonNode *Sg_JsonGet(const SgJsonDoc *doc, const SgJsonNode *obj, const char *key)
{
    const SgJsonNode *found = NULL;
    uint32_t klen = (uint32_t)strlen(key);

    if (!obj || obj->kind != SG_JSON_OBJECT)
        return NULL;
    for (const SgJsonNode *m = Sg_JsonFirst(doc, obj); m; m = Sg_JsonNext(doc, m))
        if (m->keyLen == klen && memcmp(doc->text + m->key, key, klen) == 0)
            found = m;      /* keep going: the last one wins */
    return found;
}

const char *Sg_JsonStr(const SgJsonDoc *doc, const SgJsonNode *n, uint32_t *len)
{
    if (!n || n->kind != SG_JSON_STRING)
        return NULL;
    if (len)
        *len = n->strLen;
    return doc->text + n->str;
}

const char *Sg_JsonRaw(const SgJsonDoc *doc, const SgJsonNode *n, uint32_t *len)
{
    if (len)
        *len = n->srcLen;
    return doc->src + n->srcOfs;
}

/* An integer literal's magnitude, or -1 if it has a fraction/exponent or does
 * not fit in `max`. *neg says whether it had a '-'. */
static int32_t Magnitude(const SgJsonDoc *doc, const SgJsonNode *n, uint64_t max, uint64_t *mag, int32_t *neg)
{
    const char *s;
    uint32_t i = 0;
    uint64_t v = 0;

    if (!n || n->kind != SG_JSON_NUMBER)
        return -1;
    s = doc->text + n->str;
    *neg = 0;
    if (s[0] == '-') {
        *neg = 1;
        i = 1;
    }
    if (i >= n->strLen)
        return -1;
    for (; i < n->strLen; i++) {
        if (s[i] < '0' || s[i] > '9')
            return -1;          /* '.', 'e': GetInt32 refuses those */
        v = v * 10 + (uint64_t)(s[i] - '0');
        if (v > max)
            return -1;
    }
    *mag = v;
    return 0;
}

int32_t Sg_JsonInt32(const SgJsonDoc *doc, const SgJsonNode *n, int32_t *out)
{
    uint64_t mag;
    int32_t neg;

    if (Magnitude(doc, n, 2147483648u, &mag, &neg) != 0)
        return -1;
    if (!neg && mag > 2147483647u)
        return -1;
    *out = neg ? (int32_t)(0 - (int64_t)mag) : (int32_t)mag;
    return 0;
}

int32_t Sg_JsonUInt32(const SgJsonDoc *doc, const SgJsonNode *n, uint32_t *out)
{
    uint64_t mag;
    int32_t neg;

    if (Magnitude(doc, n, 0xFFFFFFFFu, &mag, &neg) != 0)
        return -1;
    if (neg && mag != 0)
        return -1;          /* "-0" is zero */
    *out = (uint32_t)mag;
    return 0;
}
