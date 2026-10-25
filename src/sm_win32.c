/* sm_win32.c  --  Win32 GDI backend for the Naive render interface.
 *
 * This is the ONE module permitted to depend on the platform. It is C99 in
 * dialect but compiled with relaxed pedantry because <windows.h> is not
 * strict-ISO-C-clean. Everything it talks to above the OS line (SmCanvas,
 * SmApp) is defined in the portable sm_render.h.
 *
 * In the 1995 counterfactual this stands in for the hand-rolled GDI wrapper a
 * competent C programmer of the era would already have had (design doc S13).
 */
#include "sm_render.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>  /* DragAcceptFiles/DragQueryFileA -- drag-and-drop open */

/* ---- canvas over an HDC ------------------------------------------------- */

typedef struct {
    HDC   hdc;
    HFONT font;
    int   line_h;
} GdiCanvas;

static COLORREF to_ref(SmColor c)
{
    /* SmColor is 0x00RRGGBB; GDI COLORREF is 0x00BBGGRR. */
    unsigned r = (unsigned)((c >> 16) & 0xFF);
    unsigned g = (unsigned)((c >> 8) & 0xFF);
    unsigned b = (unsigned)(c & 0xFF);
    return RGB(r, g, b);
}

static void cv_fill_rect(SmCanvas *cv, SmRect r, SmColor color)
{
    GdiCanvas *g = (GdiCanvas *)cv->impl;
    RECT rc;
    HBRUSH br = CreateSolidBrush(to_ref(color));
    rc.left = r.x; rc.top = r.y; rc.right = r.x + r.w; rc.bottom = r.y + r.h;
    FillRect(g->hdc, &rc, br);
    DeleteObject(br);
}

static void cv_frame_rect(SmCanvas *cv, SmRect r, SmColor color)
{
    GdiCanvas *g = (GdiCanvas *)cv->impl;
    RECT rc;
    HBRUSH br = CreateSolidBrush(to_ref(color));
    rc.left = r.x; rc.top = r.y; rc.right = r.x + r.w; rc.bottom = r.y + r.h;
    FrameRect(g->hdc, &rc, br);
    DeleteObject(br);
}

static void cv_text(SmCanvas *cv, SmRect clip, const char *s, SmColor color, int align)
{
    GdiCanvas *g = (GdiCanvas *)cv->impl;
    RECT rc;
    UINT fmt = DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS;
    if (s == NULL) { return; }
    rc.left = clip.x + 4; rc.top = clip.y;
    rc.right = clip.x + clip.w - 4; rc.bottom = clip.y + clip.h;
    if (align == SM_ALIGN_CENTER) { fmt |= DT_CENTER; }
    else if (align == SM_ALIGN_RIGHT) { fmt |= DT_RIGHT; }
    else { fmt |= DT_LEFT; }
    SetBkMode(g->hdc, TRANSPARENT);
    SetTextColor(g->hdc, to_ref(color));
    DrawTextA(g->hdc, s, -1, &rc, fmt);
}

static int cv_text_width(SmCanvas *cv, const char *s)
{
    GdiCanvas *g = (GdiCanvas *)cv->impl;
    SIZE sz;
    if (s == NULL) { return 0; }
    if (GetTextExtentPoint32A(g->hdc, s, (int)lstrlenA(s), &sz)) {
        return sz.cx;
    }
    return 0;
}

static int cv_line_height(SmCanvas *cv)
{
    GdiCanvas *g = (GdiCanvas *)cv->impl;
    return g->line_h;
}

static HFONT make_font(int px, unsigned style)
{
    int weight = (style & SM_FS_BOLD) ? FW_BOLD : FW_NORMAL;
    DWORD ital = (style & SM_FS_ITALIC) ? 1 : 0;
    DWORD und  = (style & SM_FS_UNDERLINE) ? 1 : 0;
    DWORD strk = (style & SM_FS_STRIKE) ? 1 : 0;
    DWORD pf   = (style & SM_FS_MONO) ? (FIXED_PITCH | FF_MODERN)
                                      : (VARIABLE_PITCH | FF_SWISS);
    const char *face = (style & SM_FS_MONO) ? "Courier New" : "MS Sans Serif";
    if (px <= 0) { px = 13; }
    return CreateFontA(-px, 0, 0, 0, weight, ital, und, strk, ANSI_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                       pf, face);
}

