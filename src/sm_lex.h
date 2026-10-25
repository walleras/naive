/* sm_lex.h  --  SeSaML tokenizer (strict ISO C89).  Grammar sketch S1.
 *
 * Streaming lexer with one token of lookahead (lx->cur). Lexemes point into
 * the source buffer -- SeSaML string literals have no escape sequences, so a
 * token is always a verbatim slice of source and needs no allocation. The
 * caller must keep the source alive for as long as any token is in use.
 *
 * The language is case-insensitive and has contextual keywords (a set may be
 * named "Set"), so the lexer emits every bareword as SM_TOK_IDENT and leaves
 * keyword recognition to the parser via sm_tok_is_kw().
 */
#ifndef SM_LEX_H
#define SM_LEX_H

#include "sm_common.h"

typedef enum {
    SM_TOK_EOF = 0,
    SM_TOK_NEWLINE,   /* one or more physical line breaks, collapsed         */
    SM_TOK_IDENT,     /* a bareword; may be a contextual keyword             */
    SM_TOK_NUMBER,    /* value in .number                                    */
    SM_TOK_STRING,    /* .lex/.len span the content, quotes excluded         */
    SM_TOK_COLOR,     /* #RRGGBB; value in .color                            */
    SM_TOK_LPAREN, SM_TOK_RPAREN, SM_TOK_COMMA, SM_TOK_DOT, SM_TOK_SLASH,
    SM_TOK_EQ,        /* =  */
    SM_TOK_NE,        /* <> */
    SM_TOK_LT, SM_TOK_GT, SM_TOK_LE, SM_TOK_GE,
    SM_TOK_PLUS, SM_TOK_MINUS, SM_TOK_STAR, SM_TOK_PERCENT,
    SM_TOK_CONCAT,    /* ..  (Lua string concatenation)                      */
    SM_TOK_ERROR
} SmTokType;

typedef struct {
    SmTokType     type;
    const char   *lex;    /* points into source (not owned)                  */
    size_t        len;    /* lexeme length                                   */
    double        number; /* SM_TOK_NUMBER                                   */
    unsigned long color;  /* SM_TOK_COLOR                                    */
    int           line;   /* 1-based                                         */
    int           col;    /* 1-based column of the lexeme's first char       */
} SmToken;

typedef struct {
    const char *src;
    size_t      pos;
    size_t      len;
    int         line;
    int         col;
    SmToken     cur;          /* current token (valid after sm_lex_init)     */
    char        errmsg[128];
} SmLexer;

/* Initialise and read the first token into lx->cur. */
void     sm_lex_init(SmLexer *lx, const char *src);

/* Advance lx->cur to the next token. Returns SM_ERR_PARSE (and fills
 * lx->errmsg, sets cur.type = SM_TOK_ERROR) on an invalid character. */
SmStatus sm_lex_next(SmLexer *lx);

/* Case-insensitive test of a token against a keyword spelling. */
SmBool   sm_tok_is_kw(const SmToken *t, const char *kw);

/* Copy a token's lexeme into a freshly allocated NUL-terminated string
 * (for identifiers / set names the parser must retain). Caller frees. */
char    *sm_tok_dup(const SmToken *t);

/* Human-readable token-type name, for diagnostics. */
const char *sm_tok_type_name(SmTokType t);

#endif /* SM_LEX_H */
