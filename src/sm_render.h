/* sm_render.h  --  thin rendering/backend interface.
 *
 * Design doc S13: "Rendering backend is abstracted behind a thin interface
 * so the runtime never depends on the specific backend." Everything above
 * this line (the engine, Webcel) is pure portable C and talks only to SmCanvas /
 * SmApp; only the backend (sm_win32.c) includes <windows.h>. Swapping GDI for
 * another backend means reimplementing this interface and nothing else.
 */
#ifndef SM_RENDER_H
#define SM_RENDER_H

#include "sm_common.h"

typedef unsigned long SmColor;   /* 0x00RRGGBB */

typedef struct { int x, y, w, h; } SmRect;

/* Text alignment within a clipped rect. */
#define SM_ALIGN_LEFT   0
#define SM_ALIGN_CENTER 1
#define SM_ALIGN_RIGHT  2

/* Font-style bits for text_ex (combine with |). */
#define SM_FS_BOLD      1u
#define SM_FS_ITALIC    2u
#define SM_FS_UNDERLINE 4u
#define SM_FS_STRIKE    8u
#define SM_FS_MONO      16u

/* A drawing surface for one frame. The backend supplies `impl` and the
 * function pointers; callers never dereference `impl`. */
typedef struct SmCanvas SmCanvas;
struct SmCanvas {
    void *impl;
    void (*fill_rect)(SmCanvas *cv, SmRect r, SmColor color);
    void (*frame_rect)(SmCanvas *cv, SmRect r, SmColor color); /* 1px border */
    void (*text)(SmCanvas *cv, SmRect clip, const char *s, SmColor color, int align);
    int  (*text_width)(SmCanvas *cv, const char *s);
    int  (*line_height)(SmCanvas *cv);

    /* Styled/sized text: `style` is a mask of SM_FS_*, `px` is the pixel font
     * height (0 = default). text_width_ex measures under the same font. */
    void (*text_ex)(SmCanvas *cv, SmRect clip, const char *s, SmColor color,
                    int align, unsigned style, int px);
    int  (*text_width_ex)(SmCanvas *cv, const char *s, unsigned style, int px);

    /* Blit the image at `path`, scaled into `r`. Returns SM_TRUE if drawn; on
     * SM_FALSE the caller falls back to a placeholder box. */
    SmBool (*draw_image)(SmCanvas *cv, SmRect r, const char *path);
};

/* Special (non-character) keys delivered to on_key. */
typedef enum {
    SM_KEY_NONE = 0,
    SM_KEY_LEFT, SM_KEY_RIGHT, SM_KEY_UP, SM_KEY_DOWN,
    SM_KEY_ENTER, SM_KEY_ESCAPE, SM_KEY_BACKSPACE, SM_KEY_TAB, SM_KEY_DELETE
} SmKey;

/* The application, as a bundle of callbacks over opaque state. The backend
 * owns the window and event loop and calls these; returning SM_TRUE from an
 * input handler requests a repaint. */
typedef struct SmApp SmApp;
struct SmApp {
    void  *state;
    void   (*on_paint) (void *state, SmCanvas *cv, int width, int height);
    SmBool (*on_char)  (void *state, int ch);            /* printable input   */
    SmBool (*on_key)   (void *state, SmKey key);         /* special keys      */
    SmBool (*on_click) (void *state, int x, int y);      /* left button down  */
    void   (*on_resize)(void *state, int width, int height);

    /* A file was dropped onto the window (drag-and-drop from Explorer). May be
     * NULL if the app doesn't support opening files this way. */
    SmBool (*on_open_file)(void *state, const char *path);

    /* Mouse wheel: `notches` is positive for scroll-up/away, negative for
     * scroll-down/toward (one notch per typical wheel click). May be NULL if
     * the app has nothing scrollable. */
    SmBool (*on_scroll)(void *state, int notches);
};

/* Implemented by the backend (sm_win32.c): open a window titled `title`,
 * run the event loop dispatching to `app`, and return the exit code. */
int sm_backend_run(const char *title, int width, int height, SmApp *app);

#endif /* SM_RENDER_H */
