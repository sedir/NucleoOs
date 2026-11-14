// Keyboard — the Cardputer as a real keyboard for a PC, phone or tablet, over USB-C AND Bluetooth at once.
//
// One app, two links: TinyUSB HID on the USB-C port and HID-over-GATT (BLE, the S3 has no Classic BT) as a
// "NucleoOS" keyboard. Every keystroke goes to every link that is up (or only to the one picked in Target),
// so the same device types into the laptop on the cable and the tablet across the room without a menu.
//
// - Layout-aware: BR (ABNT2, the default), US or IT. The Cardputer types characters; nucleo_ducky turns
//   each one into the physical key + modifier that prints it on THAT host layout. On BR, ~ ^ ` stay dead
//   keys exactly like a real ABNT2 board, so ~ then a gives "a with tilde" on the host: Portuguese
//   accents for free from an ASCII keyboard.
// - Fn layer (same as the USB Keyboard app): F1-F12, arrows, forward-Delete, Esc.
// - Settings persist in /sd/system/config/keyboard.json.
//
// NX_SOLO | NX_DEEP_OFFLINE | NX_BLE: opening reboots once into a Solo boot with Wi-Fi down and the BLE
// controller kept (the BLE suite's policy), which leaves a fresh heap for NimBLE + TinyUSB + the canvas.
// TinyUSB takes the USB PHY, so the serial console is gone until the exit reboot.
#include "nucleo_app.h"
#include "launcher_theme.h"
#include "nucleo_i18n.h"
#include "app_gfx.h"
#include "app_large.h"
#include "nucleo_touch.h"
#include "nucleo_exclusive.h"
#include <M5GFX.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include "esp_task_wdt.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
extern "C" {
#include "nucleo_kbd.h"
#include "nucleo_usbhid.h"
#include "nucleo_ble.h"
#include "nucleo_ducky.h"
}

static const unsigned short ACC = C_BLUE, GRN = C_GREEN, WARN = C_YELLOW;

#define HID_ESC   0x29
#define HID_DELfw 0x4C
static inline uint8_t HID_F(int n) { return (uint8_t)(0x3A + (n - 1)); }

enum Target { TG_BOTH, TG_USB, TG_BT, TG_N };
static nucleo_ducky_layout_t s_layout = DUCKY_LAYOUT_BR;
static Target s_target = TG_BOTH;
static bool s_usb_ok = false, s_bt_ok = false;
static bool s_typing = false;
static unsigned char s_tmods = 0;
static char s_echo[28] = "";
static int s_last_state = -1;   // packed link state last painted (on_tick repaints only on change)

#define CFG_DIR "/sd/system/config"
#define CFG     CFG_DIR "/keyboard.json"

static void cfg_load(void)
{
    FILE *f = fopen(CFG, "rb"); if (!f) return;
    char buf[64]; int n = (int)fread(buf, 1, sizeof buf - 1, f); fclose(f);
    if (n <= 0) return;
    buf[n] = 0;
    int lay = -1, tg = -1;
    if (sscanf(buf, "{\"layout\":%d,\"target\":%d}", &lay, &tg) == 2) {
        if (lay >= DUCKY_LAYOUT_US && lay <= DUCKY_LAYOUT_BR) s_layout = (nucleo_ducky_layout_t)lay;
        if (tg >= 0 && tg < TG_N) s_target = (Target)tg;
    }
}

static void cfg_save(void)
{
    mkdir("/sd/system", 0775); mkdir(CFG_DIR, 0775);
    FILE *f = fopen(CFG, "wb"); if (!f) return;
    fprintf(f, "{\"layout\":%d,\"target\":%d}\n", (int)s_layout, (int)s_target);
    fclose(f);
}

static const char *layout_name(void)
{
    return s_layout == DUCKY_LAYOUT_BR ? "BR ABNT2" : s_layout == DUCKY_LAYOUT_IT ? "IT" : "US";
}
static const char *target_name(void)
{
    return s_target == TG_USB ? "USB" : s_target == TG_BT ? "Bluetooth" : TR("USB + Bluetooth", "USB + Bluetooth");
}

// ---- links ----------------------------------------------------------------------------------------
static bool usb_live(void) { return s_usb_ok && s_target != TG_BT && nucleo_usbhid_ready(); }
static bool bt_live(void)  { return s_bt_ok && s_target != TG_USB && nucleo_ble_hid_ready(); }

