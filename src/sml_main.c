/* sml_main.c  --  the SeSaML page viewer entry point (strict ISO C89).
 *
 * Usage:
 *   sesaml               -> navigate the mock server route "/"
 *   sesaml /docs         -> navigate a route (arg begins with '/')
 *   sesaml page.sml      -> load a local file directly
 * The server root can be overridden with the SESAML_WWW environment variable
 * (default "www"). Pure C89: only wires sm_doc to the backend via SmApp.
 */
#include "sm_doc.h"
#include "sm_server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Does "<dir>/index.sml" exist and is it readable? */
static SmBool has_index(const char *dir)
{
    char path[300];
    FILE *f;
    sprintf(path, "%.250s/index.sml", dir);
    f = fopen(path, "rb");
    if (f == NULL) { return SM_FALSE; }
    fclose(f);
    return SM_TRUE;
}

/* Auto-detect the server root so "sesaml" with no args works whether it is
 * launched from the project root (CWD = project root, "www" resolves), or by
 * double-clicking build/sesaml.exe (CWD = build/, so "www" would incorrectly
 * mean build/www -- the real content lives one directory up, as a sibling of
 * build/). Tries, in order: the plain relative "www", "<exe_dir>/www", and
 * "<exe_dir>/../www"; sets the first one whose index.sml is readable. Leaves
 * the default ("www") in place if nothing better is found, so behavior is
 * unchanged for anyone who already had a working setup. */
static void autodetect_root(const char *argv0)
{
    char exe_dir[256];
    char cand[300];
    size_t n;
    int i;

    if (has_index("www")) { return; }

    exe_dir[0] = '\0';
    if (argv0 != NULL) {
        n = strlen(argv0);
        for (i = (int)n - 1; i >= 0; --i) {
            if (argv0[i] == '\\' || argv0[i] == '/') {
                size_t len = (size_t)i;
                if (len >= sizeof(exe_dir)) { len = sizeof(exe_dir) - 1; }
                memcpy(exe_dir, argv0, len);
                exe_dir[len] = '\0';
                break;
            }
        }
    }
    if (exe_dir[0] == '\0') { return; } /* argv0 had no path component */

    sprintf(cand, "%.250s/www", exe_dir);
    if (has_index(cand)) { sm_server_set_root(cand); return; }

    sprintf(cand, "%.240s/../www", exe_dir);
    if (has_index(cand)) { sm_server_set_root(cand); return; }
}

int main(int argc, char **argv)
{
    SmDoc *d;
    SmApp app;
    char err[192];
    const char *arg = (argc > 1) ? argv[1] : NULL;
    const char *root = getenv("SESAML_WWW");
    int rc;

    if (root != NULL) { sm_server_set_root(root); }
    else { autodetect_root(argv[0]); }

    d = sm_doc_new();
    if (d == NULL) {
        return 1;
    }

    if (arg == NULL) {
        sm_doc_navigate(d, "/");                 /* server home */
    } else if (arg[0] == '/') {
        sm_doc_navigate(d, arg);                 /* a route */
    } else if (sm_doc_load_file(d, arg, err, sizeof(err)) != SM_OK) {
        fprintf(stderr, "SeSaML: could not load '%s': %s\n", arg, err);
        sm_doc_free(d);
        return 2;
    }

    sm_doc_fill_app(d, &app);
    rc = sm_backend_run("SeSaML", 720, 540, &app);
    sm_doc_free(d);
    return rc;
}
