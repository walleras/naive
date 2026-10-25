/* sm_server.c  --  mock SeSaML route server (strict ISO C89). */
#include "sm_server.h"

#include <stdio.h>
#include <string.h>

static char g_root[256] = "www";

void sm_server_set_root(const char *dir)
{
    if (dir != NULL) {
        size_t n = strlen(dir);
        if (n >= sizeof(g_root)) { n = sizeof(g_root) - 1; }
        memcpy(g_root, dir, n);
        g_root[n] = '\0';
    }
}

static char *read_file(const char *path)
{
    FILE *f;
    long sz;
    char *buf;
    size_t got;
    f = fopen(path, "rb");
    if (f == NULL) { return NULL; }
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return NULL; }
    buf = (char *)sm_alloc((size_t)sz + 1);
    if (buf == NULL) { fclose(f); return NULL; }
    got = fread(buf, 1, (size_t)sz, f);
    buf[got] = '\0';
    fclose(f);
    return buf;
}

/* Does `s` contain ".." anywhere? (Path-traversal guard for the domain
 * resolver below, which -- unlike the fixed ROUTES table -- builds a file path
 * directly out of address-bar text.) */
static SmBool has_dotdot(const char *s)
{
    size_t i;
    for (i = 0; s[i] != '\0'; ++i) {
        if (s[i] == '.' && s[i + 1] == '.') { return SM_TRUE; }
    }
    return SM_FALSE;
}

/* A "virtual internet": any address that doesn't start with '/' is read as a
 * domain, e.g. "xyz.com" or "xyz.com/about". The folder is the domain with its
 * TLD stripped at the first '.' ("xyz.com" -> "xyz"), served from
 * "<root>/<folder>/"; the bare domain serves "index.sml", "xyz.com/about"
 * serves "about.sml". Builds "<root>/<folder>/<page>.sml" into `path` (size
 * `pathcap`); returns SM_FALSE (path left empty) if the address is malformed. */
static SmBool resolve_domain_path(const char *addr, char *path, size_t pathcap)
{
    size_t domain_end = 0, dot = 0;
    char folder[128];
    char page[128];
    const char *rest;

    path[0] = '\0';
    if (pathcap < 408) { return SM_FALSE; } /* room for root+folder+page+".sml"+NUL */
    if (addr[0] == '\0' || has_dotdot(addr)) { return SM_FALSE; }

    while (addr[domain_end] != '\0' && addr[domain_end] != '/') { ++domain_end; }
    if (domain_end == 0) { return SM_FALSE; }

    dot = 0;
    while (dot < domain_end && addr[dot] != '.') { ++dot; }
    if (dot >= sizeof(folder)) { return SM_FALSE; }
    memcpy(folder, addr, dot);
    folder[dot] = '\0';

    rest = addr + domain_end;
    if (rest[0] == '\0' || strcmp(rest, "/") == 0) {
        strcpy(page, "index");
    } else {
        size_t n;
        ++rest; /* skip the leading '/' */
        n = strlen(rest);
        while (n > 0 && rest[n - 1] == '/') { --n; } /* drop a trailing slash */
        if (n == 0 || n >= sizeof(page)) { return SM_FALSE; }
        memcpy(page, rest, n);
        page[n] = '\0';
    }

    sprintf(path, "%.200s/%.100s/%.100s.sml", g_root, folder, page);
    return SM_TRUE;
}