// One keystroke (press + release) to every live link. True if at least one link took it.
static bool send_key(unsigned char mod, unsigned char kc)
{
    bool any = false;
    if (usb_live()) { nucleo_usbhid_key(mod, kc); any = true; }
    if (bt_live())  { nucleo_ble_hid_key(mod, kc); any = true; }
    return any;
}

static int link_state(void)
{
    int u = !s_usb_ok ? 0 : nucleo_usbhid_ready() ? 2 : 1;
    int b = !s_bt_ok ? 0 : nucleo_ble_hid_ready() ? 3 : nucleo_ble_hid_connected() ? 2 : 1;
    return u | b << 2 | (int)s_target << 4 | (int)s_layout << 6 | (s_usb_ok ? nucleo_usbhid_leds() : 0) << 8;
}

static const char *usb_text(void)
{
    if (!s_usb_ok) return TR("USB HID non avviata", "USB HID not started");
    return nucleo_usbhid_ready() ? TR("PC collegato", "Host connected") : TR("collega il cavo USB-C", "plug in the USB-C cable");
}
static unsigned short usb_col(void) { return !s_usb_ok ? WARN : nucleo_usbhid_ready() ? GRN : MUTED; }

static const char *bt_text(void)
{
    if (!s_bt_ok) return TR("Bluetooth non disponibile", "Bluetooth unavailable");
    if (nucleo_ble_hid_ready()) return TR("abbinato, pronto", "paired, ready");
    if (nucleo_ble_hid_connected()) return TR("abbinamento...", "pairing...");
    return TR("cerca 'NucleoOS'", "look for 'NucleoOS'");
}
static unsigned short bt_col(void)
{
    return !s_bt_ok ? WARN : nucleo_ble_hid_ready() ? GRN : nucleo_ble_hid_connected() ? ACC : MUTED;
}

// ---- compact view (the Cardputer's own 240x135) ------------------------------------------------------
static void link_row(int y, const char *name, const char *state, unsigned short col, bool used)
{
    d.fillCircle(12, y + 7, 5, col);
    d.setTextSize(2); d.setTextColor(used ? FG : DIM, BG); d.setCursor(24, y); d.print(name);
    d.setTextSize(1); d.setTextColor(col, BG); d.setCursor(24 + (int)strlen(name) * 12 + 8, y + 5); d.print(state);
}

static void chip(int x, int y, const char *label, bool on, unsigned short oncol)
{
    int w = (int)strlen(label) * 6 + 8;
    d.fillRoundRect(x, y, w, 13, 3, on ? oncol : LINE);
    d.setTextColor(on ? INK : MUTED, on ? oncol : LINE);
    d.setCursor(x + 4, y + 3); d.print(label);
}

static void home_screen(int top)
{
    int y = top + 4;
    link_row(y, "USB", usb_text(), usb_col(), s_target != TG_BT);
    link_row(y + 22, "BT", bt_text(), bt_col(), s_target != TG_USB);
    d.setTextSize(1);
    d.setTextColor(MUTED, BG); d.setCursor(8, y + 48); d.print("Layout:");
    d.setTextColor(FG, BG);    d.setCursor(56, y + 48); d.print(layout_name());
    d.setTextColor(DIM, BG);   d.setCursor(150, y + 48); d.print("L");
    d.setTextColor(MUTED, BG); d.setCursor(8, y + 62); d.print(TR("Invia a:", "Send to:"));
    d.setTextColor(FG, BG);    d.setCursor(56, y + 62); d.print(target_name());
    d.setTextColor(DIM, BG);   d.setCursor(150, y + 48 + 14); d.print("T");
    d.fillRoundRect(6, y + 78, 228, 24, 8, ACC);
    d.setTextSize(2); d.setTextColor(INK, ACC); d.setCursor(14, y + 83); d.print("ENTER");
    d.setTextSize(1); d.setCursor(80, y + 87); d.print(TR("inizia a digitare", "start typing"));
}

