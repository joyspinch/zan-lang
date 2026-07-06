/* ide_main.c -- Zan IDE main window (Win32 + GDI).
 *
 * Lightweight IDE with:
 *   - Tabbed editor with syntax highlighting
 *   - File browser / project tree
 *   - Build output panel
 *   - F5 compile-and-run
 */

#ifdef _WIN32

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "editor.h"
#include "project.h"
#include "intellisense.h"
#include "debugger.h"

/* ---- Global state ---- */
static editor_t g_editor;
static project_tree_t g_project;
static intellisense_t g_intel;
static debugger_t g_debugger;
static HWND     g_hwnd;
static HFONT    g_code_font;
static int      g_char_width;
static int      g_char_height;
static int      g_line_num_width;    /* pixels for line number gutter */

/* Colors (dark theme) */
#define CLR_BG          RGB(30, 30, 30)
#define CLR_BG_GUTTER   RGB(40, 40, 40)
#define CLR_BG_TAB      RGB(45, 45, 45)
#define CLR_BG_TAB_ACT  RGB(30, 30, 30)
#define CLR_BG_SELECT   RGB(38, 79, 120)
#define CLR_BG_CURSOR   RGB(255, 255, 255)
#define CLR_BG_OUTPUT   RGB(25, 25, 25)
#define CLR_TEXT         RGB(212, 212, 212)
#define CLR_TEXT_DIM     RGB(128, 128, 128)
#define CLR_LINE_NUM    RGB(100, 100, 100)
#define CLR_KEYWORD     RGB(86, 156, 214)
#define CLR_TYPE        RGB(78, 201, 176)
#define CLR_STRING      RGB(206, 145, 120)
#define CLR_NUMBER      RGB(181, 206, 168)
#define CLR_COMMENT     RGB(106, 153, 85)
#define CLR_OPERATOR    RGB(212, 212, 212)
#define CLR_BUILTIN     RGB(220, 220, 170)
#define CLR_TAB_BORDER  RGB(60, 60, 60)

/* Menu IDs */
#define IDM_NEW         1001
#define IDM_OPEN        1002
#define IDM_SAVE        1003
#define IDM_SAVEAS      1004
#define IDM_EXIT        1005
#define IDM_UNDO        1006
#define IDM_REDO        1007
#define IDM_CUT         1008
#define IDM_COPY        1009
#define IDM_PASTE       1010
#define IDM_FIND        1011
#define IDM_BUILD       1012
#define IDM_RUN         1013
#define IDM_BUILDRUN    1014
#define IDM_ABOUT       1015
#define IDM_SELECTALL   1016
#define IDM_OPENPROJECT 1017
#define IDM_REFRESH     1018
#define IDM_DEBUG       1019
#define IDM_STOPDBG     1020
#define IDM_STEPOVER    1021
#define IDM_STEPINTO    1022
#define IDM_STEPOUT     1023
#define IDM_TOGGLEBP    1024
#define IDM_GOTODEF     1025
#define IDM_REPLACE     1026
#define IDM_GOTOLINE    1027
#define IDM_CLOSETAB    1028
#define IDM_FINDNEXT    1029
#define IDM_FORMAT      1030
#define IDM_GENDOC      1031
#define IDM_NEWWINDOW   1032

/* Layout constants */
#define TAB_HEIGHT      28
#define TOOLBAR_HEIGHT  30
#define PROJECT_WIDTH   200
#define OUTPUT_HEIGHT   150
#define SCROLLBAR_WIDTH 14
#define STATUS_HEIGHT   22
#define MINIMAP_WIDTH   60

/* Output panel buffer */
static char     g_output[65536];
static int      g_output_len;
/* static HWND     g_output_hwnd; */

/* Project path */
static char     g_project_path[MAX_PATH];

/* Find/Replace state */
static bool     g_find_active;
static bool     g_replace_active;
static char     g_find_text[256];
static char     g_replace_text[256];
static int      g_find_selected;  /* index of selected match in buffer */

/* Toolbar state */
static HWND     g_toolbar_hwnd;
static HFONT    g_toolbar_font;

/* ---- Forward declarations ---- */
static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
static void     create_menus(HWND hwnd);
static void     paint_editor(HDC hdc, RECT *rc);
static void     paint_tabs(HDC hdc, RECT *rc);
static void     paint_gutter(HDC hdc, RECT *rc);
static void     paint_code(HDC hdc, RECT *rc);
static void     paint_cursor(HDC hdc, RECT *rc);
static void     paint_status(HDC hdc, RECT *rc);
static void     paint_project(HDC hdc, RECT *rc);
static void     paint_autocomplete(HDC hdc, RECT *rc);
static void     paint_breakpoints(HDC hdc, RECT *rc);
static void     paint_output(HDC hdc, RECT *rc);
static void     do_open_file(void);
static void     do_save_file(void);
static void     do_save_as(void);
static void     do_build(void);
static void     do_run(void);
static void     do_build_and_run(void);
static void     do_open_project(void);
static void     append_output(const char *text);
static COLORREF syn_color(syn_kind_t kind);
static void     paint_toolbar(HDC hdc, RECT *rc);
static void     paint_find_bar(HDC hdc, RECT *rc);
static void     paint_minimap(HDC hdc, RECT *rc);
static void     do_find(void);
static void     do_replace(void);
static void     do_find_next(void);
static void     do_goto_line(void);
static void     do_close_tab(void);
static void     do_format(void);
static void     do_gen_doc(void);

/* ---- Entry point ---- */

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;

    editor_init(&g_editor);
    project_init(&g_project);
    intel_init(&g_intel);
    dbg_init(&g_debugger);

    /* Create monospace font */
    g_code_font = CreateFontW(
        -15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN,
        L"Consolas");

    /* Register window class */
    WNDCLASSEXW wc = {0};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance      = hInstance;
    wc.hCursor       = LoadCursor(NULL, IDC_IBEAM);
    wc.hbrBackground = CreateSolidBrush(CLR_BG);
    wc.lpszClassName = L"ZanIDE";
    wc.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
    RegisterClassExW(&wc);

    /* Create main window */
    g_hwnd = CreateWindowExW(
        0, L"ZanIDE", L"Zan IDE",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1200, 800,
        NULL, NULL, hInstance, NULL);

    create_menus(g_hwnd);
    ShowWindow(g_hwnd, nCmdShow);
    UpdateWindow(g_hwnd);

    /* Open file from command line if provided */
    if (lpCmdLine && lpCmdLine[0]) {
        editor_open_file(&g_editor, lpCmdLine);
        /* parse for intellisense */
        editor_tab_t *init_tab = editor_active(&g_editor);
        if (init_tab) {
            char *txt = pt_to_string(init_tab->buffer);
            intel_parse_file(&g_intel, init_tab->filepath, txt, pt_length(init_tab->buffer));
            free(txt);
        }
    } else {
        editor_new_file(&g_editor);
    }

    /* Message loop */
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    editor_destroy(&g_editor);
    DeleteObject(g_code_font);
    return (int)msg.wParam;
}

/* ---- Menu creation ---- */

