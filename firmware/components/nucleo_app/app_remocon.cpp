// app_remocon.cpp — "RemoCon": a layout-driven IR remote built from JSON files on the SD card.
//
// Where the IR Remote app (app_ir.cpp) browses a packed brand catalog as lists, RemoCon draws YOUR remotes:
// every /sd/data/remocon/<device>.json describes one device (name, type, colour, default protocol/address)
// and its keys laid out on a grid, page by page — plain buttons with an icon, a 5-way D-pad, +/- rockers
// (VOL, CH, TEMP), RAW captured timings, signals referenced in Flipper .ir files, macros ("Cinema": power,
// wait, input, Netflix) and a Hitachi AC424 climate panel. The format is specified in docs/remocon.md; the
// house remotes (ported from the qubix firmware) ship in sd/data/remocon/.
//
// Views: the device carousel (big card per device, neighbours peeking) -> the remote (its grid). On the
// remote the arrows move a SPATIAL focus between keys (the nearest key in that direction, so any layout
// navigates naturally), Enter fires, TAB / 1-6 switch page, each key's own "key" shortcut fires it from
// anywhere on the page, and Space puts the D-pad in DIRECT mode (arrows/Enter drive the TV straight away).
// The last remote reopens on launch (resume).
//
// RAM (the full OS has ~18 KB of heap): the JSON is never loaded whole and no cJSON DOM is built. A small
// pull parser streams the file with fgetc (one char of lookahead) straight into fixed-size tables that live
// in APP_RAM (allocated on open, freed on close: 7.4 KB). Listing the devices parses only each file's head.
#include "nucleo_app.h"
#include "nucleo_kbd.h"
#include "app_gfx.h"
#include "app_ui.h"          // app_ui_ascii_fold: the TFT fonts have no accented glyphs
#include "app_large.h"       // two screens: the remote as a touch keypad on the panel, the Cardputer as companion
#include "ui_glyph.h"        // system glyph set (power, speaker, mute, star...) shared with Settings
#include "nucleo_touch.h"
#include "nucleo_theme.h"
#include "nucleo_i18n.h"
#include "nucleo_ir.h"
#include "nucleo_ir_proto.h"
#include "nucleo_board.h"    // NUCLEO_SD_MOUNT
#include "esp_timer.h"
#include <M5GFX.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>

// One pointer type for every font, so ?: can pick between a GFX, a BMP and a GLCD font.
#define FNT(n)   ((const lgfx::IFont *)&fonts::n)
#define RC_DIR   NUCLEO_SD_MOUNT "/data/remocon"
#define RC_STATE RC_DIR "/.last"

// ---- palette: chrome follows the theme; the rest are content colours a JSON can name ----------------------
#define BG    THEME_BG
#define FG    THEME_FG
#define MUTED THEME_MUTED
#define DIM   THEME_DIM
#define LINE  THEME_LINE
#define INK   THEME_INK
static const uint16_t ACCENT  = 0xFB2C;   // RemoCon's launcher identity (coral)
static const uint16_t COL_RED = 0xF9A6, COL_GREEN = 0x4F2A, COL_BLUE = 0x3C9F, COL_YELLOW = 0xFEA0,
                      COL_ORANGE = 0xFC40, COL_PURPLE = 0xA27F, COL_CYAN = 0x3EFB, COL_PINK = 0xFB56,
                      COL_GRAY = 0x8C71, COL_WHITE = 0xFFFF, COL_FLASH = 0xFFFF, COL_TX = 0x4FEA;

// ---- limits (sizes of the APP_RAM tables) ----------------------------------------------------------------
#define MAX_DEV    24
#define MAX_PAGE   6
#define MAX_W      48      // widgets on all pages of one device
#define MAX_ACT    80      // sendable actions (a D-pad is 5, a rocker 2)
#define MAX_RAW    400     // RAW µs durations, pooled for the whole device
#define MAX_STEP   24      // macro steps, pooled
#define MAX_TGT    64      // focus targets on the current page
#define MAX_REF    24      // signals referenced in Flipper .ir files
#define IRF_MAX    1024    // durations of one .ir RAW signal, read at send time (Hitachi AC424 = 854)
#define LBL        12
#define FILE_LEN   36
#define DNAME      22

enum { WT_BTN = 0, WT_DPAD, WT_ROCKER, WT_AC };
enum { DP_UP = 0, DP_DOWN, DP_LEFT, DP_RIGHT, DP_OK };      // D-pad parts (act[] slots)
enum { RK_PLUS = 0, RK_MINUS };                             // rocker parts
enum { AC_SEND = 0, AC_OFF, AC_TDOWN, AC_TUP, AC_MODE, AC_FAN, AC_SWING, AC__N };   // AC panel parts
enum { AK_NONE = 0, AK_PROTO, AK_RAW, AK_MACRO, AK_IRFILE };
enum { AF_PROTO = 1, AF_ADDR = 2 };                         // explicitly set (else inherit the device default)

typedef struct {
    uint8_t  kind, proto, flags, repeats;
    uint16_t n, freq;            // RAW: count + carrier; MACRO: step count
    uint32_t addr, val;          // PROTO: address + command; RAW: pool offset; MACRO: first step; IRFILE: ref index
} rc_act_t;
typedef struct { char file[32]; char sig[22]; } rc_ref_t;   // a named signal in a Flipper .ir file on the SD

typedef struct {
    char     label[LBL];
    uint8_t  type, page, x, y, w, h, icon;
    char     key, key2;          // shortcuts (rocker: key = plus, key2 = minus)
    uint16_t color;
    int16_t  act[5];
} rc_widget_t;

typedef struct { int16_t act; uint16_t delay; char press[LBL]; } rc_step_t;
typedef struct { uint8_t wi, part; int16_t x, y, w, h; } rc_tgt_t;     // rect in 1/6-cell units
typedef struct { char file[FILE_LEN]; char name[DNAME]; uint16_t color; uint8_t type, npages; uint16_t nkeys; } rc_dev_t;

static rc_dev_t    *s_devs;   static int s_ndev;
static rc_widget_t *s_w;      static int s_nw;
static rc_act_t    *s_act;    static int s_nact;
static uint16_t    *s_raw;    static int s_nraw;
static rc_step_t   *s_step;   static int s_nstep;
static rc_tgt_t    *s_tgt;    static int s_ntgt;
static rc_ref_t    *s_ref;    static int s_nref;

// The AC panel (one per device): the settings the user dialled in, not telemetry — the appliance has no way to
// answer (the Cardputer has no IR receiver). Adjusting never transmits; SEND does, OFF sends an explicit off.
// Temperature / mode / fan persist per device; "on" never does (nothing is sent on restore).
enum { ACM_COOL = 0, ACM_HEAT, ACM_DRY, ACM_FAN };
static struct { uint8_t temp, mode, fan; bool on; } s_ac = { 23, ACM_COOL, 1, false };

// the open device
static char     s_pname[MAX_PAGE][LBL]; static int s_npage;
static char     s_dname[DNAME];
static uint16_t s_dcolor;
static uint8_t  s_dtype, s_dproto = NIR_PROTO_NEC, s_drep;
static uint32_t s_daddr;
static int      s_cols = 4, s_rows;          // grid of the current page (rows = its lowest key)
static char     s_err[44];                   // parse error of the open / listed file

// UI state
static int  s_view;                 // 0 = carousel, 1 = remote
static int  s_dev;                  // carousel selection / open device
static int  s_page, s_foc;          // current page, focused target
static int  s_scroll;               // first visible grid row (compact)
static int  s_lg_scroll;            // panel list scroll
static bool s_direct;               // D-pad direct mode
static char s_status[28];
static int  s_flash = -1;           // target lit after a send
static int64_t s_flash_until;
static bool s_tx;                   // TX lamp on (with the flash)
// macro runner
static int  s_mac = -1, s_mac_pos;
static int64_t s_mac_due;

// =========================================================================================================
// Pull JSON parser over a FILE*: one char of lookahead, no buffer, no DOM. Lenient about commas.
// =========================================================================================================
typedef struct { FILE *f; int c; int line; bool err; } jp_t;

static void jp_adv(jp_t *p) { p->c = fgetc(p->f); if (p->c == '\n') p->line++; }
static void jp_ws(jp_t *p)  { while (p->c == ' ' || p->c == '\t' || p->c == '\n' || p->c == '\r') jp_adv(p); }
static int  jp_peek(jp_t *p) { jp_ws(p); return p->c; }
static bool jp_fail(jp_t *p) { p->err = true; return false; }

static bool jp_string(jp_t *p, char *out, int cap)
{
    int n = 0;
    if (jp_peek(p) != '"') return jp_fail(p);
    jp_adv(p);
    while (p->c != '"') {
        if (p->c == EOF || p->c == '\n') return jp_fail(p);
        int ch = p->c;
        if (ch == '\\') {
            jp_adv(p);
            switch (p->c) {
            case 'n': ch = ' '; break;  case 't': ch = ' '; break;
            case 'u': { for (int i = 0; i < 4; i++) jp_adv(p); ch = '?'; } break;
            case EOF: return jp_fail(p);
            default:  ch = p->c; break;
            }
        }
        if (out && n < cap - 1) out[n++] = (char)ch;
        jp_adv(p);
    }
    jp_adv(p);
    if (out) out[n] = 0;
    return true;
}
static bool jp_number(jp_t *p, double *v)
{
    char b[24]; int n = 0;
    jp_ws(p);
    while ((isdigit(p->c) || p->c == '-' || p->c == '+' || p->c == '.' || p->c == 'e' || p->c == 'E') && n < 23) { b[n++] = (char)p->c; jp_adv(p); }
    b[n] = 0;
    if (!n) return jp_fail(p);
    *v = strtod(b, NULL);
    return true;
}
// A number, or a string holding one ("0x20DF10EF" — IR codes are usually written in hex).
static bool jp_uint(jp_t *p, uint32_t *v)
{
    if (jp_peek(p) == '"') { char s[16]; if (!jp_string(p, s, sizeof s)) return false; *v = (uint32_t)strtoul(s, NULL, 0); return true; }
    double x; if (!jp_number(p, &x)) return false;
    *v = x < 0 ? 0 : (uint32_t)x;
    return true;
}
static bool jp_word(jp_t *p)   // true / false / null
{
    jp_ws(p);
    bool t = p->c == 't';
    while (isalpha(p->c)) jp_adv(p);
    return t;
}
static bool jp_skip(jp_t *p)
{
    int c = jp_peek(p);
    if (c == '"') return jp_string(p, NULL, 0);
    if (c == '{' || c == '[') {
        int depth = 0;
        do {
            if (p->c == '"') { if (!jp_string(p, NULL, 0)) return false; continue; }
            if (p->c == '{' || p->c == '[') depth++;
            else if (p->c == '}' || p->c == ']') depth--;
            else if (p->c == EOF) return jp_fail(p);
            jp_adv(p);
        } while (depth > 0);
        return true;
    }
    if (c == 't' || c == 'f' || c == 'n') { jp_word(p); return true; }
    double v; return jp_number(p, &v);
}
static bool jp_open(jp_t *p, char ch) { if (jp_peek(p) != ch) return jp_fail(p); jp_adv(p); return true; }
// Next member of an object: false at '}' (consumed) or on error.
static bool jp_member(jp_t *p, char *key, int cap)
{
    if (p->err) return false;
    if (jp_peek(p) == ',') { jp_adv(p); jp_ws(p); }
    if (p->c == '}') { jp_adv(p); return false; }
    if (!jp_string(p, key, cap)) return false;
    if (jp_peek(p) != ':') return jp_fail(p);
    jp_adv(p);
    return true;
}
// Next element of an array: false at ']' (consumed) or on error.
static bool jp_elem(jp_t *p)
{
    if (p->err) return false;
    if (jp_peek(p) == ',') { jp_adv(p); jp_ws(p); }
    if (p->c == ']') { jp_adv(p); return false; }
    if (p->c == EOF) return jp_fail(p);
    return true;
}

// =========================================================================================================
// Vocabulary: device types, icons, colours
// =========================================================================================================
enum { DT_GENERIC = 0, DT_TV, DT_AC, DT_AUDIO, DT_LIGHT, DT_FAN, DT_PROJECTOR, DT_BOX, DT__N };
static const char *const DTYPES[DT__N] = { "generic", "tv", "ac", "audio", "light", "fan", "projector", "box" };

