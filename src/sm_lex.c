/* sm_lex.c  --  SeSaML tokenizer implementation. */
#include "sm_lex.h"

#include <string.h>
#include <ctype.h>

void sm_lex_init(SmLexer *lx, const char *src)
{
    lx->src = (src != NULL) ? src : "";
    lx->pos = 0;
    lx->len = strlen(lx->src);
    lx->line = 1;
    lx->col = 1;
    lx->errmsg[0] = '\0';
    lx->cur.type = SM_TOK_EOF;
    lx->cur.lex = lx->src;
    lx->cur.len = 0;
    lx->cur.number = 0.0;
    lx->cur.color = 0UL;
    lx->cur.line = 1;
    lx->cur.col = 1;
    (void)sm_lex_next(lx);
}

static int peek(const SmLexer *lx, size_t ahead)
{
    size_t p = lx->pos + ahead;
    if (p >= lx->len) {
        return -1;
    }
    return (unsigned char)lx->src[p];
}

static int advance(SmLexer *lx)
{
    int c;
    if (lx->pos >= lx->len) {
        return -1;
    }
    c = (unsigned char)lx->src[lx->pos++];
    if (c == '\n') {
        ++lx->line;
        lx->col = 1;
    } else {
        ++lx->col;
    }
    return c;
}