static void cv_text_ex(SmCanvas *cv, SmRect clip, const char *s, SmColor color,
                       int align, unsigned style, int px)
{
    GdiCanvas *g = (GdiCanvas *)cv->impl;
    RECT rc;
    UINT fmt = DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS;
    HFONT f, old;
    if (s == NULL) { return; }
    f = make_font(px, style);
    old = (HFONT)SelectObject(g->hdc, f);
    rc.left = clip.x + 4; rc.top = clip.y;
    rc.right = clip.x + clip.w - 4; rc.bottom = clip.y + clip.h;
    if (align == SM_ALIGN_CENTER) { fmt |= DT_CENTER; }
    else if (align == SM_ALIGN_RIGHT) { fmt |= DT_RIGHT; }
    else { fmt |= DT_LEFT; }
    SetBkMode(g->hdc, TRANSPARENT);
    SetTextColor(g->hdc, to_ref(color));
    DrawTextA(g->hdc, s, -1, &rc, fmt);
    SelectObject(g->hdc, old);
    DeleteObject(f);
}

static int cv_text_width_ex(SmCanvas *cv, const char *s, unsigned style, int px)
{
    GdiCanvas *g = (GdiCanvas *)cv->impl;
    SIZE sz;
    HFONT f, old;
    int w = 0;
    if (s == NULL) { return 0; }
    f = make_font(px, style);
    old = (HFONT)SelectObject(g->hdc, f);
    if (GetTextExtentPoint32A(g->hdc, s, (int)lstrlenA(s), &sz)) { w = sz.cx; }
    SelectObject(g->hdc, old);
    DeleteObject(f);
    return w;
}

/* Load a .bmp from disk and stretch-blit it into `r`. (Windows' LoadImage only
 * decodes BMP; GIF/JPEG would need a separate decoder -- a period-honest
 * limitation, since 1995 GIF used patented LZW.) */