enum { IC_NONE = 0, IC_POWER, IC_MUTE, IC_VOL, IC_UP, IC_DOWN, IC_LEFT, IC_RIGHT, IC_OK, IC_PLAY, IC_PAUSE,
       IC_STOP, IC_REW, IC_FF, IC_NEXT, IC_PREV, IC_REC, IC_HOME, IC_BACK, IC_MENU, IC_LIST, IC_INFO, IC_INPUT,
       IC_SETTINGS, IC_STAR, IC_MOON, IC_SUN, IC_FAN, IC_MODE, IC_SWING, IC_TIMER, IC_MIC, IC_BT, IC_TV,
       IC_PLUS, IC_MINUS, IC_SEARCH, IC_CHECK, IC_LOCK, IC_LIGHT, IC__N };
static const char *const ICONS[IC__N] = { "", "power", "mute", "vol", "up", "down", "left", "right", "ok", "play",
       "pause", "stop", "rew", "ff", "next", "prev", "rec", "home", "back", "menu", "list", "info", "input",
       "settings", "star", "moon", "sun", "fan", "mode", "swing", "timer", "mic", "bt", "tv", "plus", "minus",
       "search", "check", "lock", "light" };

static int lookup(const char *s, const char *const *tab, int n)
{
    for (int i = 0; i < n; i++) if (!strcasecmp(s, tab[i])) return i;
    return 0;
}
static uint16_t parse_color(const char *s)
{
    static const struct { const char *n; uint16_t c; } NAMED[] = {
        { "red", COL_RED }, { "green", COL_GREEN }, { "blue", COL_BLUE }, { "yellow", COL_YELLOW },
        { "orange", COL_ORANGE }, { "purple", COL_PURPLE }, { "cyan", COL_CYAN }, { "pink", COL_PINK },
        { "gray", COL_GRAY }, { "grey", COL_GRAY }, { "white", COL_WHITE } };
    if (s[0] == '#' && strlen(s) == 7) {
        uint32_t v = (uint32_t)strtoul(s + 1, NULL, 16);
        return (uint16_t)((((v >> 16) & 0xF8) << 8) | (((v >> 8) & 0xFC) << 3) | ((v & 0xFF) >> 3));
    }
    for (size_t i = 0; i < sizeof NAMED / sizeof NAMED[0]; i++) if (!strcasecmp(s, NAMED[i].n)) return NAMED[i].c;
    return 0;
}
static void fold(char *dst, int cap, const char *src) { app_ui_ascii_fold(src, dst, cap); }

// =========================================================================================================
// Device file -> tables
// =========================================================================================================
static int new_act(void)
{
    if (s_nact >= MAX_ACT) return -1;
    memset(&s_act[s_nact], 0, sizeof s_act[0]);
    return s_nact++;
}

// An action body: the members of a button / D-pad part / macro step object that describe what to send.
// Returns true when `key` was one of them (consumed), false to let the caller handle or skip it.
static bool act_member(jp_t *p, const char *key, rc_act_t *a)
{
    uint32_t v;
    if (!strcmp(key, "cmd") || !strcmp(key, "command")) { if (jp_uint(p, &v)) { a->val = v; if (a->kind == AK_NONE) a->kind = AK_PROTO; } return true; }
    if (!strcmp(key, "address")) { if (jp_uint(p, &v)) { a->addr = v; a->flags |= AF_ADDR; } return true; }
    if (!strcmp(key, "repeats")) { if (jp_uint(p, &v)) a->repeats = (uint8_t)(v > 10 ? 10 : v); return true; }
    if (!strcmp(key, "ir") || !strcmp(key, "signal")) {      // { "ir": "tv.ir", "signal": "Power" } (Flipper format)
        if (a->kind != AK_IRFILE) {
            if (s_nref >= MAX_REF) { jp_skip(p); return true; }
            memset(&s_ref[s_nref], 0, sizeof s_ref[0]);
            a->kind = AK_IRFILE; a->val = (uint32_t)s_nref++;
        }
        rc_ref_t *r = &s_ref[a->val];
        if (key[0] == 'i') jp_string(p, r->file, sizeof r->file); else jp_string(p, r->sig, sizeof r->sig);
        return true;
    }
    if (!strcmp(key, "protocol")) {
        char s[12]; if (jp_string(p, s, sizeof s)) { a->proto = (uint8_t)nir_proto_from_name(s); a->flags |= AF_PROTO; }
        return true;
    }
    if (!strcmp(key, "raw")) {                    // { "freq": 38000, "data": [ µs, ... ] }
        char k[12];
        a->kind = AK_RAW; a->freq = 38000; a->val = (uint32_t)s_nraw; a->n = 0;
        if (!jp_open(p, '{')) return true;
        while (jp_member(p, k, sizeof k)) {
            if (!strcmp(k, "freq") || !strcmp(k, "frequency")) { if (jp_uint(p, &v)) a->freq = (uint16_t)v; }
            else if (!strcmp(k, "data")) {
                if (!jp_open(p, '[')) break;
                while (jp_elem(p)) {
                    if (!jp_uint(p, &v)) break;
                    if (s_nraw < MAX_RAW) { s_raw[s_nraw++] = (uint16_t)(v > 65535 ? 65535 : v); a->n++; }
                }
            } else jp_skip(p);
        }
        if (!a->n) a->kind = AK_NONE;
        return true;
    }
    if (!strcmp(key, "macro")) {                  // [ {"press":"Power"}, {"delay":2000}, {"cmd":12}, ... ]
        a->kind = AK_MACRO; a->val = (uint32_t)s_nstep; a->n = 0;
        if (!jp_open(p, '[')) return true;
        while (jp_elem(p)) {
            if (s_nstep >= MAX_STEP) { jp_skip(p); continue; }
            rc_step_t *st = &s_step[s_nstep]; memset(st, 0, sizeof *st); st->act = -1;
            rc_act_t inl; memset(&inl, 0, sizeof inl);
            char k[12], buf[LBL * 2];
            if (!jp_open(p, '{')) break;
            while (jp_member(p, k, sizeof k)) {
                if (!strcmp(k, "press")) { if (jp_string(p, buf, sizeof buf)) fold(st->press, LBL, buf); }
                else if (!strcmp(k, "delay")) { if (jp_uint(p, &v)) st->delay = (uint16_t)(v > 30000 ? 30000 : v); }
                else if (!act_member(p, k, &inl)) jp_skip(p);
            }
            if (inl.kind == AK_PROTO || inl.kind == AK_RAW || inl.kind == AK_IRFILE) { int ai = new_act(); if (ai >= 0) { s_act[ai] = inl; st->act = (int16_t)ai; } }
            if (st->press[0] || st->delay || st->act >= 0) { s_nstep++; a->n++; }
        }
        if (!a->n) a->kind = AK_NONE;
        return true;
    }
    return false;
}
// A D-pad / rocker part: a bare command number, or a whole action object.
static int16_t parse_part(jp_t *p)
{
    rc_act_t a; memset(&a, 0, sizeof a);
    if (jp_peek(p) == '{') {
        char k[12]; jp_adv(p);
        while (jp_member(p, k, sizeof k)) if (!act_member(p, k, &a)) jp_skip(p);
    } else {
        uint32_t v; if (!jp_uint(p, &v)) return -1;
        a.kind = AK_PROTO; a.val = v;
    }
    if (a.kind == AK_NONE || a.kind == AK_MACRO) return -1;
    int ai = new_act(); if (ai < 0) return -1;
    s_act[ai] = a;
    return (int16_t)ai;
}
static void parse_widget(jp_t *p, int page)
{
    rc_widget_t w; memset(&w, 0, sizeof w);
    for (int i = 0; i < 5; i++) w.act[i] = -1;
    w.page = (uint8_t)page; w.w = 1; w.h = 1;
    rc_act_t a; memset(&a, 0, sizeof a);
    char k[12], s[LBL * 2];
    uint32_t v;
    if (!jp_open(p, '{')) return;
    while (jp_member(p, k, sizeof k)) {
        if (!strcmp(k, "type")) {
            if (jp_string(p, s, sizeof s)) w.type = !strcmp(s, "dpad") ? WT_DPAD : !strcmp(s, "rocker") ? WT_ROCKER : !strcmp(s, "ac") ? WT_AC : WT_BTN;
        }
        else if (!strcmp(k, "label")) { if (jp_string(p, s, sizeof s)) fold(w.label, LBL, s); }
        else if (!strcmp(k, "icon"))  { if (jp_string(p, s, sizeof s)) w.icon = (uint8_t)lookup(s, ICONS, IC__N); }
        else if (!strcmp(k, "color")) { if (jp_string(p, s, sizeof s)) w.color = parse_color(s); }
        else if (!strcmp(k, "key") || !strcmp(k, "key_plus"))  { if (jp_string(p, s, sizeof s)) w.key = (char)tolower((unsigned char)s[0]); }
        else if (!strcmp(k, "key_minus")) { if (jp_string(p, s, sizeof s)) w.key2 = (char)tolower((unsigned char)s[0]); }
        else if (!strcmp(k, "x")) { if (jp_uint(p, &v)) w.x = (uint8_t)(v > 15 ? 15 : v); }
        else if (!strcmp(k, "y")) { if (jp_uint(p, &v)) w.y = (uint8_t)(v > 31 ? 31 : v); }
        else if (!strcmp(k, "w")) { if (jp_uint(p, &v)) w.w = (uint8_t)(v < 1 ? 1 : v > 8 ? 8 : v); }
        else if (!strcmp(k, "h")) { if (jp_uint(p, &v)) w.h = (uint8_t)(v < 1 ? 1 : v > 8 ? 8 : v); }
        else if (!strcmp(k, "up"))    w.act[DP_UP]    = parse_part(p);
        else if (!strcmp(k, "down"))  w.act[DP_DOWN]  = parse_part(p);
        else if (!strcmp(k, "left"))  w.act[DP_LEFT]  = parse_part(p);
        else if (!strcmp(k, "right")) w.act[DP_RIGHT] = parse_part(p);
        else if (!strcmp(k, "ok"))    w.act[DP_OK]    = parse_part(p);
        else if (!strcmp(k, "plus"))  w.act[RK_PLUS]  = parse_part(p);
        else if (!strcmp(k, "minus")) w.act[RK_MINUS] = parse_part(p);
        else if (!act_member(p, k, &a)) jp_skip(p);
    }
    if (w.type == WT_BTN && a.kind != AK_NONE) { int ai = new_act(); if (ai >= 0) { s_act[ai] = a; w.act[0] = (int16_t)ai; } }
    if (w.type == WT_DPAD && w.w < 3) { w.w = 3; w.h = 3; }   // a D-pad needs room for its cross
    if (w.type == WT_ROCKER && w.w == 1 && w.h == 1) w.h = 2;
    if (w.type == WT_AC) { if (w.w < 3) w.w = 3; if (w.h < 3) w.h = 3; }   // display + 3 rows of controls
    if (s_nw < MAX_W) s_w[s_nw++] = w;
}
// Resolve a macro's {"press": "Label"} against the keys: a button's label, a rocker's label + "+"/"-", or a
// D-pad direction ("up" "down" "left" "right" "ok").
static int16_t find_press(const char *name)
{
    static const char *const DIRS[5] = { "up", "down", "left", "right", "ok" };
    int n = (int)strlen(name);
    for (int i = 0; i < s_nw; i++) {
        const rc_widget_t *w = &s_w[i];
        if (w->type == WT_BTN && !strcasecmp(w->label, name)) return w->act[0];
        if (w->type == WT_ROCKER && n > 1 && (name[n - 1] == '+' || name[n - 1] == '-') &&
            (int)strlen(w->label) == n - 1 && !strncasecmp(w->label, name, n - 1))
            return w->act[name[n - 1] == '+' ? RK_PLUS : RK_MINUS];
        if (w->type == WT_DPAD) for (int k = 0; k < 5; k++) if (!strcasecmp(name, DIRS[k])) return w->act[k];
    }
    return -1;
}