static int is_ident_start(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_ident_char(int c)
{
    return is_ident_start(c) || (c >= '0' && c <= '9');
}

static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

/* Skip spaces/tabs and comments; return SM_TRUE if a line break was crossed
 * (so the caller can emit a single collapsed NEWLINE token). */
static SmBool skip_trivia(SmLexer *lx)
{
    SmBool sawnl = SM_FALSE;
    for (;;) {
        int c = peek(lx, 0);
        if (c == ' ' || c == '\t' || c == '\r') {
            (void)advance(lx);
        } else if (c == '\n') {
            sawnl = SM_TRUE;
            (void)advance(lx);
        } else if (c == '-' && peek(lx, 1) == '-') {
            /* line comment to end of line (do not consume the newline here) */
            while (peek(lx, 0) != -1 && peek(lx, 0) != '\n') {
                (void)advance(lx);
            }
        } else {
            break;
        }
    }
    return sawnl;
}

static void set_tok(SmLexer *lx, SmTokType type, const char *lex, size_t len,
                    int line, int col)
{
    lx->cur.type = type;
    lx->cur.lex = lex;
    lx->cur.len = len;
    lx->cur.number = 0.0;
    lx->cur.color = 0UL;
    lx->cur.line = line;
    lx->cur.col = col;
}

static SmStatus lex_error(SmLexer *lx, const char *msg, int line, int col)
{
    size_t n = strlen(msg);
    if (n >= sizeof(lx->errmsg)) {
        n = sizeof(lx->errmsg) - 1;
    }
    memcpy(lx->errmsg, msg, n);
    lx->errmsg[n] = '\0';
    set_tok(lx, SM_TOK_ERROR, lx->src + lx->pos, 0, line, col);
    return SM_ERR_PARSE;
}

SmStatus sm_lex_next(SmLexer *lx)
{
    SmBool sawnl;
    int line, col;
    int c;
    const char *start;

    sawnl = skip_trivia(lx);
    line = lx->line;
    col = lx->col;

    if (sawnl) {
        /* Emit one NEWLINE for a run of blank lines. Position points at the
         * next real token's line, which is fine for diagnostics. */
        set_tok(lx, SM_TOK_NEWLINE, lx->src + lx->pos, 0, line, col);
        return SM_OK;
    }

    if (lx->pos >= lx->len) {
        set_tok(lx, SM_TOK_EOF, lx->src + lx->pos, 0, line, col);
        return SM_OK;
    }

    start = lx->src + lx->pos;
    c = peek(lx, 0);

    /* identifier / keyword */
    if (is_ident_start(c)) {
        size_t n = 0;
        while (is_ident_char(peek(lx, 0))) {
            (void)advance(lx);
            ++n;
        }
        set_tok(lx, SM_TOK_IDENT, start, n, line, col);
        return SM_OK;
    }

    /* number: DIGIT+ ('.' DIGIT+)? */
    if (c >= '0' && c <= '9') {
        double val = 0.0;
        double frac, scale;
        while (peek(lx, 0) >= '0' && peek(lx, 0) <= '9') {
            val = val * 10.0 + (double)(advance(lx) - '0');
        }
        if (peek(lx, 0) == '.' && peek(lx, 1) >= '0' && peek(lx, 1) <= '9') {
            (void)advance(lx); /* consume '.' */
            frac = 0.0;
            scale = 1.0;
            while (peek(lx, 0) >= '0' && peek(lx, 0) <= '9') {
                frac = frac * 10.0 + (double)(advance(lx) - '0');
                scale *= 10.0;
            }
            val += frac / scale;
        }
        set_tok(lx, SM_TOK_NUMBER, start, (size_t)(lx->src + lx->pos - start),
                line, col);
        lx->cur.number = val;
        return SM_OK;
    }

    /* string: "..." with no escapes */
    if (c == '"') {
        const char *content;
        size_t n = 0;
        (void)advance(lx); /* opening quote */
        content = lx->src + lx->pos;
        while (peek(lx, 0) != -1 && peek(lx, 0) != '"') {
            (void)advance(lx);
            ++n;
        }
        if (peek(lx, 0) != '"') {
            return lex_error(lx, "unterminated string literal", line, col);
        }
        (void)advance(lx); /* closing quote */
        set_tok(lx, SM_TOK_STRING, content, n, line, col);
        return SM_OK;
    }

    /* colour: #RRGGBB */
    if (c == '#') {
        unsigned long rgb = 0UL;
        int i, h;
        (void)advance(lx); /* '#' */
        for (i = 0; i < 6; ++i) {
            h = hexval(peek(lx, 0));
            if (h < 0) {
                return lex_error(lx, "colour literal needs 6 hex digits", line, col);
            }
            rgb = (rgb << 4) | (unsigned long)h;
            (void)advance(lx);
        }
        set_tok(lx, SM_TOK_COLOR, start, 7, line, col);
        lx->cur.color = rgb;
        return SM_OK;
    }

    /* operators and punctuation */
    (void)advance(lx);
    switch (c) {
    case '(': set_tok(lx, SM_TOK_LPAREN, start, 1, line, col); return SM_OK;
    case ')': set_tok(lx, SM_TOK_RPAREN, start, 1, line, col); return SM_OK;
    case ',': set_tok(lx, SM_TOK_COMMA, start, 1, line, col); return SM_OK;
    case '.':
        if (peek(lx, 0) == '.') { (void)advance(lx);
            set_tok(lx, SM_TOK_CONCAT, start, 2, line, col); return SM_OK; }
        set_tok(lx, SM_TOK_DOT, start, 1, line, col); return SM_OK;
    case '/': set_tok(lx, SM_TOK_SLASH, start, 1, line, col); return SM_OK;
    case '+': set_tok(lx, SM_TOK_PLUS, start, 1, line, col); return SM_OK;
    case '-': set_tok(lx, SM_TOK_MINUS, start, 1, line, col); return SM_OK;
    case '*': set_tok(lx, SM_TOK_STAR, start, 1, line, col); return SM_OK;
    case '%': set_tok(lx, SM_TOK_PERCENT, start, 1, line, col); return SM_OK;
    case '=':    /* '=' and Lua's '==' both mean equality */
        if (peek(lx, 0) == '=') { (void)advance(lx);
            set_tok(lx, SM_TOK_EQ, start, 2, line, col); return SM_OK; }
        set_tok(lx, SM_TOK_EQ, start, 1, line, col); return SM_OK;
    case '~':    /* Lua's '~=' (also C's '!=') mean not-equal */
    case '!':
        if (peek(lx, 0) == '=') { (void)advance(lx);
            set_tok(lx, SM_TOK_NE, start, 2, line, col); return SM_OK; }
        break;
    case '<':
        if (peek(lx, 0) == '>') { (void)advance(lx);
            set_tok(lx, SM_TOK_NE, start, 2, line, col); return SM_OK; }
        if (peek(lx, 0) == '=') { (void)advance(lx);
            set_tok(lx, SM_TOK_LE, start, 2, line, col); return SM_OK; }
        set_tok(lx, SM_TOK_LT, start, 1, line, col); return SM_OK;
    case '>':
        if (peek(lx, 0) == '=') { (void)advance(lx);
            set_tok(lx, SM_TOK_GE, start, 2, line, col); return SM_OK; }
        set_tok(lx, SM_TOK_GT, start, 1, line, col); return SM_OK;
    default:
        break;
    }
    return lex_error(lx, "unexpected character", line, col);
}

SmBool sm_tok_is_kw(const SmToken *t, const char *kw)
{
    size_t klen;
    if (t == NULL || t->type != SM_TOK_IDENT || kw == NULL) {
        return SM_FALSE;
    }
    klen = strlen(kw);
    if (klen != t->len) {
        return SM_FALSE;
    }
    {
        size_t i;
        for (i = 0; i < klen; ++i) {
            if (tolower((unsigned char)t->lex[i]) != tolower((unsigned char)kw[i])) {
                return SM_FALSE;
            }
        }
    }
    return SM_TRUE;
}

char *sm_tok_dup(const SmToken *t)
{
    char *d;
    if (t == NULL) {
        return NULL;
    }
    d = (char *)sm_alloc(t->len + 1);
    if (d == NULL) {
        return NULL;
    }
    if (t->len > 0) {
        memcpy(d, t->lex, t->len);
    }
    d[t->len] = '\0';
    return d;
}

const char *sm_tok_type_name(SmTokType t)
{
    switch (t) {
    case SM_TOK_EOF:     return "end-of-input";
    case SM_TOK_NEWLINE: return "newline";
    case SM_TOK_IDENT:   return "identifier";
    case SM_TOK_NUMBER:  return "number";
    case SM_TOK_STRING:  return "string";
    case SM_TOK_COLOR:   return "colour";
    case SM_TOK_LPAREN:  return "'('";
    case SM_TOK_RPAREN:  return "')'";
    case SM_TOK_COMMA:   return "','";
    case SM_TOK_DOT:     return "'.'";
    case SM_TOK_SLASH:   return "'/'";
    case SM_TOK_EQ:      return "'='";
    case SM_TOK_NE:      return "'<>'";
    case SM_TOK_LT:      return "'<'";
    case SM_TOK_GT:      return "'>'";
    case SM_TOK_LE:      return "'<='";
    case SM_TOK_GE:      return "'>='";
    case SM_TOK_PLUS:    return "'+'";
    case SM_TOK_MINUS:   return "'-'";
    case SM_TOK_STAR:    return "'*'";
    case SM_TOK_PERCENT: return "'%'";
    case SM_TOK_CONCAT:  return "'..'";
    case SM_TOK_ERROR:   return "error";
    }
    return "?";
}