SmStatus sm_server_get(const char *route, char **out_body, char *err, size_t errcap)
{
    char path[512];
    char *body;
    char bare[256];

    if (out_body != NULL) { *out_body = NULL; }
    if (route == NULL) { route = "/"; }

    /* Match on the path only, ignoring any ?query (as a real server would). */
    {
        size_t n = 0;
        while (route[n] != '\0' && route[n] != '?' && n < sizeof(bare) - 1) {
            bare[n] = route[n];
            ++n;
        }
        bare[n] = '\0';
    }

    if (bare[0] == '/') {
        /* No route table: "/a/b" and "/a/b.sml" both serve "<root>/a/b.sml",
         * and "/" serves "<root>/index.sml". */
        size_t len = strlen(bare);
        if (len > 1 && bare[len - 1] == '/') { bare[--len] = 0; }
        if (len > 4 && strcmp(bare + len - 4, ".sml") == 0) { bare[len - 4] = 0; }
        if (has_dotdot(bare) || strlen(bare) > 200) {
            if (err != NULL && errcap > 0) { sprintf(err, "404 Not Found: %.100s", route); }
            return SM_ERR_NOT_FOUND;
        }
        if (strcmp(bare, "/") == 0) { strcpy(bare, "/index"); }
        sprintf(path, "%.240s%s.sml", g_root, bare);
    } else {
        /* Not a leading-slash route: read it as a domain in the virtual
         * internet ("xyz.com" -> www/xyz/index.sml, etc). */
        if (!resolve_domain_path(bare, path, sizeof(path))) {
            if (err != NULL && errcap > 0) { sprintf(err, "404 Not Found: %.100s", route); }
            return SM_ERR_NOT_FOUND;
        }
    }

    body = read_file(path);
    if (body == NULL) {
        if (err != NULL && errcap > 0) { sprintf(err, "404 Not Found: %.100s", route); }
        return SM_ERR_NOT_FOUND;
    }
    if (out_body != NULL) { *out_body = body; }
    return SM_OK;
}

/* Build a SeSaML page that echoes the fields of an urlencoded POST body -- the
 * dynamic response a 1995 CGI script would return. */
static char *build_echo_page(const char *body)
{
    size_t cap = 4096;
    char *p = (char *)sm_alloc(cap);
    const char *b = (body != NULL) ? body : "";
    int y = 96, idx = 0;
    if (p == NULL) { return NULL; }
    strcpy(p,
        "CREATE Home AS TextBox WITH text = \"<- Home\", link = \"/\" AT X.Left, Y.Top\n"
        "CREATE Title AS H2 WITH text = \"Form received (HTTP POST)\" AT X.Center, Y.40\n"
        "CREATE Ct AS TextBox WITH text = \"Content-Type: application/x-www-form-urlencoded\""
        " AT X.Left, Y.70 MEMBER OF ITALICS\n");
    while (*b != '\0') {
        char field[300];
        char line[400];
        int n = 0;
        while (*b != '\0' && *b != '&' && n < 299) {
            char c = *b++;
            if (c == '+') { c = ' '; }
            if (c == '"' || c == '\n' || c == '\r') { c = ' '; }
            field[n++] = c;
        }
        if (*b == '&') { ++b; }
        field[n] = '\0';
        sprintf(line, "CREATE F%d AS TextBox WITH text = \"%s\" AT X.Left, Y.%d\n",
                idx, field, y);
        if (strlen(p) + strlen(line) < cap - 1) { strcat(p, line); }
        y += 24;
        ++idx;
    }
    return p;
}

SmStatus sm_server_post(const char *route, const char *body,
                        char **out_body, char *err, size_t errcap)
{
    char bare[256];
    size_t n = 0;
    if (out_body != NULL) { *out_body = NULL; }
    if (route == NULL) { route = "/"; }
    while (route[n] != '\0' && route[n] != '?' && n < sizeof(bare) - 1) {
        bare[n] = route[n]; ++n;
    }
    bare[n] = '\0';

    if (strcmp(bare, "/submit") == 0) {
        char *page = build_echo_page(body);
        if (page == NULL) {
            if (err != NULL && errcap) { sprintf(err, "500 out of memory"); }
            return SM_ERR_OOM;
        }
        if (out_body != NULL) { *out_body = page; } else { sm_free(page); }
        return SM_OK;
    }
    /* other routes: a POST behaves like a GET */
    return sm_server_get(route, out_body, err, errcap);
}