static void create_menus(HWND hwnd) {
    HMENU hMenu = CreateMenu();
    HMENU hFile = CreatePopupMenu();
    HMENU hEdit = CreatePopupMenu();
    HMENU hBuild = CreatePopupMenu();
    HMENU hHelp = CreatePopupMenu();

    AppendMenuW(hFile, MF_STRING, IDM_NEW,    L"&New\tCtrl+N");
    AppendMenuW(hFile, MF_STRING, IDM_OPEN,   L"&Open...\tCtrl+O");
    AppendMenuW(hFile, MF_STRING, IDM_SAVE,   L"&Save\tCtrl+S");
    AppendMenuW(hFile, MF_STRING, IDM_SAVEAS, L"Save &As...\tCtrl+Shift+S");
    AppendMenuW(hFile, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hFile, MF_STRING, IDM_OPENPROJECT, L"Open &Project...");
    AppendMenuW(hFile, MF_STRING, IDM_REFRESH, L"&Refresh Project");
    AppendMenuW(hFile, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hFile, MF_STRING, IDM_EXIT,   L"E&xit\tAlt+F4");

    AppendMenuW(hEdit, MF_STRING, IDM_UNDO,      L"&Undo\tCtrl+Z");
    AppendMenuW(hEdit, MF_STRING, IDM_REDO,      L"&Redo\tCtrl+Y");
    AppendMenuW(hEdit, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hEdit, MF_STRING, IDM_CUT,       L"Cu&t\tCtrl+X");
    AppendMenuW(hEdit, MF_STRING, IDM_COPY,      L"&Copy\tCtrl+C");
    AppendMenuW(hEdit, MF_STRING, IDM_PASTE,     L"&Paste\tCtrl+V");
    AppendMenuW(hEdit, MF_STRING, IDM_SELECTALL, L"Select &All\tCtrl+A");
    AppendMenuW(hEdit, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hEdit, MF_STRING, IDM_FIND,      L"&Find...\tCtrl+F");
    AppendMenuW(hEdit, MF_STRING, IDM_REPLACE,   L"&Replace...\tCtrl+H");
    AppendMenuW(hEdit, MF_STRING, IDM_FINDNEXT,  L"Find &Next\tF3");
    AppendMenuW(hEdit, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hEdit, MF_STRING, IDM_GOTOLINE,  L"&Go to Line...\tCtrl+G");

    AppendMenuW(hBuild, MF_STRING, IDM_BUILD,    L"&Build\tCtrl+B");
    AppendMenuW(hBuild, MF_STRING, IDM_RUN,      L"&Run\tCtrl+R");
    AppendMenuW(hBuild, MF_STRING, IDM_BUILDRUN, L"Build && &Run\tF5");
    AppendMenuW(hBuild, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hBuild, MF_STRING, IDM_FORMAT,   L"&Format File\tCtrl+Shift+F");
    AppendMenuW(hBuild, MF_STRING, IDM_GENDOC,   L"Generate &Documentation");

    HMENU hDebug = CreatePopupMenu();
    AppendMenuW(hDebug, MF_STRING, IDM_DEBUG,    L"Start &Debugging\tF5");
    AppendMenuW(hDebug, MF_STRING, IDM_STOPDBG,  L"&Stop Debugging\tShift+F5");
    AppendMenuW(hDebug, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hDebug, MF_STRING, IDM_STEPOVER, L"Step &Over\tF10");
    AppendMenuW(hDebug, MF_STRING, IDM_STEPINTO, L"Step &Into\tF11");
    AppendMenuW(hDebug, MF_STRING, IDM_STEPOUT,  L"Step O&ut\tShift+F11");
    AppendMenuW(hDebug, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hDebug, MF_STRING, IDM_TOGGLEBP, L"Toggle &Breakpoint\tF9");

    AppendMenuW(hHelp, MF_STRING, IDM_ABOUT, L"&About Zan IDE");

    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hFile,  L"&File");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hEdit,  L"&Edit");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hBuild, L"&Build");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hDebug, L"&Debug");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hHelp,  L"&Help");

    SetMenu(hwnd, hMenu);
}

/* ---- Painting ---- */

static COLORREF syn_color(syn_kind_t kind) {
    switch (kind) {
        case SYN_KEYWORD:     return CLR_KEYWORD;
        case SYN_TYPE:        return CLR_TYPE;
        case SYN_STRING:      return CLR_STRING;
        case SYN_NUMBER:      return CLR_NUMBER;
        case SYN_COMMENT:     return CLR_COMMENT;
        case SYN_OPERATOR:    return CLR_OPERATOR;
        case SYN_BUILTIN:     return CLR_BUILTIN;
        case SYN_IDENTIFIER:  return CLR_TEXT;
        case SYN_PREPROCESSOR:return CLR_KEYWORD;
        default:              return CLR_TEXT;
    }
}

static void paint_tabs(HDC hdc, RECT *rc) {
    RECT tab_rc = { rc->left, rc->top + TOOLBAR_HEIGHT, rc->right, rc->top + TOOLBAR_HEIGHT + TAB_HEIGHT };

    /* tab bar background */
    HBRUSH br = CreateSolidBrush(CLR_BG_TAB);
    FillRect(hdc, &tab_rc, br);
    DeleteObject(br);

    SelectObject(hdc, g_code_font);
    SetBkMode(hdc, TRANSPARENT);

    int x = 0;
    for (int i = 0; i < g_editor.tab_count; i++) {
        editor_tab_t *tab = &g_editor.tabs[i];
        int tw = 120; /* tab width */

        RECT tr = { rc->left + x, rc->top, rc->left + x + tw, rc->top + TAB_HEIGHT };

        COLORREF bg = (i == g_editor.active_tab) ? CLR_BG_TAB_ACT : CLR_BG_TAB;
        br = CreateSolidBrush(bg);
        FillRect(hdc, &tr, br);
        DeleteObject(br);

        /* border */
        HPEN pen = CreatePen(PS_SOLID, 1, CLR_TAB_BORDER);
        HPEN old_pen = (HPEN)SelectObject(hdc, pen);
        MoveToEx(hdc, tr.right, tr.top, NULL);
        LineTo(hdc, tr.right, tr.bottom);
        SelectObject(hdc, old_pen);
        DeleteObject(pen);

        /* tab title */
        char title[80];
        snprintf(title, sizeof(title), "%s%s", tab->title,
                 tab->modified ? " *" : "");
        SetTextColor(hdc, (i == g_editor.active_tab) ? CLR_TEXT : CLR_TEXT_DIM);
        RECT text_rc = { tr.left + 8, tr.top + 4, tr.right - 4, tr.bottom - 2 };
        DrawTextA(hdc, title, -1, &text_rc, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

        x += tw;
    }
}

static void paint_gutter(HDC hdc, RECT *rc) {
    editor_tab_t *tab = editor_active(&g_editor);
    if (!tab) return;

    g_line_num_width = g_char_width * 5 + 8;

    int gutter_left = g_project.is_open ? rc->left + PROJECT_WIDTH + 1 : rc->left;
    RECT gutter_rc = {
        gutter_left, rc->top + TAB_HEIGHT,
        gutter_left + g_line_num_width,
        rc->bottom - OUTPUT_HEIGHT - STATUS_HEIGHT
    };

    HBRUSH br = CreateSolidBrush(CLR_BG_GUTTER);
    FillRect(hdc, &gutter_rc, br);
    DeleteObject(br);

    SelectObject(hdc, g_code_font);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, CLR_LINE_NUM);

    size_t first = tab->scroll_line;
    int rows = (gutter_rc.bottom - gutter_rc.top) / g_char_height;
    size_t total = pt_line_count(tab->buffer);

    for (int r = 0; r < rows && first + (size_t)r < total; r++) {
        char num[16];
        snprintf(num, sizeof(num), "%4d", (int)(first + (size_t)r + 1));
        int y = gutter_rc.top + r * g_char_height;
        RECT nr = { gutter_rc.left + 2, y, gutter_rc.right - 4, y + g_char_height };
        DrawTextA(hdc, num, -1, &nr, DT_RIGHT | DT_SINGLELINE);
    }
}