static void type_screen(unsigned char m)
{
    d.fillScreen(BG);
    d.setTextSize(2); d.setTextColor(ACC, BG); d.setCursor(8, 4); d.print(TR("Digita", "Type"));
    d.setTextSize(1);
    d.fillCircle(110, 11, 4, usb_live() ? GRN : LINE);  d.setTextColor(usb_live() ? GRN : DIM, BG); d.setCursor(118, 8); d.print("USB");
    d.fillCircle(150, 11, 4, bt_live() ? GRN : LINE);   d.setTextColor(bt_live() ? GRN : DIM, BG);  d.setCursor(158, 8); d.print("BT");
    d.setTextColor(MUTED, BG); d.setCursor(186, 8); d.print(s_layout == DUCKY_LAYOUT_BR ? "BR" : s_layout == DUCKY_LAYOUT_IT ? "IT" : "US");

    int x = 8, y = 30;
    chip(x, y, "CTRL", m & NK_MOD_CTRL, ACC);  x += 38;
    chip(x, y, "ALT",  m & NK_MOD_ALT, ACC);   x += 32;
    chip(x, y, "SHIFT",m & NK_MOD_SHIFT, ACC); x += 44;
    chip(x, y, "GUI",  m & NK_MOD_GUI, ACC);   x += 32;
    chip(x, y, "FN",   m & NK_MOD_FN, ACC);
    unsigned char leds = s_usb_ok ? nucleo_usbhid_leds() : 0;
    chip(8, 48, "CAPS", leds & NK_HIDLED_CAPS, GRN);

    d.setTextColor(MUTED, BG); d.setCursor(8, 70); d.print(TR("Inviato:", "Sent:"));
    d.setTextColor(FG, BG); d.setCursor(58, 70); d.print(s_echo[0] ? s_echo : "-");
    if (!usb_live() && !bt_live()) { d.setTextColor(WARN, BG); d.setCursor(8, 84); d.print(TR("Nessun PC collegato.", "No host connected.")); }

    d.setTextColor(DIM, BG);
    d.setCursor(8, 100); d.print("Fn+1..0=F1..F10  Fn+-=F11  Fn+==F12");
    d.setCursor(8, 112); d.print(TR("Fn+frecce  Fn+Canc=Del  Fn+`=Esc", "Fn+arrows  Fn+Del=fwd Del  Fn+`=Esc"));
    d.setTextColor(WARN, BG); d.setCursor(8, 124); d.print(TR("`  esci", "`  exit"));
}

static void echo_push(const char *what)
{
    size_t l = strlen(s_echo);
    if (l + strlen(what) + 1 >= sizeof s_echo) { s_echo[0] = 0; l = 0; }
    snprintf(s_echo + l, sizeof s_echo - l, "%s", what);
}

// ---- two screens ----------------------------------------------------------------------------------------
// The 320x240 panel shows the two links as big cards plus layout/target as touch buttons (the content you
// change), and the live typing view while typing. The Cardputer, where you type, keeps its compact view.
static void lg_link(int x, int y, int w, const char *name, const char *state, unsigned short col, bool used)
{
    const unsigned short cb = lg::card();
    lg::panel(x, y, w, 46);
    d.fillCircle(x + 20, y + 23, 8, col);
    lg::font(2); d.setTextColor(used ? FG : DIM, cb); d.drawString(name, x + 38, y + 8);
    lg::font(0); d.setTextColor(col, cb); d.drawString(state, x + 38, y + 30);
}

static bool s_lg_full = true;
static void kbd_draw_large(void)
{
    const int W = 320, H = 240;
    nucleo_touch_clear();
    int y = lg::header(TR("Tastiera", "Keyboard"), s_typing ? TR("digitando", "typing") : nullptr, ACC, NK_BACK, 0);
    if (s_lg_full || !s_typing) d.fillRect(0, y, W, H - y, BG);
    s_lg_full = !s_typing;
    lg_link(10, y + 6, W - 20, "USB-C", usb_text(), usb_col(), s_target != TG_BT);
    lg_link(10, y + 58, W - 20, "Bluetooth", bt_text(), bt_col(), s_target != TG_USB);
    if (s_typing) {
        d.fillRect(0, y + 110, W, 30, BG);
        static const char *const MN[] = { "CTRL", "ALT", "SHIFT", "GUI", "FN" };
        static const unsigned char MB[] = { NK_MOD_CTRL, NK_MOD_ALT, NK_MOD_SHIFT, NK_MOD_GUI, NK_MOD_FN };
        for (int i = 0, x = 10; i < 5; i++) {
            const bool on = s_tmods & MB[i]; const int w = (int)strlen(MN[i]) * 6 + 14;
            d.fillRoundRect(x, y + 114, w, 20, 5, on ? ACC : LINE);
            lg::font(0); d.setTextDatum(middle_center); d.setTextColor(on ? INK : MUTED, on ? ACC : LINE);
            d.drawString(MN[i], x + w / 2, y + 125); d.setTextDatum(top_left);
            x += w + 8;
        }
        lg::panel(10, y + 142, W - 20, 44, LINE);
        lg::font(0); d.setTextColor(MUTED, LINE); d.drawString(TR("Inviato:", "Sent:"), 20, y + 158);
        char e[40]; lg::font(2); lg::fit(e, sizeof e, s_echo[0] ? s_echo : "-", W - 110); d.setTextColor(FG, LINE); d.drawString(e, 86, y + 154);
    } else {
        char b[40];
        snprintf(b, sizeof b, "Layout %s", layout_name());
        lg::button(10, y + 112, 146, 40, b, lg::card(), FG, NK_CHAR, 'l');
        lg::button(164, y + 112, 146, 40, target_name(), lg::card(), FG, NK_CHAR, 't');
        lg::button(10, H - 50, W - 20, 44, TR("Inizia a digitare", "Start typing"), ACC, INK, NK_ENTER, 0);
    }
    nucleo_touch_commit();
}