// Parse a device file. full = false: only the head (name, type, colour) + a count of pages and keys.
static bool load_device(const char *file, bool full, rc_dev_t *head)
{
    char path[sizeof RC_DIR + FILE_LEN + 2];
    snprintf(path, sizeof path, "%s/%s", RC_DIR, file);
    jp_t p = { fopen(path, "rb"), 0, 1, false };
    s_err[0] = 0;
    if (!p.f) { snprintf(s_err, sizeof s_err, "%s", TR5("file non apribile", "cannot open file", "no se abre", "illisible", "nicht lesbar")); return false; }
    jp_adv(&p);
    if (full) { s_nw = s_nact = s_nraw = s_nstep = s_nref = 0; s_npage = 0; s_cols = 4; s_dproto = NIR_PROTO_NEC; s_daddr = 0; s_drep = 0; s_dcolor = ACCENT; s_dtype = 0; s_dname[0] = 0; }
    if (head) { memset(head, 0, sizeof *head); head->color = ACCENT; }
    char k[12], s[48]; uint32_t v;
    if (jp_open(&p, '{')) {
        while (jp_member(&p, k, sizeof k)) {
            if (!strcmp(k, "name")) {
                if (jp_string(&p, s, sizeof s)) { if (full) fold(s_dname, DNAME, s); if (head) fold(head->name, DNAME, s); }
            } else if (!strcmp(k, "type")) {
                if (jp_string(&p, s, sizeof s)) { int t = lookup(s, DTYPES, DT__N); if (full) s_dtype = (uint8_t)t; if (head) head->type = (uint8_t)t; }
            } else if (!strcmp(k, "color")) {
                if (jp_string(&p, s, sizeof s)) { uint16_t c = parse_color(s); if (c) { if (full) s_dcolor = c; if (head) head->color = c; } }
            } else if (full && !strcmp(k, "protocol")) {
                if (jp_string(&p, s, sizeof s)) s_dproto = (uint8_t)nir_proto_from_name(s);
            } else if (full && !strcmp(k, "address")) { if (jp_uint(&p, &v)) s_daddr = v; }
            else if (full && !strcmp(k, "repeats")) { if (jp_uint(&p, &v)) s_drep = (uint8_t)(v > 10 ? 10 : v); }
            else if (full && !strcmp(k, "cols")) { if (jp_uint(&p, &v)) s_cols = v < 1 ? 1 : v > 8 ? 8 : (int)v; }
            else if (!strcmp(k, "pages")) {
                if (!jp_open(&p, '[')) break;
                while (jp_elem(&p)) {
                    if (!jp_open(&p, '{')) break;
                    int pg = full ? s_npage : 0;
                    if (full && s_npage < MAX_PAGE) snprintf(s_pname[s_npage], LBL, "%d", s_npage + 1);
                    while (jp_member(&p, k, sizeof k)) {
                        if (full && !strcmp(k, "name") && pg < MAX_PAGE) { if (jp_string(&p, s, sizeof s)) fold(s_pname[pg], LBL, s); }
                        else if (!strcmp(k, "buttons")) {
                            if (!jp_open(&p, '[')) break;
                            while (jp_elem(&p)) {
                                if (full && pg < MAX_PAGE) parse_widget(&p, pg); else jp_skip(&p);
                                if (head) head->nkeys++;
                            }
                        } else jp_skip(&p);
                    }
                    if (full && s_npage < MAX_PAGE) s_npage++;
                    if (head) head->npages++;
                }
            } else jp_skip(&p);
        }
    }
    bool ok = !p.err;
    if (!ok) snprintf(s_err, sizeof s_err, TR5("JSON errato, riga %d", "bad JSON, line %d", "JSON mal, linea %d", "JSON faux, ligne %d", "JSON-Fehler, Zeile %d"), p.line);
    fclose(p.f);
    if (!full) return ok;
    // Defaults: a key without its own protocol / address / repeats takes the device's.
    for (int i = 0; i < s_nact; i++) {
        rc_act_t *a = &s_act[i];
        if (!(a->flags & AF_PROTO)) a->proto = s_dproto;
        if (!(a->flags & AF_ADDR))  a->addr = s_daddr;
        if (!a->repeats) a->repeats = s_drep;
    }
    for (int i = 0; i < s_nstep; i++) if (s_step[i].act < 0 && s_step[i].press[0]) s_step[i].act = find_press(s_step[i].press);
    if (!s_npage) { s_npage = 1; snprintf(s_pname[0], LBL, "1"); }
    if (ok && !s_nw) snprintf(s_err, sizeof s_err, "%s", TR5("nessun tasto", "no keys", "sin teclas", "aucune touche", "keine Tasten"));
    return ok && s_nw > 0;
}

static int dev_cmp(const void *a, const void *b) { return strcasecmp(((const rc_dev_t *)a)->name, ((const rc_dev_t *)b)->name); }
static void scan_devices(void)
{
    s_ndev = 0;
    DIR *dir = opendir(RC_DIR);
    if (!dir) return;
    struct dirent *e;
    while ((e = readdir(dir)) && s_ndev < MAX_DEV) {
        const char *n = e->d_name; size_t L = strlen(n);
        if (n[0] == '.' || L < 6 || L >= FILE_LEN || strcasecmp(n + L - 5, ".json")) continue;
        rc_dev_t *dv = &s_devs[s_ndev];
        if (!load_device(n, false, dv)) continue;           // a broken file is skipped from the carousel
        snprintf(dv->file, FILE_LEN, "%s", n);
        if (!dv->name[0]) { fold(dv->name, DNAME, n); dv->name[strlen(dv->name) > 5 ? strlen(dv->name) - 5 : 0] = 0; }
        s_ndev++;
    }
    closedir(dir);
    s_err[0] = 0;
    qsort(s_devs, s_ndev, sizeof s_devs[0], dev_cmp);
}

// Resume: the last opened device + page.
static void save_state(void)
{
    if (s_dev < 0 || s_dev >= s_ndev) return;
    FILE *f = fopen(RC_STATE, "wb"); if (!f) return;
    fprintf(f, "%s\n%d\n", s_devs[s_dev].file, s_page);
    fclose(f);
}
static bool load_state(int *dev, int *page)
{
    FILE *f = fopen(RC_STATE, "rb"); if (!f) return false;
    char file[FILE_LEN + 2] = { 0 }; int pg = 0;
    bool ok = fgets(file, sizeof file, f) && fscanf(f, "%d", &pg) == 1;
    fclose(f);
    if (!ok) return false;
    file[strcspn(file, "\r\n")] = 0;
    for (int i = 0; i < s_ndev; i++) if (!strcmp(s_devs[i].file, file)) { *dev = i; *page = pg; return true; }
    return false;
}

// =========================================================================================================
// Focus targets of the current page (1/6-cell units: a D-pad splits its box in thirds, a rocker in halves)
// =========================================================================================================
static void add_tgt(int wi, int part, int x, int y, int w, int h)
{
    if (s_ntgt >= MAX_TGT) return;
    rc_tgt_t *t = &s_tgt[s_ntgt++];
    t->wi = (uint8_t)wi; t->part = (uint8_t)part; t->x = (int16_t)x; t->y = (int16_t)y; t->w = (int16_t)w; t->h = (int16_t)h;
}
static void build_targets(void)
{
    s_ntgt = 0; s_rows = 1;
    for (int i = 0; i < s_nw; i++) {
        const rc_widget_t *w = &s_w[i];
        if (w->page != s_page) continue;
        if (w->y + w->h > s_rows) s_rows = w->y + w->h;
        const int X = w->x * 6, Y = w->y * 6, W6 = w->w * 6, H6 = w->h * 6;
        if (w->type == WT_DPAD) {
            const int tw = W6 / 3, th = H6 / 3;
            add_tgt(i, DP_UP,    X + tw,     Y,          tw, th);
            add_tgt(i, DP_LEFT,  X,          Y + th,     tw, th);
            add_tgt(i, DP_OK,    X + tw,     Y + th,     tw, th);
            add_tgt(i, DP_RIGHT, X + 2 * tw, Y + th,     tw, th);
            add_tgt(i, DP_DOWN,  X + tw,     Y + 2 * th, tw, th);
        } else if (w->type == WT_AC) {
            // [ T- ][   display = MODE   ][ T+ ]   top half
            // [    FAN     ][    SWING    ]      third quarter
            // [ OFF ][        SEND        ]      last quarter
            const int q = W6 / 4, hh = H6 / 2, r3 = H6 * 3 / 4;
            add_tgt(i, AC_TDOWN, X,          Y,      q,          hh);
            add_tgt(i, AC_MODE,  X + q,      Y,      W6 - 2 * q, hh);
            add_tgt(i, AC_TUP,   X + W6 - q, Y,      q,          hh);
            add_tgt(i, AC_FAN,   X,          Y + hh, W6 / 2,     r3 - hh);
            add_tgt(i, AC_SWING, X + W6 / 2, Y + hh, W6 - W6 / 2, r3 - hh);
            add_tgt(i, AC_OFF,   X,          Y + r3, q + q / 2,  H6 - r3);
            add_tgt(i, AC_SEND,  X + q + q / 2, Y + r3, W6 - q - q / 2, H6 - r3);
        } else if (w->type == WT_ROCKER) {
            if (w->w > w->h) { add_tgt(i, RK_MINUS, X, Y, W6 / 2, H6); add_tgt(i, RK_PLUS, X + W6 / 2, Y, W6 - W6 / 2, H6); }
            else             { add_tgt(i, RK_PLUS, X, Y, W6, H6 / 2);  add_tgt(i, RK_MINUS, X, Y + H6 / 2, W6, H6 - H6 / 2); }
        } else add_tgt(i, 0, X, Y, W6, H6);
    }
    // Reading order (top-left first) so focus 0 is the top-left key.
    for (int a = 1; a < s_ntgt; a++) {
        rc_tgt_t t = s_tgt[a]; int b = a - 1;
        while (b >= 0 && (s_tgt[b].y > t.y || (s_tgt[b].y == t.y && s_tgt[b].x > t.x))) { s_tgt[b + 1] = s_tgt[b]; b--; }
        s_tgt[b + 1] = t;
    }
    if (s_foc >= s_ntgt) s_foc = 0;
}
// The nearest target in a direction: along-axis distance + twice the cross-axis offset, among those whose
// centre lies strictly that way. -1 = nothing there.
static int nav(int from, int dir)
{
    if (from < 0 || from >= s_ntgt) return s_ntgt ? 0 : -1;
    const rc_tgt_t *f = &s_tgt[from];
    const int fx = f->x * 2 + f->w, fy = f->y * 2 + f->h;
    int best = -1, bs = 1 << 30;
    for (int i = 0; i < s_ntgt; i++) {
        if (i == from) continue;
        const rc_tgt_t *t = &s_tgt[i];
        const int tx = t->x * 2 + t->w, ty = t->y * 2 + t->h;
        int along, cross;
        switch (dir) {
        case NK_UP:    along = fy - ty; cross = abs(tx - fx); break;
        case NK_DOWN:  along = ty - fy; cross = abs(tx - fx); break;
        case NK_LEFT:  along = fx - tx; cross = abs(ty - fy); break;
        default:       along = tx - fx; cross = abs(ty - fy); break;
        }
        if (along <= 0) continue;
        const int sc = along + 2 * cross;
        if (sc < bs) { bs = sc; best = i; }
    }
    return best;
}
static const char *part_name(const rc_widget_t *w, int part)
{
    static char b[LBL + 4];
    static const char *const DP[5] = { "Up", "Down", "Left", "Right", "OK" };
    if (w->type == WT_DPAD) return DP[part];
    if (w->type == WT_AC) {
        switch (part) {
        case AC_SEND:  return TR5("Accendi/invia", "On/send", "Encender", "Allumer", "Ein/senden");
        case AC_OFF:   return TR5("Spegni", "Off", "Apagar", "Eteindre", "Aus");
        case AC_TDOWN: return "Temp -";
        case AC_TUP:   return "Temp +";
        case AC_MODE:  return TR5("Modo", "Mode", "Modo", "Mode", "Modus");
        case AC_FAN:   return TR5("Ventola", "Fan", "Ventilador", "Ventil.", "Luefter");
        default:       return "Swing";
        }
    }
    if (w->type == WT_ROCKER) { snprintf(b, sizeof b, "%s%s", w->label[0] ? w->label : "", part == RK_PLUS ? "+" : "-"); return b; }
    return w->label[0] ? w->label : (w->icon ? ICONS[w->icon] : "?");
}