static void paint_code(HDC hdc, RECT *rc) {
    editor_tab_t *tab = editor_active(&g_editor);
    if (!tab) return;

    int left_offset = g_project.is_open ? PROJECT_WIDTH + 1 : 0;
    RECT code_rc = {
        rc->left + left_offset + g_line_num_width, rc->top + TAB_HEIGHT,
        rc->right, rc->bottom - OUTPUT_HEIGHT - STATUS_HEIGHT
    };

    /* code background */
    HBRUSH br = CreateSolidBrush(CLR_BG);
    FillRect(hdc, &code_rc, br);
    DeleteObject(br);

    SelectObject(hdc, g_code_font);
    SetBkMode(hdc, TRANSPARENT);

    size_t first = tab->scroll_line;
    int rows = (code_rc.bottom - code_rc.top) / g_char_height;
    size_t total = pt_line_count(tab->buffer);

    /* ensure syntax is up to date */
    editor_highlight_visible(&g_editor);

    for (int r = 0; r < rows && first + (size_t)r < total; r++) {
        size_t line = first + (size_t)r;
        int y = code_rc.top + r * g_char_height;

        /* get line text */
        size_t llen = pt_line_length(tab->buffer, line);
        char *buf = (char *)malloc(llen + 1);
        size_t ls = pt_line_start(tab->buffer, line);
        pt_get_text(tab->buffer, ls, llen, buf);
        buf[llen] = '\0';

        /* selection highlight */
        if (tab->sel_anchor_line >= 0) {
            size_t sel_start_line, sel_start_col, sel_end_line, sel_end_col;
            if ((size_t)tab->sel_anchor_line < tab->cursor_line ||
                ((size_t)tab->sel_anchor_line == tab->cursor_line &&
                 (size_t)tab->sel_anchor_col < tab->cursor_col)) {
                sel_start_line = (size_t)tab->sel_anchor_line;
                sel_start_col = (size_t)tab->sel_anchor_col;
                sel_end_line = tab->cursor_line;
                sel_end_col = tab->cursor_col;
            } else {
                sel_start_line = tab->cursor_line;
                sel_start_col = tab->cursor_col;
                sel_end_line = (size_t)tab->sel_anchor_line;
                sel_end_col = (size_t)tab->sel_anchor_col;
            }

            if (line >= sel_start_line && line <= sel_end_line) {
                int sx = (line == sel_start_line) ? (int)sel_start_col : 0;
                int ex = (line == sel_end_line) ? (int)sel_end_col : (int)llen;
                RECT sel_rc = {
                    code_rc.left + (sx - (int)tab->scroll_col) * g_char_width,
                    y,
                    code_rc.left + (ex - (int)tab->scroll_col) * g_char_width,
                    y + g_char_height
                };
                br = CreateSolidBrush(CLR_BG_SELECT);
                FillRect(hdc, &sel_rc, br);
                DeleteObject(br);
            }
        }

        /* draw syntax-highlighted spans */
        if (line < tab->syn_cache_count && tab->syn_cache[line].span_count > 0) {
            syn_line_t *sl = &tab->syn_cache[line];
            for (int s = 0; s < sl->span_count; s++) {
                int cs = sl->spans[s].col_start - (int)tab->scroll_col;
                int ce = sl->spans[s].col_end - (int)tab->scroll_col;
                if (ce <= 0) continue;
                if (cs < 0) cs = 0;

                SetTextColor(hdc, syn_color(sl->spans[s].kind));
                int x = code_rc.left + cs * g_char_width;
                int draw_len = sl->spans[s].col_end - sl->spans[s].col_start;
                int start_in_buf = sl->spans[s].col_start;
                if (start_in_buf < (int)llen && draw_len > 0) {
                    if (start_in_buf + draw_len > (int)llen)
                        draw_len = (int)llen - start_in_buf;
                    TextOutA(hdc, x, y, buf + start_in_buf, draw_len);
                }
            }
        } else {
            /* no highlighting — draw plain */
            SetTextColor(hdc, CLR_TEXT);
            int visible_start = (int)tab->scroll_col;
            if (visible_start < (int)llen) {
                TextOutA(hdc, code_rc.left, y,
                         buf + visible_start, (int)llen - visible_start);
            }
        }

        free(buf);
    }
}

static void paint_cursor(HDC hdc, RECT *rc) {
    editor_tab_t *tab = editor_active(&g_editor);
    if (!tab) return;

    int proj_off = g_project.is_open ? PROJECT_WIDTH + 1 : 0;
    int cx = rc->left + proj_off + g_line_num_width +
             (int)(tab->cursor_col - tab->scroll_col) * g_char_width;
    int cy = rc->top + TAB_HEIGHT +
             (int)(tab->cursor_line - tab->scroll_line) * g_char_height;

    if (cx >= rc->left + g_line_num_width &&
        cy >= rc->top + TAB_HEIGHT &&
        cy < rc->bottom - OUTPUT_HEIGHT - STATUS_HEIGHT) {
        RECT cursor_rc = { cx, cy, cx + 2, cy + g_char_height };
        HBRUSH br = CreateSolidBrush(CLR_BG_CURSOR);
        FillRect(hdc, &cursor_rc, br);
        DeleteObject(br);
    }
}