static void draw_companion(void)
{
    const int top = nucleo_app_content_top(), h = nucleo_app_content_height();
    d.fillRect(0, top, 240, h, BG);
    const int y = lg::comp_head(TR("TASTIERA", "KEYBOARD"), ACC);
    d.setFont(&fonts::Font0);
    d.setTextColor(usb_col(), BG); d.drawString(usb_text(), 8, y + 6);
    d.setTextColor(bt_col(), BG);  d.drawString(bt_text(), 8, y + 20);
    d.setTextColor(MUTED, BG);     d.drawString(layout_name(), 8, y + 36);
    lg::comp_keys(TR("INVIO digita  L layout  T invia a", "ENTER type  L layout  T send to"));
}

// ---- the typing loop: owns the keyboard until ` ------------------------------------------------------------
static M5Canvas *s_tcv;
static void tcv_push(void *g) { if (s_tcv) s_tcv->pushSprite((lgfx::LovyanGFX *)g, 0, 0); }
extern "C" bool nucleo_extdisp_draw_internal(void (*fn)(void *gfx));

static void type_show(unsigned char m)
{
    s_tmods = m;
    M5Canvas *cv = nucleo_screen();
    if (!nucleo_app_two_screens()) {
        if (!cv) { type_screen(m); return; }          // OOM fallback: direct (rare in a Solo boot)
        nucleo_app_set_gfx(cv); type_screen(m); nucleo_app_set_gfx(nullptr);
        cv->pushSprite(0, 0);
        return;
    }
    if ((s_tcv = cv) != nullptr) {
        nucleo_app_set_gfx(cv); type_screen(m); nucleo_app_set_gfx(nullptr);
        nucleo_extdisp_draw_internal(tcv_push);
    }
    d.startWrite(); kbd_draw_large(); d.endWrite();
}

