/* Loader state, shared by the logic (main.c) and the screens (app/ui/ui.c).
 * No libnx types, so the screens also build on the host (scripts/mockups.sh). */
#ifndef RSL_LOADER_H
#define RSL_LOADER_H

#include <stdbool.h>
#include <stdint.h>

#include "ftpd.h"
#include "input.h"
#include "input_config.h"

#ifndef RSL_VERSION
#define RSL_VERSION "dev"
#endif

#define ROOT        "sdmc:/switch/renpy-loader"
#define GAMES_DIR   ROOT "/games"
#define CTRL_DIR    ROOT "/config/controller"
#define GLOBAL_CFG  CTRL_DIR "/global.json"
#define RUNTIME_NRO ROOT "/runtimes/renpy-runtime.nro"
#define MAX_GAMES   64

typedef enum { SCR_LIBRARY, SCR_DETAIL, SCR_REMAP, SCR_HELP, SCR_LAYOUT, SCR_PICTURE,
               SCR_FTP, SCR_UNPACK } screen_id;

/* Rows of the controller settings screen: one per action, then these. */
enum { ROW_DEADZONE = RSL_ACT_COUNT, ROW_SPEED, ROW_ACCEL, ROW_DELAY, ROW_INTERVAL,
       ROW_STICKS, ROW_CURSOR_VIS, ROW_COUNT };

enum { MENU_FTP, MENU_MTP, MENU_REMAP, MENU_LAYOUT, MENU_HELP, MENU_EXIT, MENU_COUNT };   /* + menu */
enum { LAYOUT_ROW, LAYOUT_GRID, LAYOUT_COUNT };   /* library layouts, docs/LOADER_UI.md */
/* Layout page (+ menu): the library layouts, the accent colour presets,
 * then the custom hue row. */
enum { ACCENT_PRESETS = 8 };
enum { SET_PRESET = LAYOUT_COUNT, SET_CUSTOM = SET_PRESET + ACCENT_PRESETS, SET_COUNT };
enum { HELP_COUNT = 5 };                                          /* how-to-play cards */
enum { DETAIL_LAUNCH, DETAIL_SHORTCUT, DETAIL_CONTROLS, DETAIL_PICTURE, DETAIL_COUNT };   /* detail buttons, right to left */
enum { DLG_NONE = 0, DLG_APPLET, DLG_DELETE };
enum { DEL_CONFIRM = 0, DEL_CANCEL = 1, DEL_COUNT = 2 };
enum { SC_ACCEPT = 0, SC_DECLINE = 1, SC_COUNT = 2 };            /* first-launch loader shortcut prompt */
enum { ARRIVED_LIBRARY, ARRIVED_MORE, ARRIVED_COUNT };   /* "game arrived" buttons, right to left */

typedef enum { ST_INFO, ST_OK, ST_BAD } status_kind;

typedef struct {
    bool running, done, ok, cancelled;
    char zip_name[128];
    char title[96];
    char file[256];
    char stage[64];
    char error[256];
    char out_id[64];
    char runtime[49];
    uint64_t done_bytes, total_bytes;
    unsigned done_files, total_files;
} rsl_unpack_status;

struct game_info {
    char title[96];            /* manifest title, "" if none (the folder name is shown) */
    char cover[272];           /* cover JPEG (BMP if chosen on the Switch) path, "" for a placeholder */
    char renpy[24];            /* Ren'Py version from manifest.json, "" if none */
    char state[16];            /* experimental / unsupported / zip; "" without manifest */
    char runtime[49];          /* runtime the game needs */
    char nro[160];             /* where that runtime would be */
    char problem[160];         /* why it must not start, "" if it may */
    int issues;                /* performance.issues from the importer */
    char issue[3][160];        /* the first three of them */
    bool recompile, runtime_ok, is_zip;
    uint64_t zip_size;
};

typedef struct {
    screen_id screen;
    int layout;                    /* LAYOUT_ROW / LAYOUT_GRID, saved in config/loader.json */
    uint32_t accent;               /* focus colour, 0xRRGGBB, saved there too */
    int accent_hue;                /* the custom row's hue, 0-359 */
    int sel;                       /* focused item on the current screen */
    int game;                      /* game of the detail page / per-game controls */
    bool menu_open;                /* the + menu, over the library */
    bool dialog_open;              /* applet-mode / delete confirm (details), or "game arrived" (Wi-Fi page) */
    int dialog_kind;               /* DLG_* on SCR_DETAIL */
    int dialog_sel;                /* its focused button: 0, DEL_*, or ARRIVED_* */
    int menu_sel;
    char status[512];             /* the applet-mode refusal was ~260 chars (run 27) */
    status_kind status_kind;
    uint32_t now;

    rsl_input in;
    rsl_raw_input raw;

    char games[MAX_GAMES][128];    /* folder or .zip names under games/ */
    bool game_ok[MAX_GAMES];       /* has a game/ folder (or valid zip entry) */
    struct game_info info[MAX_GAMES];
    int game_count;

    rsl_input_config edit;         /* controller settings being edited */
    char edit_path[256];
    char edit_label[80];
    int edit_preset;
    bool edit_dirty;
    int capture;                   /* action being captured, -1 if none */
    uint32_t capture_deadline;

    /* Picture page (Change picture on the details): zoom/pan preview of the
     * game's cover artwork (1280 x 720 XRGB8888); NULL off the page. */
    uint32_t *shot;                /* pixels, XRGB8888 (B,G,R,X), shot_w x shot_h; NULL off the page */
    int shot_w, shot_h, pic_zoom_pct;
    unsigned shot_gen;             /* increments on every preview redraw */
    char pic_label[64];            /* source label shown in header */

    /* Add games over Wi-Fi (+ menu): the loader's FTP server, its state
     * copied in by transfer.c every frame while the page is open. */
    bool transfer_usb;
    char ftp_addr[24];             /* what to type into the importer, "" without Wi-Fi */
    rsl_ftpd_status ftp;
    char ftp_title[96];            /* title of g.ftp.done, the game that just arrived */
    rsl_unpack_status unpack;      /* ZIP unpack worker state */

    /* Facts about the console, filled in by main.c. */
    bool sd_ok, full_memory, runtime_found;
    rsl_load_result global_load;
    const char *mode;
    uint64_t mem_total;
} loader_state;

extern loader_state g;

#endif