static void paint_project(HDC hdc, RECT *rc) {
    if (!g_project.is_open) return;

    RECT proj_rc = {
        rc->left, rc->top + TAB_HEIGHT,
        rc->left + PROJECT_WIDTH,
        rc->bottom - OUTPUT_HEIGHT - STATUS_HEIGHT
    };

    /* background */
    HBRUSH br = CreateSolidBrush(RGB(37, 37, 38));
    FillRect(hdc, &proj_rc, br);
    DeleteObject(br);

    /* separator line */
    HPEN pen = CreatePen(PS_SOLID, 1, CLR_TAB_BORDER);
    HPEN old_pen = (HPEN)SelectObject(hdc, pen);
    MoveToEx(hdc, proj_rc.right, proj_rc.top, NULL);
    LineTo(hdc, proj_rc.right, proj_rc.bottom);
    SelectObject(hdc, old_pen);
    DeleteObject(pen);

    SelectObject(hdc, g_code_font);
    SetBkMode(hdc, TRANSPARENT);

    /* project name header */
    SetTextColor(hdc, CLR_TEXT);
    RECT header_rc = { proj_rc.left + 8, proj_rc.top + 4, proj_rc.right - 4, proj_rc.top + 20 };
    DrawTextA(hdc, g_project.root_name, -1, &header_rc, DT_LEFT | DT_SINGLELINE);

    /* file entries */
    int y = proj_rc.top + 24;
    int row_height = g_char_height > 0 ? g_char_height : 16;
    int vis = 0;

    for (int i = 0; i < g_project.entry_count && y < proj_rc.bottom; i++) {
        project_entry_t *e = &g_project.entries[i];
        if (!e->visible) continue;
        if (vis < g_project.scroll) { vis++; continue; }

        /* selection highlight */
        if (i == g_project.selected) {
            RECT sel_rc = { proj_rc.left, y, proj_rc.right, y + row_height };
            br = CreateSolidBrush(RGB(38, 79, 120));
            FillRect(hdc, &sel_rc, br);
            DeleteObject(br);
        }

        /* indent + icon prefix */
        int indent = e->depth * 12 + 8;
        char display[300];
        if (e->type == ENTRY_DIR) {
            snprintf(display, sizeof(display), "%s %s",
                     e->expanded ? "-" : "+", e->name);
            SetTextColor(hdc, RGB(220, 220, 170));
        } else {
            /* color by extension */
            const char *ext = strrchr(e->name, '.');
            if (ext && (_stricmp(ext, ".zan") == 0))
                SetTextColor(hdc, CLR_KEYWORD);
            else if (ext && (_stricmp(ext, ".c") == 0 || _stricmp(ext, ".h") == 0))
                SetTextColor(hdc, CLR_TYPE);
            else
                SetTextColor(hdc, CLR_TEXT_DIM);
            snprintf(display, sizeof(display), "  %s", e->name);
        }

        RECT text_rc = { proj_rc.left + indent, y, proj_rc.right - 4, y + row_height };
        DrawTextA(hdc, display, -1, &text_rc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

        y += row_height;
        vis++;
    }
}


static void paint_autocomplete(HDC hdc, RECT *rc) {
    if (!g_intel.completion_active || g_intel.completion_count == 0) return;

    editor_tab_t *tab = editor_active(&g_editor);
    if (!tab) return;

    int proj_off = g_project.is_open ? PROJECT_WIDTH + 1 : 0;
    int popup_x = rc->left + proj_off + g_line_num_width +
                  (int)(tab->cursor_col - tab->scroll_col) * g_char_width;
    int popup_y = rc->top + TAB_HEIGHT +
                  (int)(tab->cursor_line - tab->scroll_line + 1) * g_char_height;

    int popup_w = 300;
    int item_h = g_char_height > 0 ? g_char_height + 2 : 18;
    int popup_h = g_intel.completion_count * item_h + 4;
    if (popup_h > 200) popup_h = 200;

    /* popup background */
    RECT popup_rc = { popup_x, popup_y, popup_x + popup_w, popup_y + popup_h };
    HBRUSH br = CreateSolidBrush(RGB(37, 37, 38));
    FillRect(hdc, &popup_rc, br);
    DeleteObject(br);

    /* border */
    HPEN pen = CreatePen(PS_SOLID, 1, CLR_TAB_BORDER);
    HPEN old_pen = (HPEN)SelectObject(hdc, pen);
    MoveToEx(hdc, popup_rc.left, popup_rc.top, NULL);
    LineTo(hdc, popup_rc.right, popup_rc.top);
    LineTo(hdc, popup_rc.right, popup_rc.bottom);
    LineTo(hdc, popup_rc.left, popup_rc.bottom);
    LineTo(hdc, popup_rc.left, popup_rc.top);
    SelectObject(hdc, old_pen);
    DeleteObject(pen);

    SelectObject(hdc, g_code_font);
    SetBkMode(hdc, TRANSPARENT);

    int y = popup_y + 2;
    for (int i = 0; i < g_intel.completion_count && y + item_h <= popup_y + popup_h; i++) {
        completion_t *c = &g_intel.completions[i];

        /* highlight selected */
        if (i == g_intel.completion_selected) {
            RECT sel_rc = { popup_x + 1, y, popup_x + popup_w - 1, y + item_h };
            br = CreateSolidBrush(RGB(38, 79, 120));
            FillRect(hdc, &sel_rc, br);
            DeleteObject(br);
        }

        /* icon color by kind */
        COLORREF icon_clr = CLR_TEXT;
        char icon = ' ';
        switch (c->kind) {
            case ISYM_METHOD:    icon = 'M'; icon_clr = RGB(220, 220, 170); break;
            case ISYM_FIELD:     icon = 'F'; icon_clr = CLR_TYPE; break;
            case ISYM_PROPERTY:  icon = 'P'; icon_clr = CLR_TYPE; break;
            case ISYM_CLASS:     icon = 'C'; icon_clr = CLR_KEYWORD; break;
            case ISYM_KEYWORD:   icon = 'K'; icon_clr = CLR_KEYWORD; break;
            case ISYM_TYPE:      icon = 'T'; icon_clr = CLR_TYPE; break;
            default:             icon = ' '; break;
        }

        /* draw icon */
        SetTextColor(hdc, icon_clr);
        char icon_str[2] = { icon, '\0' };
        TextOutA(hdc, popup_x + 4, y + 1, icon_str, 1);

        /* draw label */
        SetTextColor(hdc, CLR_TEXT);
        TextOutA(hdc, popup_x + 20, y + 1, c->label, (int)strlen(c->label));

        /* draw detail (dimmed) */
        SetTextColor(hdc, CLR_TEXT_DIM);
        int detail_x = popup_x + 160;
        int max_detail = (popup_w - 164) / (g_char_width > 0 ? g_char_width : 8);
        if (max_detail > 0 && c->detail[0]) {
            int dlen = (int)strlen(c->detail);
            if (dlen > max_detail) dlen = max_detail;
            TextOutA(hdc, detail_x, y + 1, c->detail, dlen);
        }

        y += item_h;
    }
}

static void paint_breakpoints(HDC hdc, RECT *rc) {
    editor_tab_t *tab = editor_active(&g_editor);
    if (!tab || g_debugger.bp_count == 0) return;

    int gutter_left = g_project.is_open ? rc->left + PROJECT_WIDTH + 1 : rc->left;
    size_t first = tab->scroll_line;
    int rows = (rc->bottom - TAB_HEIGHT - OUTPUT_HEIGHT - STATUS_HEIGHT) / g_char_height;

    for (int r = 0; r < rows; r++) {
        int line = (int)(first + (size_t)r);
        if (dbg_has_breakpoint(&g_debugger, tab->filepath, line)) {
            int y = rc->top + TAB_HEIGHT + r * g_char_height;
            /* red circle for breakpoint */
            HBRUSH br = CreateSolidBrush(RGB(220, 50, 50));
            RECT bp_rc = { gutter_left + 2, y + 2, gutter_left + 12, y + g_char_height - 2 };
            FillRect(hdc, &bp_rc, br);
            DeleteObject(br);
        }
        if (dbg_is_current_line(&g_debugger, tab->filepath, line)) {
            int y = rc->top + TAB_HEIGHT + r * g_char_height;
            /* yellow arrow for current line */
            SetTextColor(hdc, RGB(255, 200, 0));
            TextOutA(hdc, gutter_left + 1, y, ">", 1);
        }
    }
}

static void paint_output(HDC hdc, RECT *rc) {
    RECT out_rc = {
        rc->left, rc->bottom - OUTPUT_HEIGHT - STATUS_HEIGHT,
        rc->right, rc->bottom - STATUS_HEIGHT
    };

    /* separator */
    HPEN pen = CreatePen(PS_SOLID, 1, CLR_TAB_BORDER);
    HPEN old_pen = (HPEN)SelectObject(hdc, pen);
    MoveToEx(hdc, out_rc.left, out_rc.top, NULL);
    LineTo(hdc, out_rc.right, out_rc.top);
    SelectObject(hdc, old_pen);
    DeleteObject(pen);

    out_rc.top += 1;
    HBRUSH br = CreateSolidBrush(CLR_BG_OUTPUT);
    FillRect(hdc, &out_rc, br);
    DeleteObject(br);

    /* "Output" label */
    SelectObject(hdc, g_code_font);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, CLR_TEXT_DIM);
    RECT label_rc = { out_rc.left + 8, out_rc.top + 2, out_rc.right, out_rc.top + 16 };
    DrawTextA(hdc, "Output", -1, &label_rc, DT_LEFT | DT_SINGLELINE);

    /* output text */
    SetTextColor(hdc, CLR_TEXT);
    RECT text_rc = { out_rc.left + 8, out_rc.top + 18, out_rc.right - 8, out_rc.bottom - 4 };
    DrawTextA(hdc, g_output, g_output_len, &text_rc,
              DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
}

static void paint_status(HDC hdc, RECT *rc) {
    RECT status_rc = {
        rc->left, rc->bottom - STATUS_HEIGHT,
        rc->right, rc->bottom
    };

    HBRUSH br = CreateSolidBrush(RGB(0, 122, 204));
    FillRect(hdc, &status_rc, br);
    DeleteObject(br);

    SelectObject(hdc, g_code_font);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));

    editor_tab_t *tab = editor_active(&g_editor);
    char status[256];
    if (tab) {
        const char *dbg_str = "";
        if (g_debugger.state == DBG_RUNNING) dbg_str = "  |  Debugging";
        else if (g_debugger.state == DBG_PAUSED) dbg_str = "  |  Paused";
        snprintf(status, sizeof(status), " Ln %d, Col %d  |  %s  |  Zan%s",
                 (int)tab->cursor_line + 1, (int)tab->cursor_col + 1,
                 tab->modified ? "Modified" : "Saved", dbg_str);
    } else {
        snprintf(status, sizeof(status), " Ready  |  Zan IDE");
    }

    RECT text_rc = { status_rc.left + 4, status_rc.top + 2,
                     status_rc.right - 4, status_rc.bottom - 2 };
    DrawTextA(hdc, status, -1, &text_rc, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
}

/* ---- Toolbar ---- */

static void paint_toolbar(HDC hdc, RECT *rc) {
    RECT tb_rc = { rc->left, rc->top, rc->right, rc->top + TOOLBAR_HEIGHT };

    /* toolbar background */
    HBRUSH br = CreateSolidBrush(RGB(37, 37, 38));
    FillRect(hdc, &tb_rc, br);
    DeleteObject(br);

    /* separator line */
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(60, 60, 60));
    HPEN old_pen = (HPEN)SelectObject(hdc, pen);
    MoveToEx(hdc, 0, TOOLBAR_HEIGHT - 1, NULL);
    LineTo(hdc, rc->right, TOOLBAR_HEIGHT - 1);
    SelectObject(hdc, old_pen);
    DeleteObject(pen);

    /* toolbar buttons: [New] [Open] [Save] | [Build] [Run] | [Find] */
    static const struct { const char *label; int cmd; } buttons[] = {
        {"New",  IDM_NEW}, {"Open", IDM_OPEN}, {"Save", IDM_SAVE},
        {"|", 0},
        {"Build", IDM_BUILD}, {"Run", IDM_RUN}, {"F5", IDM_BUILDRUN},
        {"|", 0},
        {"Find", IDM_FIND}, {"Replace", IDM_REPLACE},
        {"|", 0},
        {"Format", IDM_FORMAT},
    };
    int bcount = sizeof(buttons) / sizeof(buttons[0]);

    SelectObject(hdc, g_code_font);
    SetBkMode(hdc, TRANSPARENT);

    int bx = 4;
    for (int i = 0; i < bcount; i++) {
        if (buttons[i].label[0] == '|') {
            /* separator */
            HPEN sp = CreatePen(PS_SOLID, 1, RGB(80, 80, 80));
            HPEN op2 = (HPEN)SelectObject(hdc, sp);
            MoveToEx(hdc, bx + 4, 4, NULL);
            LineTo(hdc, bx + 4, TOOLBAR_HEIGHT - 6);
            SelectObject(hdc, op2);
            DeleteObject(sp);
            bx += 10;
            continue;
        }
        int tw = (int)strlen(buttons[i].label) * g_char_width + 12;
        RECT btn_rc = { bx, 3, bx + tw, TOOLBAR_HEIGHT - 4 };

        HBRUSH bbr = CreateSolidBrush(RGB(55, 55, 58));
        FillRect(hdc, &btn_rc, bbr);
        DeleteObject(bbr);

        SetTextColor(hdc, RGB(200, 200, 200));
        DrawTextA(hdc, buttons[i].label, -1, &btn_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        bx += tw + 4;
    }
}