// =========================================================================================================
// Sending
// =========================================================================================================
// ---- Flipper .ir files: the signal is looked up by name and streamed from the SD at send time -------------
// The format ("Filetype: IR signals file"): records separated by '#', each "name:", "type: parsed|raw", then
// "protocol/address/command" (hex bytes, little-endian) or "frequency/data" (µs). A data line can be several KB
// (an AC frame), so it is read a character at a time into a buffer that exists only during the send.
static nir_proto_t flipper_proto(const char *s)
{
    static const struct { const char *f, *n; } MAP[] = {
        { "NEC", "nec" }, { "NECext", "necext" }, { "Samsung32", "samsung" }, { "SIRC", "sony12" },
        { "SIRC15", "sony15" }, { "SIRC20", "sony20" }, { "RC5", "rc5" }, { "RC5X", "rc5" } };
    for (size_t i = 0; i < sizeof MAP / sizeof MAP[0]; i++) if (!strcasecmp(s, MAP[i].f)) return nir_proto_from_name(MAP[i].n);
    return NIR_PROTO__COUNT;
}
// The rest of the line as text (trimmed), or skipped when out is null.
static int irf_value(FILE *f, char *out, int cap)
{
    int c, n = 0;
    while ((c = fgetc(f)) == ' ' || c == '\t') {}
    while (c != EOF && c != '\n') { if (out && c != '\r' && n < cap - 1) out[n++] = (char)c; c = fgetc(f); }
    if (out) { while (n && out[n - 1] == ' ') n--; out[n] = 0; }
    return c;
}
static uint32_t hex_le(const char *s)       // "E7 0C 00 00" -> 0x00000CE7
{
    uint32_t v = 0; int sh = 0;
    while (*s && sh < 32) { while (*s == ' ') s++; if (!*s) break; v |= (uint32_t)strtoul(s, (char **)&s, 16) << sh; sh += 8; }
    return v;
}
static const char *send_irfile(const rc_ref_t *r, int repeats)
{
    char path[sizeof RC_DIR + sizeof r->file + 2];
    snprintf(path, sizeof path, "%s/%s", RC_DIR, r->file);
    FILE *f = fopen(path, "rb");
    if (!f) return TR5("file .ir mancante", "missing .ir file", "falta el .ir", ".ir manquant", ".ir fehlt");
    bool match = false, raw = false, done = false;
    char key[16], val[40], proto[16] = "";
    uint32_t addr = 0, cmd = 0, freq = 38000;
    uint16_t *dur = NULL; int n = 0;
    int c = fgetc(f);
    while (c != EOF && !done) {
        int k = 0;
        while (c != EOF && c != ':' && c != '\n') { if (c != '\r' && k < (int)sizeof key - 1) key[k++] = (char)c; c = fgetc(f); }
        key[k] = 0;
        if (c != ':') { c = c == EOF ? EOF : fgetc(f); continue; }          // '#', blank or header line
        if (!strcmp(key, "name")) {
            if (match) break;                                                // the next record: ours is complete
            c = irf_value(f, val, sizeof val);
            match = !strcasecmp(val, r->sig);
        } else if (!match) c = irf_value(f, NULL, 0);
        else if (!strcmp(key, "type")) { c = irf_value(f, val, sizeof val); raw = !strcmp(val, "raw"); }
        else if (!strcmp(key, "protocol")) c = irf_value(f, proto, sizeof proto);
        else if (!strcmp(key, "address")) { c = irf_value(f, val, sizeof val); addr = hex_le(val); }
        else if (!strcmp(key, "command")) { c = irf_value(f, val, sizeof val); cmd = hex_le(val); }
        else if (!strcmp(key, "frequency")) { c = irf_value(f, val, sizeof val); freq = (uint32_t)strtoul(val, NULL, 10); }
        else if (!strcmp(key, "data")) {
            if (!dur) dur = (uint16_t *)malloc(IRF_MAX * sizeof(uint16_t));
            if (!dur) { fclose(f); return TR5("RAM esaurita", "out of RAM", "sin RAM", "RAM pleine", "kein RAM"); }
            uint32_t v = 0; bool in = false;
            while ((c = fgetc(f)) != EOF && c != '\n') {                     // a "data:" line may repeat: append
                if (isdigit(c)) { v = v * 10 + (uint32_t)(c - '0'); in = true; }
                else if (in) { if (n < IRF_MAX) dur[n++] = (uint16_t)(v > 65535 ? 65535 : v); v = 0; in = false; }
            }
            if (in && n < IRF_MAX) dur[n++] = (uint16_t)(v > 65535 ? 65535 : v);
        } else c = irf_value(f, NULL, 0);
        if (c == '\n') c = fgetc(f);
    }
    fclose(f);
    const char *err = NULL;
    if (!match) err = TR5("segnale assente", "signal not found", "senal no existe", "signal absent", "Signal fehlt");
    else if (raw) { if (n > 0) { if (nucleo_ir_send_raw(dur, n, (uint16_t)freq, repeats) != ESP_OK) err = "IR"; } else err = "RAW?"; }
    else {
        const nir_proto_t pr = flipper_proto(proto);
        if (pr == NIR_PROTO__COUNT) err = TR5("protocollo ignoto", "unknown protocol", "protocolo raro", "protocole inconnu", "Protokoll?");
        else {
            if (pr == NIR_PROTO_NEC || pr == NIR_PROTO_NECEXT || pr == NIR_PROTO_SAMSUNG) cmd &= 0xFF;   // the byte after is ~cmd
            if (nucleo_ir_send_proto(pr, addr, cmd, repeats) != ESP_OK) err = "IR";
        }
    }
    free(dur);
    return err;
}

// ---- Hitachi AC424 (RAR-8P2 remote, RAS-AJ22M): 53-byte packet, complementary pairs from byte 3, LSB first,
// 38 kHz. The wire format (byte offsets, constants) follows the public protocol description in
// IRremoteESP8266's ir_Hitachi.h; this builder is the one validated on the user's own unit (qubix qbx_ac.c).
#define AC_PULSES 854
static void ac_normalize(void)
{
    if (s_ac.temp < 16) s_ac.temp = 16;
    if (s_ac.temp > 32) s_ac.temp = 32;
    if (s_ac.mode > ACM_FAN) s_ac.mode = ACM_COOL;
    if (s_ac.fan < 1 || s_ac.fan > 5) s_ac.fan = 1;
    if (s_ac.mode == ACM_FAN && s_ac.fan == 5) s_ac.fan = 1;              // no "auto" in fan-only
    if (s_ac.mode == ACM_DRY && s_ac.fan > 2 && s_ac.fan != 5) s_ac.fan = 2;
}
static const char *ac_send(bool power, bool swing)
{
    static const uint8_t MODES[4] = { 3, 6, 5, 1 };
    ac_normalize();
    uint8_t pk[53];
    memset(pk, 0, sizeof pk);
    pk[0] = 0x01; pk[1] = 0x10; pk[3] = 0x40; pk[5] = 0xFF; pk[7] = 0xCC;
    pk[9]  = s_ac.fan == 1 ? 0x98 : 0x92;
    pk[11] = swing ? 0x81 : 0x13;
    pk[13] = (uint8_t)((s_ac.mode == ACM_FAN ? 27 : s_ac.temp) << 2);
    pk[25] = (uint8_t)((s_ac.fan << 4) | MODES[s_ac.mode]);
    pk[27] = (uint8_t)(0xE1 | ((power ? 1 : 0) << 4));
    pk[33] = 0x80; pk[35] = 0x03; pk[37] = 0x01; pk[39] = 0x88;
    for (int i = 45; i <= 51; i += 2) pk[i] = 0xFF;
    for (int i = 3; i < 53; i += 2) pk[i + 1] = pk[i] ^ 0xFF;
    uint16_t *w = (uint16_t *)malloc(AC_PULSES * sizeof(uint16_t));
    if (!w) return TR5("RAM esaurita", "out of RAM", "sin RAM", "RAM pleine", "kein RAM");
    int n = 0;
    w[n++] = 29784; w[n++] = 49290; w[n++] = 3416; w[n++] = 1604;       // wake + leader (the driver splits > 32.7 ms)
    for (int b = 0; b < 53; b++)
        for (int bit = 0; bit < 8; bit++) { w[n++] = 463; w[n++] = (pk[b] >> bit) & 1 ? 1208 : 372; }
    w[n++] = 463; w[n++] = 65535;                                         // stop mark + trailing gap
    const esp_err_t r = nucleo_ir_send_raw(w, n, 38000, 1);
    free(w);
    return r == ESP_OK ? NULL : "IR";
}
static void ac_path(char *out, int cap) { snprintf(out, cap, "%s/.ac_%s", RC_DIR, s_dev >= 0 && s_dev < s_ndev ? s_devs[s_dev].file : "x"); }
static void ac_load(void)
{
    char p[sizeof RC_DIR + FILE_LEN + 6]; ac_path(p, sizeof p);
    s_ac.temp = 23; s_ac.mode = ACM_COOL; s_ac.fan = 1; s_ac.on = false;     // "on" is never restored
    FILE *f = fopen(p, "rb"); if (!f) return;
    int t, m, fa;
    if (fscanf(f, "%d %d %d", &t, &m, &fa) == 3) { s_ac.temp = (uint8_t)t; s_ac.mode = (uint8_t)m; s_ac.fan = (uint8_t)fa; }
    fclose(f);
    ac_normalize();
}
static void ac_save(void)
{
    char p[sizeof RC_DIR + FILE_LEN + 6]; ac_path(p, sizeof p);
    FILE *f = fopen(p, "wb"); if (!f) return;
    fprintf(f, "%d %d %d\n", s_ac.temp, s_ac.mode, s_ac.fan);
    fclose(f);
}
static const char *ac_mode_name(int m)
{
    switch (m) {
    case ACM_COOL: return TR5("Freddo", "Cool", "Frio", "Froid", "Kuehlen");
    case ACM_HEAT: return TR5("Caldo", "Heat", "Calor", "Chaud", "Heizen");
    case ACM_DRY:  return TR5("Deumid.", "Dry", "Secar", "Deshum.", "Trocken");
    default:       return TR5("Ventola", "Fan", "Vent.", "Ventil.", "Luefter");
    }
}

static const char *s_last_err;   // why the last send failed (status line)
static bool fire_act(int ai)
{
    s_last_err = NULL;
    if (ai < 0 || ai >= s_nact) return false;
    const rc_act_t *a = &s_act[ai];
    esp_err_t r = ESP_FAIL;
    if (a->kind == AK_PROTO) r = nucleo_ir_send_proto((nir_proto_t)a->proto, a->addr, a->val, a->repeats);
    else if (a->kind == AK_RAW) r = nucleo_ir_send_raw(&s_raw[a->val], a->n, a->freq, a->repeats);
    else if (a->kind == AK_IRFILE) { s_last_err = send_irfile(&s_ref[a->val], a->repeats); return !s_last_err; }
    return r == ESP_OK;
}
static void flash(int tgt) { s_flash = tgt; s_tx = true; s_flash_until = esp_timer_get_time() + 180000; }