static SmBool cv_draw_image(SmCanvas *cv, SmRect r, const char *path)
{
    GdiCanvas *g = (GdiCanvas *)cv->impl;
    HBITMAP bmp, oldbmp;
    HDC mem;
    BITMAP bm;
    if (path == NULL) { return SM_FALSE; }
    bmp = (HBITMAP)LoadImageA(NULL, path, IMAGE_BITMAP, 0, 0,
                              LR_LOADFROMFILE | LR_CREATEDIBSECTION);
    if (bmp == NULL) { return SM_FALSE; }
    mem = CreateCompatibleDC(g->hdc);
    oldbmp = (HBITMAP)SelectObject(mem, bmp);
    GetObject(bmp, sizeof(bm), &bm);
    SetStretchBltMode(g->hdc, COLORONCOLOR);
    StretchBlt(g->hdc, r.x, r.y, r.w, r.h, mem, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
    SelectObject(mem, oldbmp);
    DeleteDC(mem);
    DeleteObject(bmp);
    return SM_TRUE;
}

/* ---- window plumbing ---------------------------------------------------- */

static SmApp *g_app = NULL;

static SmKey vk_to_key(WPARAM vk)
{
    switch (vk) {
    case VK_LEFT:   return SM_KEY_LEFT;
    case VK_RIGHT:  return SM_KEY_RIGHT;
    case VK_UP:     return SM_KEY_UP;
    case VK_DOWN:   return SM_KEY_DOWN;
    case VK_RETURN: return SM_KEY_ENTER;
    case VK_ESCAPE: return SM_KEY_ESCAPE;
    case VK_BACK:   return SM_KEY_BACKSPACE;
    case VK_TAB:    return SM_KEY_TAB;
    case VK_DELETE: return SM_KEY_DELETE;
    default:        return SM_KEY_NONE;
    }
}

static void do_paint(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT cr;
    HDC mem;
    HBITMAP bmp, oldbmp;
    HFONT font, oldfont;
    GdiCanvas gc;
    SmCanvas cv;
    TEXTMETRICA tm;

    GetClientRect(hwnd, &cr);

    /* Double-buffer to avoid flicker on redraw. */
    mem = CreateCompatibleDC(hdc);
    bmp = CreateCompatibleBitmap(hdc, cr.right, cr.bottom);
    oldbmp = (HBITMAP)SelectObject(mem, bmp);

    font = CreateFontA(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0, ANSI_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                       FF_SWISS | VARIABLE_PITCH, "MS Sans Serif");
    oldfont = (HFONT)SelectObject(mem, font);
    GetTextMetricsA(mem, &tm);

    gc.hdc = mem; gc.font = font; gc.line_h = tm.tmHeight + tm.tmExternalLeading;
    cv.impl = &gc;
    cv.fill_rect = cv_fill_rect;
    cv.frame_rect = cv_frame_rect;
    cv.text = cv_text;
    cv.text_width = cv_text_width;
    cv.line_height = cv_line_height;
    cv.text_ex = cv_text_ex;
    cv.text_width_ex = cv_text_width_ex;
    cv.draw_image = cv_draw_image;

    if (g_app != NULL && g_app->on_paint != NULL) {
        g_app->on_paint(g_app->state, &cv, cr.right, cr.bottom);
    }

    BitBlt(hdc, 0, 0, cr.right, cr.bottom, mem, 0, 0, SRCCOPY);

    SelectObject(mem, oldfont);
    DeleteObject(font);
    SelectObject(mem, oldbmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    SmBool repaint = SM_FALSE;
    switch (msg) {
    case WM_PAINT:
        do_paint(hwnd);
        return 0;
    case WM_ERASEBKGND:
        return 1; /* handled in do_paint; suppress flicker */
    case WM_CHAR:
        if (g_app != NULL && g_app->on_char != NULL && wp >= 32 && wp < 127) {
            repaint = g_app->on_char(g_app->state, (int)wp);
        }
        break;
    case WM_KEYDOWN:
        if (g_app != NULL && g_app->on_key != NULL) {
            SmKey k = vk_to_key(wp);
            if (k != SM_KEY_NONE) {
                repaint = g_app->on_key(g_app->state, k);
            }
        }
        break;
    case WM_LBUTTONDOWN:
        if (g_app != NULL && g_app->on_click != NULL) {
            repaint = g_app->on_click(g_app->state,
                                      (int)(short)LOWORD(lp),
                                      (int)(short)HIWORD(lp));
        }
        break;
    case WM_SIZE:
        if (g_app != NULL && g_app->on_resize != NULL) {
            g_app->on_resize(g_app->state, (int)LOWORD(lp), (int)HIWORD(lp));
        }
        repaint = SM_TRUE;
        break;
    case WM_DROPFILES:
        if (g_app != NULL && g_app->on_open_file != NULL) {
            HDROP drop = (HDROP)wp;
            char path[MAX_PATH];
            if (DragQueryFileA(drop, 0, path, sizeof(path)) > 0) {
                repaint = g_app->on_open_file(g_app->state, path);
            }
            DragFinish(drop);
        }
        break;
    case WM_MOUSEWHEEL:
        if (g_app != NULL && g_app->on_scroll != NULL) {
            int notches = (short)HIWORD(wp) / WHEEL_DELTA;
            if (notches != 0) { repaint = g_app->on_scroll(g_app->state, notches); }
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcA(hwnd, msg, wp, lp);
    }
    if (repaint) {
        InvalidateRect(hwnd, NULL, FALSE);
    }
    return 0;
}

int sm_backend_run(const char *title, int width, int height, SmApp *app)
{
    WNDCLASSA wc;
    HWND hwnd;
    MSG msg;
    HINSTANCE inst = GetModuleHandleA(NULL);
    RECT want;

    g_app = app;

    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = NULL; /* we paint everything */
    wc.lpszClassName = "NaiveWebcel";
    if (!RegisterClassA(&wc)) {
        return 1;
    }

    want.left = 0; want.top = 0; want.right = width; want.bottom = height;
    AdjustWindowRect(&want, WS_OVERLAPPEDWINDOW, FALSE);

    hwnd = CreateWindowA(wc.lpszClassName, title, WS_OVERLAPPEDWINDOW,
                         CW_USEDEFAULT, CW_USEDEFAULT,
                         want.right - want.left, want.bottom - want.top,
                         NULL, NULL, inst, NULL);
    if (hwnd == NULL) {
        return 1;
    }
    DragAcceptFiles(hwnd, TRUE); /* let Explorer drag-drop a .nve file in */

    ShowWindow(hwnd, SW_SHOWNORMAL);
    UpdateWindow(hwnd);

    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return (int)msg.wParam;
}