/* ---- Find/Replace Bar ---- */

static void paint_find_bar(HDC hdc, RECT *rc) {
    if (!g_find_active && !g_replace_active) return;

    int bar_height = g_replace_active ? 56 : 28;
    RECT bar_rc = {
        rc->left + (g_project.is_open ? PROJECT_WIDTH + 1 : 0),
        rc->top + TOOLBAR_HEIGHT + TAB_HEIGHT,
        rc->right - MINIMAP_WIDTH,
        rc->top + TOOLBAR_HEIGHT + TAB_HEIGHT + bar_height
    };

    HBRUSH br = CreateSolidBrush(RGB(60, 60, 60));
    FillRect(hdc, &bar_rc, br);
    DeleteObject(br);

    SelectObject(hdc, g_code_font);
    SetBkMode(hdc, TRANSPARENT);

    /* Find field */
    SetTextColor(hdc, RGB(180, 180, 180));
    RECT label_rc = { bar_rc.left + 8, bar_rc.top + 4, bar_rc.left + 50, bar_rc.top + 24 };
    DrawTextA(hdc, "Find:", -1, &label_rc, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    /* Find text box */
    RECT find_rc = { bar_rc.left + 52, bar_rc.top + 4, bar_rc.right - 80, bar_rc.top + 24 };
    HBRUSH fbr = CreateSolidBrush(RGB(30, 30, 30));
    FillRect(hdc, &find_rc, fbr);
    DeleteObject(fbr);
    /* border */
    HPEN fp = CreatePen(PS_SOLID, 1, RGB(0, 122, 204));
    HPEN ofp = (HPEN)SelectObject(hdc, fp);
    MoveToEx(hdc, find_rc.left, find_rc.top, NULL);
    LineTo(hdc, find_rc.right, find_rc.top);
    LineTo(hdc, find_rc.right, find_rc.bottom);
    LineTo(hdc, find_rc.left, find_rc.bottom);
    LineTo(hdc, find_rc.left, find_rc.top);
    SelectObject(hdc, ofp);
    DeleteObject(fp);

    SetTextColor(hdc, CLR_TEXT);
    RECT ftx_rc = { find_rc.left + 4, find_rc.top + 2, find_rc.right - 4, find_rc.bottom - 2 };
    DrawTextA(hdc, g_find_text, -1, &ftx_rc, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    /* Replace field */
    if (g_replace_active) {
        RECT rlabel_rc = { bar_rc.left + 8, bar_rc.top + 30, bar_rc.left + 50, bar_rc.top + 50 };
        SetTextColor(hdc, RGB(180, 180, 180));
        DrawTextA(hdc, "Repl:", -1, &rlabel_rc, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

        RECT repl_rc = { bar_rc.left + 52, bar_rc.top + 30, bar_rc.right - 80, bar_rc.top + 50 };
        HBRUSH rbr = CreateSolidBrush(RGB(30, 30, 30));
        FillRect(hdc, &repl_rc, rbr);
        DeleteObject(rbr);

        HPEN rp = CreatePen(PS_SOLID, 1, RGB(100, 100, 100));
        HPEN orp = (HPEN)SelectObject(hdc, rp);
        MoveToEx(hdc, repl_rc.left, repl_rc.top, NULL);
        LineTo(hdc, repl_rc.right, repl_rc.top);
        LineTo(hdc, repl_rc.right, repl_rc.bottom);
        LineTo(hdc, repl_rc.left, repl_rc.bottom);
        LineTo(hdc, repl_rc.left, repl_rc.top);
        SelectObject(hdc, orp);
        DeleteObject(rp);

        SetTextColor(hdc, CLR_TEXT);
        RECT rtx_rc = { repl_rc.left + 4, repl_rc.top + 2, repl_rc.right - 4, repl_rc.bottom - 2 };
        DrawTextA(hdc, g_replace_text, -1, &rtx_rc, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    }
}

/* ---- Minimap ---- */

static void paint_minimap(HDC hdc, RECT *rc) {
    editor_tab_t *tab = editor_active(&g_editor);
    if (!tab) return;

    int mm_left = rc->right - MINIMAP_WIDTH;
    int mm_top = rc->top + TOOLBAR_HEIGHT + TAB_HEIGHT;
    int mm_bottom = rc->bottom - OUTPUT_HEIGHT - STATUS_HEIGHT;

    RECT mm_rc = { mm_left, mm_top, rc->right, mm_bottom };

    /* minimap background */
    HBRUSH br = CreateSolidBrush(RGB(25, 25, 25));
    FillRect(hdc, &mm_rc, br);
    DeleteObject(br);

    /* separator line */
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(50, 50, 50));
    HPEN old_pen = (HPEN)SelectObject(hdc, pen);
    MoveToEx(hdc, mm_left, mm_top, NULL);
    LineTo(hdc, mm_left, mm_bottom);
    SelectObject(hdc, old_pen);
    DeleteObject(pen);

    /* draw minimap lines as tiny colored dots/lines */
    size_t total_lines = pt_line_count(tab->buffer);
    if (total_lines == 0) return;

    int mm_height = mm_bottom - mm_top;
    double scale = (double)mm_height / (double)(total_lines > 0 ? total_lines : 1);
    if (scale > 2.0) scale = 2.0;

    /* draw visible region indicator */
    int vis_top = mm_top + (int)(tab->scroll_line * scale);
    int vis_height = (int)(g_editor.view_rows * scale);
    if (vis_height < 4) vis_height = 4;
    RECT vis_rc = { mm_left + 1, vis_top, rc->right, vis_top + vis_height };
    HBRUSH vbr = CreateSolidBrush(RGB(50, 50, 70));
    FillRect(hdc, &vis_rc, vbr);
    DeleteObject(vbr);

    /* draw line representations */
    size_t max_show = (size_t)(mm_height / scale);
    if (max_show > total_lines) max_show = total_lines;

    for (size_t i = 0; i < max_show; i++) {
        int y = mm_top + (int)(i * scale);
        if (y >= mm_bottom) break;

        size_t llen = pt_line_length(tab->buffer, i);
        if (llen == 0) continue;

        int draw_len = (int)(llen * (MINIMAP_WIDTH - 4) / 120);
        if (draw_len > MINIMAP_WIDTH - 4) draw_len = MINIMAP_WIDTH - 4;
        if (draw_len < 1) draw_len = 1;

        HPEN lp = CreatePen(PS_SOLID, 1, RGB(100, 100, 110));
        HPEN olp = (HPEN)SelectObject(hdc, lp);
        MoveToEx(hdc, mm_left + 3, y, NULL);
        LineTo(hdc, mm_left + 3 + draw_len, y);
        SelectObject(hdc, olp);
        DeleteObject(lp);
    }
}

static void paint_editor(HDC hdc, RECT *rc) {
    /* measure font if not done */
    if (g_char_width == 0) {
        SelectObject(hdc, g_code_font);
        TEXTMETRIC tm;
        GetTextMetrics(hdc, &tm);
        g_char_width = tm.tmAveCharWidth;
        g_char_height = tm.tmHeight;
        g_editor.view_cols = (rc->right - 60 - MINIMAP_WIDTH) / g_char_width;
        g_editor.view_rows = (rc->bottom - TOOLBAR_HEIGHT - TAB_HEIGHT - OUTPUT_HEIGHT - STATUS_HEIGHT) / g_char_height;
    }

    paint_toolbar(hdc, rc);
    paint_tabs(hdc, rc);
    paint_project(hdc, rc);
    paint_gutter(hdc, rc);
    paint_code(hdc, rc);
    paint_cursor(hdc, rc);
    paint_minimap(hdc, rc);
    paint_find_bar(hdc, rc);
    paint_output(hdc, rc);
    paint_breakpoints(hdc, rc);
    paint_autocomplete(hdc, rc);
    paint_status(hdc, rc);
}

/* ---- File dialogs ---- */

static void do_open_file(void) {
    char filename[MAX_PATH] = {0};
    OPENFILENAMEA ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = "Zan Files (*.zan)\0*.zan\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (GetOpenFileNameA(&ofn)) {
        editor_open_file(&g_editor, filename);
        InvalidateRect(g_hwnd, NULL, TRUE);
    }
}

static void do_save_file(void) {
    editor_tab_t *tab = editor_active(&g_editor);
    if (!tab) return;
    if (tab->is_new) {
        do_save_as();
    } else {
        editor_save(&g_editor);
        InvalidateRect(g_hwnd, NULL, TRUE);
    }
}

static void do_save_as(void) {
    char filename[MAX_PATH] = {0};
    OPENFILENAMEA ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = "Zan Files (*.zan)\0*.zan\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT;
    ofn.lpstrDefExt = "zan";

    if (GetSaveFileNameA(&ofn)) {
        editor_save_as(&g_editor, filename);
        InvalidateRect(g_hwnd, NULL, TRUE);
    }
}

/* ---- Build integration ---- */

static void append_output(const char *text) {
    int len = (int)strlen(text);
    if (g_output_len + len < (int)sizeof(g_output) - 1) {
        memcpy(g_output + g_output_len, text, (size_t)len);
        g_output_len += len;
        g_output[g_output_len] = '\0';
    }
}

static void do_build(void) {
    editor_tab_t *tab = editor_active(&g_editor);
    if (!tab || tab->is_new) {
        append_output("Error: Save file before building.\n");
        InvalidateRect(g_hwnd, NULL, TRUE);
        return;
    }

    /* save first */
    editor_save(&g_editor);

    g_output_len = 0;
    g_output[0] = '\0';
    append_output("Building: ");
    append_output(tab->filepath);
    append_output("\n");

    /* construct build command */
    char cmd[2048];
    char out_path[MAX_PATH];
    strncpy(out_path, tab->filepath, MAX_PATH - 5);
    /* replace .zan with .exe */
    char *dot = strrchr(out_path, '.');
    if (dot) strcpy(dot, ".exe");
    else strcat(out_path, ".exe");

    snprintf(cmd, sizeof(cmd), "zanc \"%s\" -o \"%s\" 2>&1",
             tab->filepath, out_path);

    FILE *p = _popen(cmd, "r");
    if (p) {
        char line[1024];
        while (fgets(line, sizeof(line), p)) {
            append_output(line);
        }
        int rc = _pclose(p);
        if (rc == 0) {
            append_output("Build successful.\n");
        } else {
            append_output("Build failed.\n");
        }
    } else {
        append_output("Error: Could not run zanc compiler.\n");
    }

    InvalidateRect(g_hwnd, NULL, TRUE);
}

static void do_run(void) {
    editor_tab_t *tab = editor_active(&g_editor);
    if (!tab || tab->is_new) return;

    char exe_path[MAX_PATH];
    strncpy(exe_path, tab->filepath, MAX_PATH - 5);
    char *dot = strrchr(exe_path, '.');
    if (dot) strcpy(dot, ".exe");
    else strcat(exe_path, ".exe");

    append_output("\nRunning: ");
    append_output(exe_path);
    append_output("\n");

    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "\"%s\" 2>&1", exe_path);

    FILE *p = _popen(cmd, "r");
    if (p) {
        char line[1024];
        while (fgets(line, sizeof(line), p)) {
            append_output(line);
        }
        int rc = _pclose(p);
        char result[64];
        snprintf(result, sizeof(result), "\n[Exit code: %d]\n", rc);
        append_output(result);
    } else {
        append_output("Error: Could not run program.\n");
    }

    InvalidateRect(g_hwnd, NULL, TRUE);
}

static void do_build_and_run(void) {
    do_build();
    /* check if build succeeded */
    if (strstr(g_output, "Build successful")) {
        do_run();
    }
}

/* ---- Window procedure ---- */


static void do_open_project(void) {
    /* Use SHBrowseForFolder to pick a directory */
    BROWSEINFOA bi = {0};
    bi.hwndOwner = g_hwnd;
    bi.lpszTitle = "Select Project Folder";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_USENEWUI;

    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (pidl) {
        char path[MAX_PATH];
        if (SHGetPathFromIDListA(pidl, path)) {
            project_init(&g_project);
    intel_init(&g_intel);
    dbg_init(&g_debugger);
            project_open(&g_project, path);
            strncpy(g_project_path, path, MAX_PATH - 1);
        }
        CoTaskMemFree(pidl);
        InvalidateRect(g_hwnd, NULL, TRUE);
    }
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        SetTimer(hwnd, 1, 500, NULL); /* cursor blink timer */
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);

        /* double buffer */
        HDC mem_dc = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HBITMAP old_bmp = (HBITMAP)SelectObject(mem_dc, bmp);

        paint_editor(mem_dc, &rc);

        BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem_dc, 0, 0, SRCCOPY);

        SelectObject(mem_dc, old_bmp);
        DeleteObject(bmp);
        DeleteDC(mem_dc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_SIZE: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        if (g_char_width > 0) {
            g_editor.view_cols = (rc.right - g_line_num_width - MINIMAP_WIDTH) / g_char_width;
            g_editor.view_rows = (rc.bottom - TOOLBAR_HEIGHT - TAB_HEIGHT - OUTPUT_HEIGHT - STATUS_HEIGHT) / g_char_height;
        }
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    }

    case WM_TIMER:
        /* cursor blink — just repaint cursor area */
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_KEYDOWN: {
        bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

        if (shift) editor_start_selection(&g_editor);

        /* handle autocomplete keys first */
        if (g_intel.completion_active) {
            if (wParam == VK_UP) { intel_select_up(&g_intel); InvalidateRect(hwnd, NULL, FALSE); return 0; }
            if (wParam == VK_DOWN) { intel_select_down(&g_intel); InvalidateRect(hwnd, NULL, FALSE); return 0; }
            if (wParam == VK_RETURN || wParam == VK_TAB) {
                const char *ins = intel_accept(&g_intel);
                if (ins) {
                    /* delete the prefix and insert the full word */
                    editor_tab_t *atab = editor_active(&g_editor);
                    if (atab) {
                        size_t plen = strlen(g_intel.current_word);
                        /* for now just insert remaining */
                        editor_insert_text(&g_editor, ins + plen, strlen(ins) - plen);
                    }
                }
                intel_dismiss(&g_intel);
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            if (wParam == VK_ESCAPE) { intel_dismiss(&g_intel); g_find_active = false; g_replace_active = false; InvalidateRect(hwnd, NULL, FALSE); return 0; }
        }

        switch (wParam) {
        case VK_LEFT:
            if (ctrl) editor_move_word_left(&g_editor);
            else editor_move_left(&g_editor);
            break;
        case VK_RIGHT:
            if (ctrl) editor_move_word_right(&g_editor);
            else editor_move_right(&g_editor);
            break;
        case VK_UP:     editor_move_up(&g_editor); break;
        case VK_DOWN:   editor_move_down(&g_editor); break;
        case VK_HOME:   editor_move_home(&g_editor); break;
        case VK_END:    editor_move_end(&g_editor); break;
        case VK_PRIOR:  editor_move_page_up(&g_editor); break;
        case VK_NEXT:   editor_move_page_down(&g_editor); break;
        case VK_DELETE: editor_delete_char(&g_editor); break;
        case VK_F12: {
            /* Go to definition */
            editor_tab_t *gtab = editor_active(&g_editor);
            if (gtab) {
                size_t llen = pt_line_length(gtab->buffer, gtab->cursor_line);
                char *lbuf = (char *)malloc(llen + 1);
                size_t ls = pt_line_start(gtab->buffer, gtab->cursor_line);
                pt_get_text(gtab->buffer, ls, llen, lbuf);
                lbuf[llen] = '\0';
                int ws = (int)gtab->cursor_col;
                while (ws > 0 && (isalnum((unsigned char)lbuf[ws-1]) || lbuf[ws-1] == '_')) ws--;
                int we = (int)gtab->cursor_col;
                while (we < (int)llen && (isalnum((unsigned char)lbuf[we]) || lbuf[we] == '_')) we++;
                char word[128] = {0};
                int wl = we - ws;
                if (wl > 0 && wl < 127) memcpy(word, lbuf + ws, (size_t)wl);
                goto_def_t gd = intel_goto_def(&g_intel, word);
                if (gd.found) {
                    editor_open_file(&g_editor, gd.file);
                    editor_tab_t *nt = editor_active(&g_editor);
                    if (nt) { nt->cursor_line = (size_t)gd.line; nt->cursor_col = (size_t)gd.col;
                              editor_ensure_cursor_visible(&g_editor); }
                }
                free(lbuf);
            }
            break;
        }
        case VK_BACK:   editor_backspace(&g_editor); break;
        case VK_RETURN: editor_insert_newline(&g_editor); break;
        case VK_TAB:    editor_indent(&g_editor); break;
        case VK_F5:
            if (shift) dbg_stop(&g_debugger);
            else if (g_debugger.state == DBG_PAUSED) dbg_continue(&g_debugger);
            else do_build_and_run();
            break;
        case VK_F9:     {
            editor_tab_t *t = editor_active(&g_editor);
            if (t) dbg_toggle_breakpoint(&g_debugger, t->filepath, (int)t->cursor_line);
            break;
        }
        case VK_F10:    dbg_step_over(&g_debugger); break;
        case VK_F3:     do_find_next(); break;
        case VK_F11:
            if (shift) dbg_step_out(&g_debugger);
            else dbg_step_into(&g_debugger);
            break;
        default:
            if (ctrl) {
                switch (wParam) {
                case 'N': editor_new_file(&g_editor); break;
                case 'O': do_open_file(); break;
                case 'S':
                    if (shift) do_save_as();
                    else do_save_file();
                    break;
                case 'Z': editor_undo(&g_editor); break;
                case 'Y': editor_redo(&g_editor); break;
                case 'X': editor_cut(&g_editor); break;
                case 'C': editor_copy(&g_editor); break;
                case 'V': editor_paste(&g_editor); break;
                case 'A': editor_select_all(&g_editor); break;
                case 'B': do_build(); break;
                case VK_SPACE: {
                    /* trigger autocomplete */
                    editor_tab_t *atab = editor_active(&g_editor);
                    if (atab) {
                        /* get word at cursor */
                        size_t llen = pt_line_length(atab->buffer, atab->cursor_line);
                        char *line_buf = (char *)malloc(llen + 1);
                        size_t ls = pt_line_start(atab->buffer, atab->cursor_line);
                        pt_get_text(atab->buffer, ls, llen, line_buf);
                        line_buf[llen] = '\0';
                        /* find word start */
                        int ws = (int)atab->cursor_col;
                        while (ws > 0 && (isalnum((unsigned char)line_buf[ws-1]) || line_buf[ws-1] == '_')) ws--;
                        int wlen = (int)atab->cursor_col - ws;
                        char prefix[128] = {0};
                        if (wlen > 0 && wlen < 127) memcpy(prefix, line_buf + ws, (size_t)wlen);
                        intel_complete(&g_intel, prefix, NULL);
                        free(line_buf);
                    }
                    break;
                }
                case 'R': do_run(); break;
                case 'F':
                    if (shift) do_format();
                    else do_find();
                    break;
                case 'H': do_replace(); break;
                case 'G': do_goto_line(); break;
                case 'W': do_close_tab(); break;
                }
            }
            break;
        }
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_CHAR: {
        char ch = (char)wParam;
        if (g_find_active) {
            /* route typing to find bar */
            if (ch == 27) { /* ESC */
                g_find_active = false;
                g_replace_active = false;
            } else if (ch == 13) { /* Enter */
                do_find_next();
            } else if (ch == 8) { /* Backspace */
                int len = (int)strlen(g_find_text);
                if (len > 0) g_find_text[len - 1] = '\0';
            } else if (ch >= 32 && ch < 127) {
                int len = (int)strlen(g_find_text);
                if (len < 254) {
                    g_find_text[len] = ch;
                    g_find_text[len + 1] = '\0';
                }
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        if (ch >= 32 && ch < 127) {
            editor_insert_char(&g_editor, ch);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONDOWN: {
        int x = LOWORD(lParam);
        int y = HIWORD(lParam);
        RECT rc_click;
        GetClientRect(hwnd, &rc_click);
        editor_tab_t *tab = editor_active(&g_editor);
        if (!tab) break;

        /* check toolbar clicks */
        if (y < TOOLBAR_HEIGHT) {
            /* toolbar button clicks */
            static const struct { const char *label; int cmd; } tbuttons[] = {
                {"New",  IDM_NEW}, {"Open", IDM_OPEN}, {"Save", IDM_SAVE},
                {"|", 0},
                {"Build", IDM_BUILD}, {"Run", IDM_RUN}, {"F5", IDM_BUILDRUN},
                {"|", 0},
                {"Find", IDM_FIND}, {"Replace", IDM_REPLACE},
                {"|", 0},
                {"Format", IDM_FORMAT},
            };
            int tbcount = sizeof(tbuttons) / sizeof(tbuttons[0]);
            int bx2 = 4;
            for (int ti = 0; ti < tbcount; ti++) {
                if (tbuttons[ti].label[0] == '|') { bx2 += 10; continue; }
                int tw2 = (int)strlen(tbuttons[ti].label) * g_char_width + 12;
                if (x >= bx2 && x <= bx2 + tw2 && tbuttons[ti].cmd != 0) {
                    SendMessage(hwnd, WM_COMMAND, tbuttons[ti].cmd, 0);
                    return 0;
                }
                bx2 += tw2 + 4;
            }
            return 0;
        }

        /* check tab clicks */
        if (y >= TOOLBAR_HEIGHT && y < TOOLBAR_HEIGHT + TAB_HEIGHT) {
            int tab_idx = x / 120;
            if (tab_idx < g_editor.tab_count) {
                g_editor.active_tab = tab_idx;
                editor_highlight_visible(&g_editor);
                InvalidateRect(hwnd, NULL, TRUE);
            }
            return 0;
        }

        /* click in project tree */
        if (g_project.is_open && x < PROJECT_WIDTH && y > (TOOLBAR_HEIGHT + TAB_HEIGHT) &&
            y < (int)(rc_click.bottom - OUTPUT_HEIGHT - STATUS_HEIGHT)) {
            int row_height = g_char_height > 0 ? g_char_height : 16;
            int click_row = (y - TOOLBAR_HEIGHT - TAB_HEIGHT - 24) / row_height + g_project.scroll;
            int vis = 0;
            for (int pi = 0; pi < g_project.entry_count; pi++) {
                if (!g_project.entries[pi].visible) continue;
                if (vis == click_row) {
                    g_project.selected = pi;
                    if (g_project.entries[pi].type == ENTRY_DIR) {
                        project_toggle(&g_project, pi);
                    } else {
                        editor_open_file(&g_editor, g_project.entries[pi].full_path);
                    }
                    InvalidateRect(hwnd, NULL, TRUE);
                    break;
                }
                vis++;
            }
            return 0;
        }

        /* click in code area */
        int code_left = g_project.is_open ? PROJECT_WIDTH + 1 + g_line_num_width : g_line_num_width;
        if (x > code_left && x < (rc_click.right - MINIMAP_WIDTH) &&
            y > (TOOLBAR_HEIGHT + TAB_HEIGHT) &&
            y < (int)(g_editor.view_rows * g_char_height + TOOLBAR_HEIGHT + TAB_HEIGHT)) {
            size_t click_line = tab->scroll_line +
                (size_t)(y - TOOLBAR_HEIGHT - TAB_HEIGHT) / (size_t)g_char_height;
            size_t click_col = tab->scroll_col +
                (size_t)(x - code_left) / (size_t)g_char_width;
            size_t total = pt_line_count(tab->buffer);
            if (click_line >= total) click_line = total > 0 ? total - 1 : 0;
            size_t llen = pt_line_length(tab->buffer, click_line);
            if (click_col > llen) click_col = llen;

            editor_clear_selection(&g_editor);
            tab->cursor_line = click_line;
            tab->cursor_col = click_col;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        SetCapture(hwnd);
        return 0;
    }

    case WM_LBUTTONUP:
        ReleaseCapture();
        return 0;

    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        editor_tab_t *tab = editor_active(&g_editor);
        if (tab) {
            int scroll = -(delta / 40);
            if ((int)tab->scroll_line + scroll < 0)
                tab->scroll_line = 0;
            else
                tab->scroll_line += (size_t)scroll;
            editor_highlight_visible(&g_editor);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDM_NEW:     editor_new_file(&g_editor); break;
        case IDM_OPEN:    do_open_file(); break;
        case IDM_SAVE:    do_save_file(); break;
        case IDM_SAVEAS:  do_save_as(); break;
        case IDM_EXIT:    DestroyWindow(hwnd); break;
        case IDM_UNDO:    editor_undo(&g_editor); break;
        case IDM_REDO:    editor_redo(&g_editor); break;
        case IDM_CUT:     editor_cut(&g_editor); break;
        case IDM_COPY:    editor_copy(&g_editor); break;
        case IDM_PASTE:   editor_paste(&g_editor); break;
        case IDM_SELECTALL: editor_select_all(&g_editor); break;
        case IDM_BUILD:   do_build(); break;
        case IDM_RUN:     do_run(); break;
        case IDM_BUILDRUN: do_build_and_run(); break;
        case IDM_OPENPROJECT: do_open_project(); break;
        case IDM_DEBUG: {
            editor_tab_t *dt = editor_active(&g_editor);
            if (dt && !dt->is_new) {
                do_build();
                if (strstr(g_output, "Build successful")) {
                    char exe[MAX_PATH];
                    strncpy(exe, dt->filepath, MAX_PATH - 5);
                    char *dot = strrchr(exe, '.'); if (dot) strcpy(dot, ".exe"); else strcat(exe, ".exe");
                    dbg_start(&g_debugger, exe, NULL);
                }
            }
            break;
        }
        case IDM_STOPDBG:   dbg_stop(&g_debugger); break;
        case IDM_STEPOVER:  dbg_step_over(&g_debugger); break;
        case IDM_STEPINTO:  dbg_step_into(&g_debugger); break;
        case IDM_STEPOUT:   dbg_step_out(&g_debugger); break;
        case IDM_TOGGLEBP: {
            editor_tab_t *bt = editor_active(&g_editor);
            if (bt) dbg_toggle_breakpoint(&g_debugger, bt->filepath, (int)bt->cursor_line);
            break;
        }
        case IDM_FIND:    do_find(); break;
        case IDM_REPLACE: do_replace(); break;
        case IDM_FINDNEXT: do_find_next(); break;
        case IDM_GOTOLINE: do_goto_line(); break;
        case IDM_CLOSETAB: do_close_tab(); break;
        case IDM_FORMAT:  do_format(); break;
        case IDM_GENDOC:  do_gen_doc(); break;
        case IDM_REFRESH: project_refresh(&g_project); break;
        case IDM_ABOUT:
            MessageBoxW(hwnd,
                L"Zan IDE v1.0\n\n"
                L"A lightweight IDE for the Zan programming language.\n"
                L"Built with Win32 + GDI.\n\n"
                L"Shortcuts:\n"
                L"  Ctrl+N  New file\n"
                L"  Ctrl+O  Open file\n"
                L"  Ctrl+S  Save\n"
                L"  F5      Build & Run\n"
                L"  Ctrl+Z  Undo\n"
                L"  Ctrl+Y  Redo\n"
                L"  Ctrl+F  Find\n"
                L"  Ctrl+H  Replace\n"
                L"  Ctrl+G  Go to Line\n"
                L"  Ctrl+W  Close Tab\n"
                L"  F3      Find Next\n"
                L"  Ctrl+Shift+F  Format",
                L"About Zan IDE",
                MB_OK | MB_ICONINFORMATION);
            break;
        }
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

/* ---- Find/Replace/Goto implementations ---- */

static void do_find(void) {
    g_find_active = true;
    g_replace_active = false;
    /* prompt for search text */
    char buf[256] = {0};
    strncpy(buf, g_find_text, sizeof(buf) - 1);
    /* Use simple input box approach: read from WM_CHAR into find_text */
    /* The find bar is drawn by paint_find_bar; WM_CHAR routes input there when active */
    InvalidateRect(g_hwnd, NULL, TRUE);
}

static void do_replace(void) {
    g_find_active = true;
    g_replace_active = true;
    InvalidateRect(g_hwnd, NULL, TRUE);
}

static void do_find_next(void) {
    if (g_find_text[0] == '\0') { do_find(); return; }
    editor_tab_t *tab = editor_active(&g_editor);
    if (!tab) return;

    /* search forward from cursor */
    char *text = pt_to_string(tab->buffer);
    if (!text) return;
    size_t tlen = pt_length(tab->buffer);
    size_t start = pt_line_start(tab->buffer, tab->cursor_line) + tab->cursor_col + 1;
    size_t flen = strlen(g_find_text);

    for (size_t i = start; i + flen <= tlen; i++) {
        if (strncmp(text + i, g_find_text, flen) == 0) {
            /* found — navigate to it */
            size_t line = 0, col = 0;
            for (size_t j = 0; j < i; j++) {
                if (text[j] == '\n') { line++; col = 0; } else col++;
            }
            tab->cursor_line = line;
            tab->cursor_col = col;
            editor_ensure_cursor_visible(&g_editor);
            free(text);
            InvalidateRect(g_hwnd, NULL, TRUE);
            return;
        }
    }
    /* wrap around */
    for (size_t i = 0; i + flen <= start && i + flen <= tlen; i++) {
        if (strncmp(text + i, g_find_text, flen) == 0) {
            size_t line = 0, col = 0;
            for (size_t j = 0; j < i; j++) {
                if (text[j] == '\n') { line++; col = 0; } else col++;
            }
            tab->cursor_line = line;
            tab->cursor_col = col;
            editor_ensure_cursor_visible(&g_editor);
            free(text);
            InvalidateRect(g_hwnd, NULL, TRUE);
            return;
        }
    }
    free(text);
    append_output("Find: no match found\n");
    InvalidateRect(g_hwnd, NULL, TRUE);
}

static void do_goto_line(void) {
    editor_tab_t *tab = editor_active(&g_editor);
    if (!tab) return;
    char buf[32] = {0};
    /* Simple input via Windows API */
    /* Use a minimal approach: prompt box */
    WNDCLASSEXW dlg_wc = {0};
    /* Actually, use a simple approach: just show an input box using a combo of dialogs */
    char input[32] = {0};

    /* Simple approach: use GetConsoleWindow or a MessageBox prompt */
    /* For now, use a simple static approach with keyboard state */
    /* The user types the line number then presses Enter */
    (void)buf;
    (void)input;

    /* Practical approach: toggle a goto-line mode flag and read digits */
    /* Or just use the find bar mechanism with a special mode */
    /* For simplicity, directly ask via a simple dialog */
    snprintf(buf, sizeof(buf), "%d", (int)(tab->cursor_line + 1));

    append_output("Go to Line: type line number in find bar and press Enter\n");
    g_find_active = true;
    g_replace_active = false;
    g_find_text[0] = '\0';
    InvalidateRect(g_hwnd, NULL, TRUE);
}

static void do_close_tab(void) {
    if (g_editor.tab_count > 0) {
        editor_close_tab(&g_editor, g_editor.active_tab);
        InvalidateRect(g_hwnd, NULL, TRUE);
    }
}

static void do_format(void) {
    editor_tab_t *tab = editor_active(&g_editor);
    if (!tab || tab->is_new) {
        append_output("Format: save the file first\n");
        return;
    }

    /* save current file, run zanfmt, reload */
    editor_save(&g_editor);

    char cmd[MAX_PATH * 2];
    snprintf(cmd, sizeof(cmd), "zanfmt.exe \"%s\" 2>&1", tab->filepath);

    append_output("Formatting...\n");
    FILE *fp = _popen(cmd, "r");
    if (fp) {
        char line[512];
        while (fgets(line, sizeof(line), fp)) {
            append_output(line);
        }
        _pclose(fp);
    }

    /* reload file */
    editor_open_file(&g_editor, tab->filepath);
    append_output("Format complete.\n");
    InvalidateRect(g_hwnd, NULL, TRUE);
}

static void do_gen_doc(void) {
    editor_tab_t *tab = editor_active(&g_editor);
    if (!tab || tab->is_new) {
        append_output("Doc: save the file first\n");
        return;
    }

    char out_path[MAX_PATH];
    strncpy(out_path, tab->filepath, MAX_PATH - 6);
    char *dot = strrchr(out_path, '.');
    if (dot) strcpy(dot, ".html"); else strcat(out_path, ".html");

    char cmd[MAX_PATH * 3];
    snprintf(cmd, sizeof(cmd), "zandoc.exe \"%s\" --html -o \"%s\" 2>&1", tab->filepath, out_path);

    append_output("Generating documentation...\n");
    FILE *fp = _popen(cmd, "r");
    if (fp) {
        char line[512];
        while (fgets(line, sizeof(line), fp)) {
            append_output(line);
        }
        _pclose(fp);
    }
    append_output("Documentation generated.\n");
    InvalidateRect(g_hwnd, NULL, TRUE);
}

#else /* non-Windows stub */

#include <stdio.h>

int main(void) {
    fprintf(stderr, "Zan IDE currently requires Windows.\n");
    return 1;
}

#endif /* _WIN32 */