static void type_loop(void)
{
    s_echo[0] = 0; s_typing = true; s_lg_full = true;
    unsigned char last_m = 0xFF; int last_state = -1; bool first = true;
    for (;;) {
        esp_task_wdt_reset();
        nucleo_key_t k = nucleo_kbd_read();
        unsigned char m = nucleo_kbd_mods();
        bool sent = false; const char *lbl = nullptr; char lblbuf[2];

        if (k.key != NK_NONE) {
            const bool fn = (m & NK_MOD_FN);
            if (k.key == NK_BACK && !fn) break;                                   // ` leaves
            unsigned char hidmod = 0;
            if (m & NK_MOD_CTRL) hidmod |= NK_HIDMOD_CTRL;
            if (m & NK_MOD_ALT)  hidmod |= NK_HIDMOD_ALT;
            if (m & NK_MOD_GUI)  hidmod |= NK_HIDMOD_GUI;
            const unsigned char shift = (m & NK_MOD_SHIFT) ? NK_HIDMOD_SHIFT : 0;
            unsigned char kc = 0, kmod = hidmod;

            if (k.key == NK_BACK)       { kc = HID_ESC; lbl = "Esc"; }             // Fn+` -> Esc
            else if (k.key == NK_UP)    { kc = NK_HID_UP; kmod |= shift; lbl = "^"; }
            else if (k.key == NK_DOWN)  { kc = NK_HID_DOWN; kmod |= shift; lbl = "v"; }
            else if (k.key == NK_LEFT)  { kc = NK_HID_LEFT; kmod |= shift; lbl = "<"; }
            else if (k.key == NK_RIGHT) { kc = NK_HID_RIGHT; kmod |= shift; lbl = ">"; }
            else if (k.key == NK_ENTER) { kc = NK_HID_ENTER; kmod |= shift; lbl = " Ent "; }
            else if (k.key == NK_TAB)   { kc = 0x2B; kmod |= shift; lbl = "Tab"; }
            else if (k.key == NK_DEL)   { kc = fn ? HID_DELfw : NK_HID_BKSP; lbl = fn ? "Del" : "<-"; }
            else if (fn && k.ch >= '1' && k.ch <= '9') { kc = HID_F(k.ch - '0'); lbl = "F"; }
            else if (fn && k.ch == '0') { kc = HID_F(10); lbl = "F10"; }
            else if (fn && k.ch == '-') { kc = HID_F(11); lbl = "F11"; }
            else if (fn && k.ch == '=') { kc = HID_F(12); lbl = "F12"; }
            else if (k.ch == ' ')       { kc = NK_HID_SPACE; lbl = " "; }
            else if (k.ch >= 33 && k.ch < 127) {
                uint8_t dm, dk;                                    // the key that prints this char on THIS host layout
                if (nucleo_ducky_char(k.ch, s_layout, &dm, &dk)) {
                    kc = dk; kmod = hidmod | dm; lblbuf[0] = k.ch; lblbuf[1] = 0; lbl = lblbuf;
                }
            }
            if (kc) sent = send_key(kmod, kc);
            if (sent && lbl) echo_push(lbl);
        }

        const int st = link_state();
        if (first || sent || m != last_m || st != last_state) {
            type_show(m);
            last_m = m; last_state = st; first = false;
        }
        vTaskDelay(pdMS_TO_TICKS(15));
    }
    s_typing = false; s_lg_full = true;
    nucleo_app_request_draw();
}

// ---- framework hooks --------------------------------------------------------------------------------------
static void cycle_layout(void)
{
    s_layout = s_layout == DUCKY_LAYOUT_BR ? DUCKY_LAYOUT_US : s_layout == DUCKY_LAYOUT_US ? DUCKY_LAYOUT_IT : DUCKY_LAYOUT_BR;
    cfg_save(); nucleo_app_request_draw();
}
static void cycle_target(void) { s_target = (Target)((s_target + 1) % TG_N); cfg_save(); nucleo_app_request_draw(); }

static void on_enter(void)
{
    s_layout = DUCKY_LAYOUT_BR; s_target = TG_BOTH;
    cfg_load();
    s_last_state = -1; s_lg_full = true;
    s_usb_ok = (nucleo_usbhid_start() == ESP_OK);
    s_bt_ok  = nucleo_ble_radio_present() && nucleo_ble_up();
    if (s_bt_ok) nucleo_ble_hid_start();                  // advertise as keyboard "NucleoOS" (starts on host sync)
    nucleo_app_set_hint(TR("invio digita  L layout  T invia a", "enter type  L layout  T send to"));
    nucleo_app_request_draw();
}

static void on_key(int key, char ch)
{
    if (key == NK_ENTER) { type_loop(); return; }
    if (ch == 'l' || ch == 'L') cycle_layout();
    else if (ch == 't' || ch == 'T') cycle_target();
}

static void on_tick(void)
{
    const int st = link_state();
    if (st == s_last_state) return;
    s_last_state = st;
    nucleo_app_request_draw();
}

static void draw(void)
{
    if (nucleo_app_two_screens()) { draw_companion(); return; }
    const int top = nucleo_app_content_top(), h = nucleo_app_content_height();
    d.fillRect(0, top, 240, h, BG);
    home_screen(top);
}

static void on_exit(void)
{
    if (!s_bt_ok) return;
    nucleo_ble_hid_stop();
    nucleo_ble_down();   // the framework then reboots back to the normal OS (also frees the USB PHY)
}

extern "C" void nucleo_register_keyboard(void)
{
    static const nucleo_app_def_t app = {
        "keyboard", "Keyboard", "Connect", "USB-C + Bluetooth keyboard: BR/US/IT layout, Fn, accents",
        'K', 0x2D7F, on_enter, on_key, on_tick, draw, on_exit,
        NX_SOLO | NX_DEEP_OFFLINE | NX_BLE, nullptr, kbd_draw_large
    };
    nucleo_app_register(&app);
}