static void press_target(int ti)
{
    if (ti < 0 || ti >= s_ntgt) return;
    const rc_widget_t *w = &s_w[s_tgt[ti].wi];
    const int part = s_tgt[ti].part;
    flash(ti);
    if (w->type == WT_AC) {                       // adjust locally; only SEND / OFF / SWING transmit
        const char *err = NULL;
        switch (part) {
        case AC_TDOWN: if (s_ac.mode != ACM_FAN && s_ac.temp > 16) s_ac.temp--; s_tx = false; ac_save(); break;
        case AC_TUP:   if (s_ac.mode != ACM_FAN && s_ac.temp < 32) s_ac.temp++; s_tx = false; ac_save(); break;
        case AC_MODE:  s_ac.mode = (uint8_t)((s_ac.mode + 1) % 4); ac_normalize(); s_tx = false; ac_save(); break;
        case AC_FAN:   s_ac.fan = (uint8_t)(s_ac.fan % 5 + 1); ac_normalize(); s_tx = false; ac_save(); break;
        case AC_SEND:  err = ac_send(true, false);  if (!err) s_ac.on = true; break;
        case AC_OFF:   err = ac_send(false, false); if (!err) s_ac.on = false; break;
        case AC_SWING:
            if (!s_ac.on) { s_tx = false; snprintf(s_status, sizeof s_status, "%s", TR5("prima accendi", "send ON first", "enciende antes", "allumez d'abord", "erst EIN")); nucleo_app_request_draw(); return; }
            err = ac_send(true, true); break;
        }
        if (part == AC_SEND || part == AC_OFF || part == AC_SWING) snprintf(s_status, sizeof s_status, err ? "! %s" : "> %s", err ? err : part_name(w, part));
        else s_status[0] = 0;
        nucleo_app_request_draw();
        return;
    }
    const int ai = w->act[part];
    if (ai < 0) { snprintf(s_status, sizeof s_status, "%s", TR5("tasto vuoto", "no code", "sin codigo", "sans code", "kein Code")); }
    else if (s_act[ai].kind == AK_MACRO) {
        s_mac = ai; s_mac_pos = 0; s_mac_due = esp_timer_get_time();
        snprintf(s_status, sizeof s_status, "%s ...", part_name(w, s_tgt[ti].part));
    } else {
        const bool ok = fire_act(ai);
        if (!ok && s_last_err) snprintf(s_status, sizeof s_status, "! %s", s_last_err);
        else snprintf(s_status, sizeof s_status, ok ? "> %s" : "! %s", part_name(w, part));
    }
    nucleo_app_request_draw();
}
// Advance a running macro: one step per call when its time has come. True = something to repaint.
static bool macro_step(void)
{
    if (s_mac < 0) return false;
    const int64_t now = esp_timer_get_time();
    if (now < s_mac_due || nucleo_ir_busy()) return false;
    const rc_act_t *m = &s_act[s_mac];
    if (s_mac_pos >= m->n) { s_mac = -1; snprintf(s_status, sizeof s_status, "%s", TR5("macro fatta", "macro done", "macro hecha", "macro faite", "Makro fertig")); return true; }
    const rc_step_t *st = &s_step[m->val + s_mac_pos++];
    if (st->act >= 0 && s_act[st->act].kind != AK_MACRO) { fire_act(st->act); s_tx = true; s_flash_until = now + 150000; }
    s_mac_due = now + (int64_t)(st->delay ? st->delay : 250) * 1000;      // a gap between frames even without a delay
    return true;
}

// =========================================================================================================
// Icons — filled primitives in one ink colour, the ui_glyph recipe; the system glyphs where one exists
// =========================================================================================================
static void tri(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c) { d.fillTriangle(x0, y0, x1, y1, x2, y2, c); }
static void draw_icon(int ic, int cx, int cy, int r, uint16_t col, uint16_t bg)
{
    const int h = r / 2 > 0 ? r / 2 : 1, t = r / 4 > 0 ? r / 4 : 1;
    switch (ic) {
    case IC_POWER:    ui_glyph(&d, UG_POWER, cx, cy, r, col, bg); break;
    case IC_MUTE:     ui_glyph(&d, UG_MUTE, cx, cy, r, col, bg); break;
    case IC_VOL:      ui_glyph(&d, UG_SPEAKER, cx, cy, r, col, bg); break;
    case IC_INFO:     ui_glyph(&d, UG_INFO, cx, cy, r, col, bg); break;
    case IC_SETTINGS: ui_glyph(&d, UG_GEAR, cx, cy, r, col, bg); break;
    case IC_STAR:     ui_glyph(&d, UG_STAR, cx, cy, r, col, bg); break;
    case IC_MOON:     ui_glyph(&d, UG_MOON, cx, cy, r, col, bg); break;
    case IC_SUN:      ui_glyph(&d, UG_SUN, cx, cy, r, col, bg); break;
    case IC_TIMER:    ui_glyph(&d, UG_CLOCK, cx, cy, r, col, bg); break;
    case IC_MIC:      ui_glyph(&d, UG_MIC, cx, cy, r, col, bg); break;
    case IC_BT:       ui_glyph(&d, UG_BLUETOOTH, cx, cy, r, col, bg); break;
    case IC_TV:       ui_glyph(&d, UG_MONITOR, cx, cy, r, col, bg); break;
    case IC_SEARCH:   ui_glyph(&d, UG_SEARCH, cx, cy, r, col, bg); break;
    case IC_CHECK:    ui_glyph(&d, UG_CHECK, cx, cy, r, col, bg); break;
    case IC_LOCK:     ui_glyph(&d, UG_LOCK, cx, cy, r, col, bg); break;
    case IC_LIGHT:    ui_glyph(&d, UG_TORCH, cx, cy, r, col, bg); break;
    case IC_NEXT:     ui_glyph(&d, UG_NEXT, cx, cy, r, col, bg); break;
    case IC_PREV:     ui_glyph(&d, UG_PREV, cx, cy, r, col, bg); break;
    case IC_UP:       tri(cx, cy - h, cx - r + t, cy + h, cx + r - t, cy + h, col); break;
    case IC_DOWN:     tri(cx, cy + h, cx - r + t, cy - h, cx + r - t, cy - h, col); break;
    case IC_LEFT:     tri(cx - h, cy, cx + h, cy - r + t, cx + h, cy + r - t, col); break;
    case IC_RIGHT:    tri(cx + h, cy, cx - h, cy - r + t, cx - h, cy + r - t, col); break;
    case IC_OK:       d.fillCircle(cx, cy, r - t, col); d.fillCircle(cx, cy, r - 2 * t, bg); break;
    case IC_PLAY:     tri(cx - h, cy - r + t, cx - h, cy + r - t, cx + r - t, cy, col); break;
    case IC_PAUSE:    d.fillRect(cx - h - t / 2, cy - r + t, t + 1, 2 * (r - t), col); d.fillRect(cx + h - t / 2, cy - r + t, t + 1, 2 * (r - t), col); break;
    case IC_STOP:     d.fillRoundRect(cx - r + t, cy - r + t, 2 * (r - t), 2 * (r - t), t, col); break;
    case IC_REC:      d.fillCircle(cx, cy, r - t, COL_RED); break;
    case IC_REW:      tri(cx, cy - h, cx, cy + h, cx - r, cy, col); tri(cx + r, cy - h, cx + r, cy + h, cx, cy, col); break;
    case IC_FF:       tri(cx, cy - h, cx, cy + h, cx + r, cy, col); tri(cx - r, cy - h, cx - r, cy + h, cx, cy, col); break;
    case IC_HOME:     tri(cx, cy - r, cx - r, cy, cx + r, cy, col); d.fillRect(cx - r + t + 1, cy, 2 * (r - t) - 1, r - 1, col);
                      d.fillRect(cx - t / 2, cy + h / 2, t + 1, r - h / 2, bg); break;
    case IC_BACK:     tri(cx - r, cy - h, cx - t, cy - r, cx - t, cy, col); d.fillRect(cx - t, cy - h - t / 2, r, t + 1, col);
                      d.fillRect(cx + r - t - 1, cy - h - t / 2, t + 1, r, col); d.fillRect(cx - h, cy + h - t, r + h - 1, t + 1, col); break;
    case IC_MENU:     { for (int k = -1; k <= 1; k++) d.fillRoundRect(cx - r + t, cy + k * h - t / 2, 2 * (r - t), t + 1, 1, col); } break;
    case IC_LIST:     for (int k = -1; k <= 1; k++) { d.fillCircle(cx - r + t + 1, cy + k * h, t / 2 + 1, col); d.fillRect(cx - h + 1, cy + k * h - t / 2, r + h - t, t + 1, col); } break;
    case IC_INPUT:    d.drawRoundRect(cx - r, cy - r + t, 2 * r, 2 * r - 2 * t, t, col); tri(cx + h, cy, cx - t, cy - h, cx - t, cy + h, col);
                      d.fillRect(cx - r + t, cy - t / 2, h + t, t + 1, col); break;
    case IC_PLUS:     d.fillRect(cx - r + t, cy - t / 2, 2 * (r - t), t + 1, col); d.fillRect(cx - t / 2, cy - r + t, t + 1, 2 * (r - t), col); break;
    case IC_MINUS:    d.fillRect(cx - r + t, cy - t / 2, 2 * (r - t), t + 1, col); break;
    case IC_FAN:      for (int k = 0; k < 3; k++) { static const int8_t DX[3] = { 0, -5, 5 }, DY[3] = { -6, 4, 4 };
                          d.fillCircle(cx + DX[k] * r / 10, cy + DY[k] * r / 10, h, col); }
                      d.fillCircle(cx, cy, t, bg); break;
    case IC_MODE:     d.fillCircle(cx, cy, r - t, col); d.fillRect(cx - r, cy - r, r, 2 * r, bg); d.drawCircle(cx, cy, r - t, col); break;
    case IC_SWING:    { for (int k = -1; k <= 1; k++) for (int x = -r + t; x < r - t; x++) d.drawPixel(cx + x, cy + k * h + ((x / 2) & 1 ? 1 : -1) * t / 2, col); } break;
    default: break;
    }
}
// The device-type badge on the carousel card.
static void draw_dev_icon(int type, int cx, int cy, int r, uint16_t col, uint16_t bg)
{
    const int t = r / 5 > 1 ? r / 5 : 2;
    switch (type) {
    case DT_TV:        d.fillRoundRect(cx - r, cy - r * 3 / 4, 2 * r, r * 5 / 4, t, col); d.fillRect(cx - r + t, cy - r * 3 / 4 + t, 2 * r - 2 * t, r * 5 / 4 - 2 * t, bg);
                       d.fillRect(cx - t / 2, cy + r / 2, t, r / 3, col); d.fillRoundRect(cx - r / 2, cy + r * 3 / 4, r, t, 1, col); break;
    case DT_AC:        d.fillRoundRect(cx - r, cy - r * 2 / 3, 2 * r, r, t, col); d.fillRect(cx - r + t, cy + r / 6 - t, 2 * r - 2 * t, 1, bg);
                       { for (int k = -1; k <= 1; k++) d.fillRect(cx + k * r / 2 - 1, cy + r / 2, 2, r / 2, col); } break;
    case DT_AUDIO:     ui_glyph(&d, UG_SPEAKER, cx, cy, r, col, bg); break;
    case DT_LIGHT:     ui_glyph(&d, UG_SUN, cx, cy, r, col, bg); break;
    case DT_FAN:       draw_icon(IC_FAN, cx, cy, r, col, bg); break;
    case DT_PROJECTOR: d.fillRoundRect(cx - r, cy - r / 2, 2 * r, r, t, col); d.fillCircle(cx + r / 3, cy, r / 3, bg); d.fillCircle(cx + r / 3, cy, r / 6, col);
                       d.fillRect(cx - r + t, cy + r / 2, t, t, col); d.fillRect(cx + r - 2 * t, cy + r / 2, t, t, col); break;
    case DT_BOX:       d.fillRoundRect(cx - r, cy - r / 3, 2 * r, r * 2 / 3, t, col); d.fillCircle(cx + r / 2, cy, t / 2 + 1, bg);
                       d.fillRect(cx - r / 2, cy - 1, r / 2, 2, bg); break;
    default:           d.fillRoundRect(cx - r / 2, cy - r, r, 2 * r, r / 3, col);       // a remote: body, red power, keys
                       d.fillCircle(cx, cy - r / 2, t, COL_RED);
                       for (int k = 0; k < 3; k++) { d.fillCircle(cx - r / 5, cy + k * r / 3, t / 2 + 1, bg); d.fillCircle(cx + r / 5, cy + k * r / 3, t / 2 + 1, bg); }
                       break;
    }
}

// =========================================================================================================
// The keypad — one renderer for both screens: the grid's origin, cell size and fonts change, nothing else
// =========================================================================================================
typedef struct { int x0, y0, cw, rh, vis_y, vis_h; bool big; } geo_t;

static void cell_rect(const geo_t &g, int x6, int y6, int w6, int h6, int *x, int *y, int *w, int *h)
{
    *x = g.x0 + x6 * g.cw / 6; *y = g.y0 + y6 * g.rh / 6;
    *w = g.x0 + (x6 + w6) * g.cw / 6 - *x; *h = g.y0 + (y6 + h6) * g.rh / 6 - *y;
}
static void label_in(const char *s, int cx, int cy, int maxw, uint16_t col, uint16_t bg, bool big)
{
    char b[LBL + 2];
    d.setTextDatum(middle_center);
    d.setFont(big ? FNT(FreeSansBold9pt7b) : FNT(Font2));
    if (d.textWidth(s) > maxw) d.setFont(big ? FNT(Font2) : FNT(Font0));
    lg::fit(b, sizeof b, s, maxw);
    d.setTextColor(col, bg);
    d.drawString(b, cx, cy + 1);
    d.setTextDatum(top_left);
}
static uint16_t wcolor(const rc_widget_t *w) { return w->color ? w->color : (s_dcolor ? s_dcolor : ACCENT); }

static void draw_button(const geo_t &g, int ti)
{
    const rc_tgt_t *t = &s_tgt[ti]; const rc_widget_t *w = &s_w[t->wi];
    int x, y, cw, ch; cell_rect(g, t->x, t->y, t->w, t->h, &x, &y, &cw, &ch);
    const int pad = g.big ? 3 : 2; x += pad; y += pad; cw -= 2 * pad; ch -= 2 * pad;
    const bool focus = ti == s_foc, lit = ti == s_flash;
    const uint16_t col = wcolor(w), card = lg::card();
    const uint16_t fill = lit ? COL_FLASH : focus ? col : card, ink = (lit || focus) ? INK : FG;
    const int rad = ch < 20 ? 4 : g.big ? 10 : 7;
    d.fillRoundRect(x, y, cw, ch, rad, fill);
    if (!focus && !lit) d.drawRoundRect(x, y, cw, ch, rad, col == ACCENT && !w->color ? LINE : col);
    const int cx = x + cw / 2, cy = y + ch / 2;
    const int r = (ch < cw ? ch : cw) * (g.big ? 3 : 2) / (g.big ? 10 : 7);
    if (w->icon && w->label[0]) {
        d.setFont(g.big ? FNT(FreeSansBold9pt7b) : FNT(Font2));
        const int lw = d.textWidth(w->label), ir = r > 9 ? 9 : r, gap = 4, tot = 2 * ir + gap + lw;
        if (tot <= cw - 6) {                                   // icon + label side by side
            const uint16_t ic = (lit || focus) ? INK : col;
            draw_icon(w->icon, cx - tot / 2 + ir, cy, ir, ic, fill);
            d.setTextDatum(middle_left); d.setTextColor(ink, fill); d.drawString(w->label, cx - tot / 2 + 2 * ir + gap, cy + 1); d.setTextDatum(top_left);
        } else if (ch >= 34 && g.big) {                        // stacked on a tall panel key
            draw_icon(w->icon, cx, cy - 8, ir, (lit || focus) ? INK : col, fill);
            label_in(w->label, cx, cy + 12, cw - 6, ink, fill, false);
        } else draw_icon(w->icon, cx, cy, r, (lit || focus) ? INK : col, fill);
    } else if (w->icon) draw_icon(w->icon, cx, cy, r, (lit || focus) ? INK : col, fill);
    else label_in(w->label[0] ? w->label : "?", cx, cy, cw - 6, ink, fill, g.big);
    if (!g.big && w->key && ch >= 22 && cw >= 34) {             // its shortcut, small in the corner
        char k[2] = { (char)toupper((unsigned char)w->key), 0 };
        d.setFont(FNT(Font0)); d.setTextColor((lit || focus) ? INK : DIM, fill); d.drawString(k, x + cw - 7, y + 2);
    }
}
static void draw_dpad(const geo_t &g, int wi)
{
    const rc_widget_t *w = &s_w[wi];
    int x, y, bw, bh; cell_rect(g, w->x * 6, w->y * 6, w->w * 6, w->h * 6, &x, &y, &bw, &bh);
    const int cx = x + bw / 2, cy = y + bh / 2, R = (bw < bh ? bw : bh) / 2 - (g.big ? 3 : 2);
    const uint16_t col = wcolor(w), card = lg::card();
    d.fillCircle(cx, cy, R, card);
    d.drawCircle(cx, cy, R, s_direct ? col : LINE);
    if (s_direct) d.drawCircle(cx, cy, R - 1, col);
    int fpart = -1, lpart = -1;
    if (s_foc >= 0 && s_foc < s_ntgt && s_tgt[s_foc].wi == wi) fpart = s_tgt[s_foc].part;
    if (s_flash >= 0 && s_flash < s_ntgt && s_tgt[s_flash].wi == wi) lpart = s_tgt[s_flash].part;
    const int off = R * 2 / 3, ar = R / 4 > 3 ? R / 4 : 3, okr = R / 3;
    static const int8_t PX[4] = { 0, 0, -1, 1 }, PY[4] = { -1, 1, 0, 0 };
    static const uint8_t PIC[4] = { IC_UP, IC_DOWN, IC_LEFT, IC_RIGHT };
    for (int k = 0; k < 4; k++) {
        const int ax = cx + PX[k] * off, ay = cy + PY[k] * off;
        const bool on = k == fpart || k == lpart;
        if (on) d.fillCircle(ax, ay, ar + (g.big ? 6 : 4), k == lpart ? COL_FLASH : col);
        draw_icon(PIC[k], ax, ay, ar, on ? INK : (w->act[k] >= 0 ? FG : DIM), on ? (k == lpart ? COL_FLASH : col) : card);
    }
    const bool on = fpart == DP_OK || lpart == DP_OK;
    const uint16_t okf = lpart == DP_OK ? COL_FLASH : on ? col : LINE;
    d.fillCircle(cx, cy, okr, okf);
    d.setFont(g.big ? FNT(FreeSansBold9pt7b) : (okr >= 9 ? FNT(Font2) : FNT(Font0)));
    d.setTextDatum(middle_center); d.setTextColor(on ? INK : FG, okf); d.drawString("OK", cx, cy + 1); d.setTextDatum(top_left);
}
static void draw_rocker(const geo_t &g, int wi)
{
    const rc_widget_t *w = &s_w[wi];
    int x, y, bw, bh; cell_rect(g, w->x * 6, w->y * 6, w->w * 6, w->h * 6, &x, &y, &bw, &bh);
    const int pad = g.big ? 3 : 2; x += pad; y += pad; bw -= 2 * pad; bh -= 2 * pad;
    const bool horiz = bw > bh;
    const uint16_t col = wcolor(w), card = lg::card();
    const int rad = (horiz ? bh : bw) / 2;
    d.fillRoundRect(x, y, bw, bh, rad, card);
    d.drawRoundRect(x, y, bw, bh, rad, col);
    for (int part = 0; part < 2; part++) {
        int ti = -1;
        for (int i = 0; i < s_ntgt; i++) if (s_tgt[i].wi == wi && s_tgt[i].part == part) { ti = i; break; }
        const bool focus = ti >= 0 && ti == s_foc, lit = ti >= 0 && ti == s_flash;
        // the half's centre: plus on top (or right), minus below (or left)
        const int hx = horiz ? (part == RK_PLUS ? x + bw * 3 / 4 : x + bw / 4) : x + bw / 2;
        const int hy = horiz ? y + bh / 2 : (part == RK_PLUS ? y + bh / 4 : y + bh * 3 / 4);
        const int pr = ((horiz ? bh : bw) / 2) - 2;
        if (focus || lit) d.fillCircle(hx, hy, pr, lit ? COL_FLASH : col);
        draw_icon(part == RK_PLUS ? IC_PLUS : IC_MINUS, hx, hy, pr * 2 / 3 > 3 ? pr * 2 / 3 : 3,
                  (focus || lit) ? INK : FG, (focus || lit) ? (lit ? COL_FLASH : col) : card);
    }
    if (w->label[0] && (horiz ? bw : bh) >= (g.big ? 60 : 40)) {       // the name across the middle
        d.setFont(g.big ? FNT(Font2) : FNT(Font0));
        d.setTextDatum(middle_center); d.setTextColor(MUTED, card); d.drawString(w->label, x + bw / 2, y + bh / 2); d.setTextDatum(top_left);
    }
}
// The AC panel: what will be sent (never what the appliance did) — temperature as the hero, the mode under it,
// the fan as bars, and the two transmitting keys at the bottom; "on" lights the SEND key's lamp.
static void draw_ac(const geo_t &g, int wi)
{
    const rc_widget_t *w = &s_w[wi];
    const uint16_t col = wcolor(w), card = lg::card();
    for (int ti = 0; ti < s_ntgt; ti++) {
        if (s_tgt[ti].wi != wi) continue;
        const int part = s_tgt[ti].part;
        int x, y, bw, bh; cell_rect(g, s_tgt[ti].x, s_tgt[ti].y, s_tgt[ti].w, s_tgt[ti].h, &x, &y, &bw, &bh);
        const int pad = g.big ? 3 : 2; x += pad; y += pad; bw -= 2 * pad; bh -= 2 * pad;
        const bool focus = ti == s_foc, lit = ti == s_flash;
        const uint16_t keycol = part == AC_OFF ? COL_RED : part == AC_SEND ? COL_GREEN : col;
        const uint16_t fill = lit ? COL_FLASH : focus ? keycol : card, ink = (lit || focus) ? INK : FG;
        const int cx = x + bw / 2, cy = y + bh / 2;
        d.fillRoundRect(x, y, bw, bh, g.big ? 10 : 6, fill);
        if (!focus && !lit) d.drawRoundRect(x, y, bw, bh, g.big ? 10 : 6, part == AC_MODE ? col : LINE);
        char b[24];
        switch (part) {
        case AC_TDOWN: case AC_TUP:
            draw_icon(part == AC_TUP ? IC_PLUS : IC_MINUS, cx, cy, bw / 4 > 4 ? bw / 4 : 4, s_ac.mode == ACM_FAN ? DIM : ((lit || focus) ? INK : col), fill);
            break;
        case AC_MODE: {
            if (s_ac.mode == ACM_FAN) snprintf(b, sizeof b, "--");
            else snprintf(b, sizeof b, "%d", s_ac.temp);
            d.setTextDatum(middle_center);
            d.setFont(g.big ? FNT(FreeSansBold18pt7b) : FNT(FreeSansBold12pt7b));
            const int ty = cy - (g.big ? 10 : 6);
            d.setTextColor(ink, fill); d.drawString(b, cx - 6, ty);
            const int tw = d.textWidth(b);
            d.setFont(FNT(Font0)); d.drawString("C", cx - 6 + tw / 2 + 6, ty - (g.big ? 8 : 5));
            d.drawCircle(cx - 6 + tw / 2 + 2, ty - (g.big ? 11 : 8), 1, ink);
            d.setFont(g.big ? FNT(Font2) : FNT(Font0));
            d.setTextColor((lit || focus) ? INK : col, fill);
            d.drawString(ac_mode_name(s_ac.mode), cx, y + bh - (g.big ? 10 : 6));
            d.setTextDatum(top_left);
            break;
        }
        case AC_FAN: {
            d.setFont(g.big ? FNT(Font2) : FNT(Font0)); d.setTextDatum(middle_left); d.setTextColor(ink, fill);
            const char *fl = s_ac.fan == 5 ? "Auto" : TR5("Vent", "Fan", "Vent", "Vent", "Luft");
            d.drawString(fl, x + 5, cy + 1); d.setTextDatum(top_left);
            const int bx = x + 8 + d.textWidth(fl), bwid = g.big ? 5 : 3, step = bwid + 2;
            for (int k = 0; k < 4; k++) {                                     // 4 bars, filled to the level (auto: none)
                const int hgt = (bh - 8) * (k + 1) / 4;
                const bool on = s_ac.fan != 5 && k < s_ac.fan;
                d.fillRect(bx + k * step, y + bh - 4 - hgt, bwid, hgt, on ? ((lit || focus) ? INK : col) : LINE);
            }
            break;
        }
        case AC_SWING:
            draw_icon(IC_SWING, cx - (g.big ? 22 : 0), cy, bh / 3 > 4 ? bh / 3 : 4, s_ac.on ? ((lit || focus) ? INK : col) : DIM, fill);
            if (g.big) label_in("Swing", cx + 16, cy, bw / 2, s_ac.on ? ink : DIM, fill, false);
            break;
        case AC_OFF:
            draw_icon(IC_POWER, cx, cy, bh / 3 > 4 ? bh / 3 : 4, (lit || focus) ? INK : COL_RED, fill);
            break;
        default:                                                              // SEND: lamp + label
            d.fillCircle(x + (g.big ? 14 : 8), cy, g.big ? 5 : 3, s_ac.on ? COL_TX : LINE);
            label_in(TR5("INVIA", "SEND", "ENVIAR", "ENVOI", "SENDEN"), cx + 4, cy, bw - 20, ink, fill, g.big);
            break;
        }
    }
}

// Paint the page's keys whose rows fall inside the visible band.
static void draw_keys(const geo_t &g)
{
    for (int i = 0; i < s_nw; i++) {
        const rc_widget_t *w = &s_w[i];
        if (w->page != s_page) continue;
        int x, y, bw, bh; cell_rect(g, w->x * 6, w->y * 6, w->w * 6, w->h * 6, &x, &y, &bw, &bh);
        if (y < g.vis_y || y + bh > g.vis_y + g.vis_h + 1) continue;    // clipped rows stay hidden (scroll shows them)
        if (w->type == WT_DPAD) draw_dpad(g, i);
        else if (w->type == WT_ROCKER) draw_rocker(g, i);
        else if (w->type == WT_AC) draw_ac(g, i);
    }
    for (int ti = 0; ti < s_ntgt; ti++) {
        const rc_widget_t *w = &s_w[s_tgt[ti].wi];
        if (w->type != WT_BTN) continue;
        int x, y, bw, bh; cell_rect(g, s_tgt[ti].x, s_tgt[ti].y, s_tgt[ti].w, s_tgt[ti].h, &x, &y, &bw, &bh);
        if (y < g.vis_y || y + bh > g.vis_y + g.vis_h + 1) continue;
        draw_button(g, ti);
    }
}
// Keep the focused key's whole widget in view: scroll is in grid rows.
static void follow_focus(int vis_rows)
{
    if (s_foc < 0 || s_foc >= s_ntgt) return;
    const rc_widget_t *w = &s_w[s_tgt[s_foc].wi];
    if (w->y < s_scroll) s_scroll = w->y;
    if (w->y + w->h > s_scroll + vis_rows) s_scroll = w->y + w->h - vis_rows;
    if (s_scroll > s_rows - vis_rows) s_scroll = s_rows - vis_rows;
    if (s_scroll < 0) s_scroll = 0;
}

// =========================================================================================================
// Compact (Cardputer 240x135)
// =========================================================================================================
static void set_hint(void)
{
    if (s_view == 0) nucleo_app_set_hint(TR5("</> scegli   invio apri   esc esci", "</> pick   enter open   esc back", "</> elige   intro abre   esc sale",
                                              "</> choix   entree ouvre   esc", "</> wahl   enter oeffne   esc"));
    else if (s_direct) nucleo_app_set_hint(TR5("frecce d-pad   invio ok   spazio fine", "arrows d-pad   enter ok   space done", "flechas d-pad   intro ok   esp fin",
                                                "fleches d-pad  entree ok  esp fin", "pfeile d-pad  enter ok  leer ende"));
    else nucleo_app_set_hint(TR5("frecce   invio invia   tab pagina   esc", "arrows   enter send   tab page   esc", "flechas  intro envia  tab pag  esc",
                                 "fleches  entree envoi  tab page  esc", "pfeile  enter senden  tab seite  esc"));
}

static void draw_carousel(int top, int H)
{
    if (!s_ndev) {
        d.setTextDatum(middle_center);
        d.setFont(FNT(FreeSansBold9pt7b)); d.setTextColor(FG, BG);
        d.drawString(TR5("Nessun telecomando", "No remotes yet", "Sin mandos", "Aucune telecommande", "Keine Fernbedienung"), 120, top + H / 2 - 18);
        d.setFont(FNT(Font0)); d.setTextColor(MUTED, BG);
        d.drawString(TR5("metti i .json in", "put device .json files in", "pon los .json en", "mettez les .json dans", "lege .json-Dateien in"), 120, top + H / 2 + 4);
        d.setTextColor(ACCENT, BG); d.drawString("/sd/data/remocon/", 120, top + H / 2 + 18);
        d.setTextDatum(top_left);
        return;
    }
    const int cw = 156, ch = H - 20, cx = 120, cy = top + 4 + ch / 2;
    const uint16_t card = lg::card();
    // neighbours peeking in at the sides, dimmer and smaller
    for (int side = -1; side <= 1; side += 2) {
        if (s_ndev < 2) break;
        const int i = (s_dev + side + s_ndev) % s_ndev;
        const int nx = side < 0 ? 0 : 240 - 34;
        d.fillRoundRect(nx - (side < 0 ? 10 : 0), top + 14, 44, ch - 20, 8, card);
        draw_dev_icon(s_devs[i].type, nx + (side < 0 ? 12 : 22), top + 14 + (ch - 20) / 2, 9, DIM, card);
    }
    // the focused device card
    const rc_dev_t *dv = &s_devs[s_dev];
    const int x = cx - cw / 2, y = top + 4;
    d.fillRoundRect(x, y, cw, ch, 12, card);
    d.fillRoundRect(x, y, 6, ch, 3, dv->color);                        // its colour as a spine
    d.fillCircle(x + 34, cy - 6, 22, BG);
    draw_dev_icon(dv->type, x + 34, cy - 6, 14, dv->color, BG);
    d.setFont(FNT(FreeSansBold9pt7b));
    char nm[DNAME + 2]; lg::fit(nm, sizeof nm, dv->name, cw - 70);
    d.setTextColor(FG, card); d.drawString(nm, x + 62, y + 12);
    d.setFont(FNT(Font0)); d.setTextColor(MUTED, card);
    char sub[32]; snprintf(sub, sizeof sub, TR5("%d tasti", "%d keys", "%d teclas", "%d touches", "%d Tasten"), dv->nkeys);
    d.drawString(sub, x + 62, y + 36);
    snprintf(sub, sizeof sub, TR5("%d pagine", "%d pages", "%d paginas", "%d pages", "%d Seiten"), dv->npages ? dv->npages : 1);
    d.drawString(sub, x + 62, y + 48);
    d.setTextColor(dv->color, card); d.drawString(DTYPES[dv->type], x + 62, y + 62);
    // position dots
    const int n = s_ndev > 12 ? 12 : s_ndev, dx0 = 120 - (n - 1) * 5;
    for (int i = 0; i < n; i++) {
        const bool on = s_ndev > 12 ? (i == s_dev * 12 / s_ndev) : (i == s_dev);
        d.fillCircle(dx0 + i * 10, top + H - 7, on ? 3 : 2, on ? ACCENT : LINE);
    }
}

static void draw_header(int top)
{
    const uint16_t col = s_dcolor ? s_dcolor : ACCENT;
    d.fillRect(0, top, 240, 16, BG);
    d.fillCircle(7, top + 7, 4, s_tx ? COL_TX : LINE);                // the TX lamp
    d.setFont(FNT(Font2)); d.setTextColor(col, BG);
    char nm[DNAME + 2]; lg::fit(nm, sizeof nm, s_dname, 104);
    d.drawString(nm, 16, top);
    d.setTextDatum(top_right);
    if (s_direct) { d.setTextColor(INK, col); d.fillRoundRect(240 - 52, top + 1, 50, 13, 6, col); d.setFont(FNT(Font0)); d.drawString("D-PAD", 240 - 9, top + 4); }
    else if (s_status[0]) { d.setFont(FNT(Font0)); d.setTextColor(s_status[0] == '!' ? COL_RED : COL_TX, BG); char st[24]; lg::fit(st, sizeof st, s_status, 110); d.drawString(st, 238, top + 4); }
    else if (s_npage > 1) {
        d.setFont(FNT(Font0)); d.setTextColor(MUTED, BG);
        char pg[LBL + 8]; snprintf(pg, sizeof pg, "%s  %d/%d", s_pname[s_page], s_page + 1, s_npage); d.drawString(pg, 238, top + 4);
    }
    d.setTextDatum(top_left);
    if (s_npage > 1) for (int i = 0; i < s_npage; i++)                // page pips on the rule
        d.fillRect(120 - s_npage * 7 + i * 14, top + 15, 12, 1, i == s_page ? col : LINE);
    else d.drawFastHLine(0, top + 15, 240, LINE);
}

static void draw_remote(int top, int H)
{
    draw_header(top);
    const int gy = top + 17, gh = H - 17;
    d.fillRect(0, gy, 240, gh, BG);
    if (!s_ntgt) {
        d.setTextDatum(middle_center); d.setFont(FNT(Font2)); d.setTextColor(MUTED, BG);
        d.drawString(TR5("pagina vuota", "empty page", "pagina vacia", "page vide", "leere Seite"), 120, gy + gh / 2); d.setTextDatum(top_left);
        return;
    }
    int rh = gh / (s_rows ? s_rows : 1);
    if (rh > 34) rh = 34;
    if (rh < 26) rh = 26;
    const int vis = gh / rh;
    follow_focus(vis);
    const int cw = (240 - 4) / s_cols;
    const int used = (s_rows < vis ? s_rows : vis) * rh;
    geo_t g = { 2 + (240 - 4 - cw * s_cols) / 2, gy + (gh - used) / 2 - s_scroll * rh, cw, rh, gy, gh, false };
    if (s_rows > vis) g.y0 = gy - s_scroll * rh;
    draw_keys(g);
    if (s_rows > vis) {                                                // scroll knob
        const int track = gh - 4;
        d.fillRect(238, gy + 2, 2, track, LINE);
        d.fillRect(238, gy + 2 + track * s_scroll / s_rows, 2, track * vis / s_rows, s_dcolor ? s_dcolor : ACCENT);
    }
}

static void draw_error(int top, int H)
{
    d.setTextDatum(middle_center);
    d.setFont(FNT(FreeSansBold9pt7b)); d.setTextColor(COL_RED, BG);
    d.drawString(TR5("File non valido", "Invalid file", "Archivo invalido", "Fichier invalide", "Ungueltige Datei"), 120, top + H / 2 - 14);
    d.setFont(FNT(Font2)); d.setTextColor(MUTED, BG); d.drawString(s_err, 120, top + H / 2 + 8);
    d.setTextDatum(top_left);
}

static void draw_companion(void);
static void on_draw(void)
{
    if (nucleo_app_two_screens()) { draw_companion(); return; }
    const int top = nucleo_app_content_top(), H = nucleo_app_content_height();
    d.fillRect(0, top, 240, H, BG);
    if (s_view == 1) draw_remote(top, H);
    else if (s_err[0]) draw_error(top, H);
    else draw_carousel(top, H);
}

// =========================================================================================================
// Two screens: the panel is the remote itself (320x240, touch: tap a key to send it); the Cardputer is the
// companion (device, focused key, last sent, keys).
// =========================================================================================================
static const char *dev_label(int i, void *) { return (i >= 0 && i < s_ndev) ? s_devs[i].name : ""; }
static const char *dev_right(int i, void *) { static char b[8]; b[0] = 0; if (i >= 0 && i < s_ndev) snprintf(b, sizeof b, "%d", s_devs[i].nkeys); return b; }
static uint16_t dev_color(int i, void *) { return (i >= 0 && i < s_ndev) ? s_devs[i].color : FG; }

static geo_t s_lg;                    // the panel grid of the last frame (for the tap hit test)

static void draw_large(void)
{
    const int W = d.width(), H = d.height();
    nucleo_touch_clear();
    if (s_view == 0) {
        int y = lg::header("RemoCon", nullptr, ACCENT);
        d.fillRect(0, y, W, H - y, BG);
        if (!s_ndev) { lg::message(0, y, W, H - y, TR5("Nessun telecomando", "No remotes yet", "Sin mandos", "Aucune telecommande", "Keine Fernbedienung"), "/sd/data/remocon/*.json", MUTED); nucleo_touch_commit(); return; }
        const int lw = 160;
        lg::text_list(0, y + 2, lw, H - y - 4, s_ndev, s_dev, &s_lg_scroll, dev_label, dev_right, dev_color, nullptr, ACCENT);
        const int px = lw + 6, pw = W - lw - 12, py = y + 6, ph = H - y - 12;
        lg::panel(px, py, pw, ph); const uint16_t cb = lg::card();
        const rc_dev_t *dv = &s_devs[s_dev];
        d.fillCircle(px + pw / 2, py + 40, 28, BG);
        draw_dev_icon(dv->type, px + pw / 2, py + 40, 18, dv->color, BG);
        lg::font(1); d.setTextColor(FG, cb); d.setTextDatum(top_center);
        char nm[DNAME + 2]; lg::fit(nm, sizeof nm, dv->name, pw - 16); d.drawString(nm, px + pw / 2, py + 76);
        lg::font(0); d.setTextColor(MUTED, cb);
        char sub[32]; snprintf(sub, sizeof sub, TR5("%d tasti", "%d keys", "%d teclas", "%d touches", "%d Tasten"), dv->nkeys);
        d.drawString(sub, px + pw / 2, py + 98); d.setTextDatum(top_left);
        lg::button(px + 10, py + ph - 46, pw - 20, 36, TR5("Apri", "Open", "Abrir", "Ouvrir", "Oeffnen"), dv->color, INK, NK_ENTER, 0);
        nucleo_touch_commit();
        return;
    }
    const uint16_t col = s_dcolor ? s_dcolor : ACCENT;
    int y = lg::header(s_dname, s_status[0] ? s_status : (s_direct ? "D-PAD" : nullptr), col);
    if (s_npage > 1) {
        const char *names[MAX_PAGE]; for (int i = 0; i < s_npage; i++) names[i] = s_pname[i];
        y = lg::tabs(y + 2, names, s_npage, s_page, col, "\x01\x02\x03\x04\x05\x06");
    }
    d.fillRect(0, y, W, H - y, BG);
    const int gh = H - y - 4;
    int rh = gh / (s_rows ? s_rows : 1);
    if (rh > 56) rh = 56;
    if (rh < 38) rh = 38;
    const int vis = gh / rh;
    follow_focus(vis);
    const int cw = (W - 8) / s_cols;
    const int used = (s_rows < vis ? s_rows : vis) * rh;
    s_lg = { 4 + (W - 8 - cw * s_cols) / 2, y + 2 + (gh - used) / 2, cw, rh, y + 2, gh, true };
    if (s_rows > vis) s_lg.y0 = y + 2 - s_scroll * rh;
    draw_keys(s_lg);
    lg::field(0, y, W, H - y);                                         // the keypad IS the content: tap a key
    if (s_rows > vis) nucleo_touch_add_scroll(W - 24, y, 24, H - y, rh, NK_UP, 0, NK_DOWN, 0);
    nucleo_touch_commit();
}
static int hit_large(int px, int py)
{
    for (int i = 0; i < s_ntgt; i++) {
        int x, y, w, h; cell_rect(s_lg, s_tgt[i].x, s_tgt[i].y, s_tgt[i].w, s_tgt[i].h, &x, &y, &w, &h);
        if (y < s_lg.vis_y || y + h > s_lg.vis_y + s_lg.vis_h + 1) continue;
        if (px >= x && px < x + w && py >= y && py < y + h) return i;
    }
    return -1;
}

static void draw_companion(void)
{
    const int top = nucleo_app_content_top(), h = nucleo_app_content_height();
    d.fillRect(0, top, 240, h, BG);
    const int y = lg::comp_head("REMOCON", ACCENT);
    if (s_view == 0) {
        d.setFont(FNT(FreeSansBold9pt7b)); d.setTextColor(FG, BG);
        d.drawString(s_ndev ? s_devs[s_dev].name : TR5("Nessun telecomando", "No remotes yet", "Sin mandos", "Aucune telecommande", "Keine Fernbedienung"), 8, y + 6);
        lg::comp_keys(TR5("su/giu scegli   invio apri", "up/dn pick   enter open", "arr/ab elige   intro abre", "haut/bas choix  entree", "auf/ab wahl   enter"));
        return;
    }
    const uint16_t col = s_dcolor ? s_dcolor : ACCENT;
    d.setFont(FNT(FreeSansBold9pt7b)); d.setTextColor(col, BG); d.drawString(s_dname, 8, y + 2);
    d.setFont(FNT(Font0)); d.setTextColor(MUTED, BG);
    if (s_npage > 1) d.drawString(s_pname[s_page], 8, y + 22);
    if (s_foc >= 0 && s_foc < s_ntgt) {
        d.setFont(FNT(FreeSansBold12pt7b)); d.setTextColor(FG, BG);
        d.drawString(part_name(&s_w[s_tgt[s_foc].wi], s_tgt[s_foc].part), 8, y + 34);
    }
    if (s_status[0]) { d.setFont(FNT(Font2)); d.setTextColor(s_status[0] == '!' ? COL_RED : COL_TX, BG); d.drawString(s_status, 8, y + 62); }
    d.fillCircle(230, y + 8, 5, s_tx ? COL_TX : LINE);
    lg::comp_keys(s_direct ? "D-PAD   ENTER ok   SPACE done" : "arrows   ENTER send   TAB page");
}

// =========================================================================================================
// Input
// =========================================================================================================
static void open_device(int i, int page)
{
    if (i < 0 || i >= s_ndev) return;
    s_dev = i;
    s_mac = -1; s_flash = -1; s_tx = false; s_direct = false; s_status[0] = 0;
    if (!load_device(s_devs[i].file, true, nullptr)) { s_view = 0; nucleo_app_request_draw(); return; }
    s_view = 1; s_page = (page >= 0 && page < s_npage) ? page : 0; s_foc = 0; s_scroll = 0;
    ac_load();
    build_targets();
    save_state();
    set_hint();
    nucleo_app_request_draw();
}
static void set_page(int pg)
{
    if (pg < 0 || pg >= s_npage || pg == s_page) return;
    s_page = pg; s_foc = 0; s_scroll = 0; s_direct = false; s_status[0] = 0;
    build_targets(); save_state(); set_hint();
    nucleo_app_request_draw();
}
static int dpad_target(int part)
{
    for (int i = 0; i < s_ntgt; i++) if (s_w[s_tgt[i].wi].type == WT_DPAD && s_tgt[i].part == part) return i;
    return -1;
}
static void move(int dir)
{
    if (s_direct) {
        const int part = dir == NK_UP ? DP_UP : dir == NK_DOWN ? DP_DOWN : dir == NK_LEFT ? DP_LEFT : DP_RIGHT;
        const int t = dpad_target(part);
        if (t >= 0) { s_foc = t; press_target(t); }
        return;
    }
    const int n = nav(s_foc, dir);
    if (n >= 0) { s_foc = n; s_status[0] = 0; nucleo_app_request_draw(); }
}
static bool shortcut(char ch)
{
    const char c = (char)tolower((unsigned char)ch);
    for (int i = 0; i < s_ntgt; i++) {
        const rc_widget_t *w = &s_w[s_tgt[i].wi];
        const bool hit = (w->type == WT_BTN && w->key == c) ||
                         (w->type == WT_ROCKER && ((s_tgt[i].part == RK_PLUS && w->key == c) || (s_tgt[i].part == RK_MINUS && w->key2 == c)));
        if (hit) { s_foc = i; press_target(i); return true; }
    }
    return false;
}

static void on_key(int k, char ch)
{
    int px, py;
    if (lg::tap(k, ch, &px, &py)) {                                    // panel: a tapped key fires at once
        if (s_view == 1) { const int t = hit_large(px, py); if (t >= 0) { s_foc = t; press_target(t); } }
        return;
    }
    if (s_view == 0) {
        if (!s_ndev) return;
        if (nucleo_touch_is_row(k, ch)) {
            const int t = lg::row_tap(k, ch, s_lg_scroll, s_ndev, &s_dev);
            if (t == 2) open_device(s_dev, 0); else if (t == 1) nucleo_app_request_draw();
            return;
        }
        if (k == NK_RIGHT || k == NK_DOWN) { s_dev = (s_dev + 1) % s_ndev; nucleo_app_request_draw(); }
        else if (k == NK_UP) { s_dev = (s_dev + s_ndev - 1) % s_ndev; nucleo_app_request_draw(); }
        else if (k == NK_ENTER) open_device(s_dev, 0);
        else if (k == NK_CHAR && ch >= '1' && ch <= '9' && ch - '1' < s_ndev) open_device(ch - '1', 0);
        return;
    }
    if (k == NK_CHAR && ch >= 1 && ch <= MAX_PAGE) { set_page(ch - 1); return; }      // a panel tab
    switch (k) {
    case NK_UP: case NK_DOWN: case NK_RIGHT: move(k); return;
    case NK_ENTER:
        if (s_direct) { const int t = dpad_target(DP_OK); if (t >= 0) { s_foc = t; press_target(t); } }
        else press_target(s_foc);
        return;
    case NK_CHAR:
        if (ch == ' ') {
            if (dpad_target(DP_OK) >= 0) { s_direct = !s_direct; s_status[0] = 0; set_hint(); nucleo_app_request_draw(); }
            return;
        }
        if (shortcut(ch)) return;
        if (!s_direct && ch >= '1' && ch <= '0' + MAX_PAGE) set_page(ch - '1');        // digits not bound to a key: pages
        return;
    default: return;
    }
}
static bool on_back(int key)
{
    if (s_view == 0) {
        if (key == NK_LEFT && s_ndev) { s_dev = (s_dev + s_ndev - 1) % s_ndev; nucleo_app_request_draw(); return true; }
        return key == NK_LEFT;                    // LEFT never closes the app; Esc does
    }
    if (key == NK_LEFT) { move(NK_LEFT); return true; }
    if (s_direct) { s_direct = false; set_hint(); nucleo_app_request_draw(); return true; }
    s_view = 0; s_mac = -1; s_status[0] = 0; s_err[0] = 0;
    set_hint(); nucleo_app_request_draw();
    return true;
}
static void on_tab(void) { if (s_view == 1 && s_npage > 1) set_page((s_page + 1) % s_npage); }

// Macros + the key flash run here (~50 Hz): true = repaint now.
static bool on_poll(void)
{
    bool dirty = macro_step();
    if ((s_flash >= 0 || s_tx) && esp_timer_get_time() >= s_flash_until && s_mac < 0) { s_flash = -1; s_tx = false; dirty = true; }
    else if (s_tx && s_mac >= 0 && esp_timer_get_time() >= s_flash_until) { s_tx = false; dirty = true; }
    return dirty;
}

// =========================================================================================================
// Lifecycle
// =========================================================================================================
static void on_enter(void)
{
    s_view = 0; s_dev = 0; s_page = 0; s_foc = 0; s_scroll = 0; s_lg_scroll = 0; s_direct = false;
    s_mac = -1; s_flash = -1; s_tx = false; s_status[0] = 0; s_err[0] = 0;
    s_nw = s_nact = s_nraw = s_nstep = s_ntgt = s_nref = 0;
    mkdir(NUCLEO_SD_MOUNT "/data", 0775); mkdir(RC_DIR, 0775);
    scan_devices();
    nucleo_app_set_back_handler(on_back);
    nucleo_app_set_tab_handler(on_tab);
    nucleo_app_set_poll_handler(on_poll);
    int dv, pg;
    if (load_state(&dv, &pg)) open_device(dv, pg);                     // resume where the user left
    set_hint();
    nucleo_app_request_draw();
}
static void on_exit(void) { s_mac = -1; }

static const nucleo_app_ram_t APP_RAM[] = {
    { (void **)&s_devs, sizeof(rc_dev_t) * MAX_DEV },
    { (void **)&s_w,    sizeof(rc_widget_t) * MAX_W },
    { (void **)&s_act,  sizeof(rc_act_t) * MAX_ACT },
    { (void **)&s_raw,  sizeof(uint16_t) * MAX_RAW },
    { (void **)&s_step, sizeof(rc_step_t) * MAX_STEP },
    { (void **)&s_tgt,  sizeof(rc_tgt_t) * MAX_TGT },
    { (void **)&s_ref,  sizeof(rc_ref_t) * MAX_REF },
    { nullptr, 0 } };

extern "C" void nucleo_register_remocon(void)
{
    static const nucleo_app_def_t app = {
        "remocon", "RemoCon", "Tools", "Your own IR remotes: JSON devices + layouts from the SD card",
        'C', ACCENT,
        on_enter, on_key, nullptr, on_draw, on_exit, 0, APP_RAM, draw_large
    };
    nucleo_app_register(&app);
}
