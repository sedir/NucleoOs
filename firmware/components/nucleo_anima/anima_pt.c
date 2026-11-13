// ANIMA Brazilian Portuguese (pt-BR) — a "catalog language" for the OFFLINE cascade. See docs/anima-pt.md.
//
// The cascade understands Italian + English through keyword tables spread over ~40 functions and answers
// through ~280 `en ? "..." : "..."` call sites. Portuguese adds no third branch to any of them:
//   1. INPUT BRIDGE  — a_pt_bridge(): fold the accents, then rewrite known Portuguese words/phrases into the
//      English the cascade already reads ("abre a calculadora" -> "open the calculator"). Applied only when
//      the line carries a device-command cue, so a knowledge question keeps its Portuguese for the online
//      tier (whose LLM already answers in pt) instead of a half-translated pidgin.
//   2. REPLY CATALOG — a_pt_reply(): the cascade runs with en=true; its English reply is matched against
//      EN templates ("Opening %s.") and rewritten to Portuguese ("Abrindo %s."). A reply the catalog does
//      not know stays English (the floor, same rule as nucleo_tr() in the UI).
// Both tables are `static const` (flash/.rodata): zero heap, zero .bss. Frames are small (no big locals).
// Replies are ASCII on purpose: the TFT font has no accented glyphs (same rule as tools/native-i18n/pt.json).
#include "anima_internal.h"
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

// ---------------------------------------------------------------------------------------------------------
// 1. Input bridge
// ---------------------------------------------------------------------------------------------------------
// pt: one or more folded words (single spaces). en: the replacement ("" deletes the words).
// cue: the entry names a device command/state (verb, setting, reading, app) -> the line is a command.
// Longest match wins, so phrases ("que horas sao") must simply be listed; order does not matter.
typedef struct { const char *pt; const char *en; unsigned char cue; } a_pt_word_t;

static const a_pt_word_t PT_WORDS[] = {
    // --- time / date / readings ---------------------------------------------------------------------
    { "que horas sao",        "what time is it", 1 },
    { "que hora e",           "what time is it", 1 },
    { "que horas e",          "what time is it", 1 },
    { "me diz as horas",      "what time is it", 1 },
    { "me diga as horas",     "what time is it", 1 },
    { "horas sao",            "time is it", 1 },
    { "horas",                "time", 1 },
    { "hora",                 "time", 1 },
    { "que dia e hoje",       "what is the date today", 1 },
    { "qual a data de hoje",  "what is the date today", 1 },
    { "qual e a data de hoje","what is the date today", 1 },
    { "data de hoje",         "date today", 1 },
    { "em que ano estamos",   "what year is it", 1 },
    { "que ano e",            "what year is it", 1 },
    { "em que estacao estamos","what season is it", 1 },
    { "quanta bateria",       "battery level", 1 },
    { "quanto de bateria",    "battery level", 1 },
    { "nivel da bateria",     "battery level", 1 },
    { "quanto de bateria resta","battery level", 1 },
    { "nivel de bateria",     "battery level", 1 },
    { "carga da bateria",     "battery level", 1 },
    { "bateria",              "battery", 1 },
    { "espaco livre",         "free space", 1 },
    { "espaco",               "space", 1 },
    { "cartao sd",            "sd card", 1 },
    { "memoria ram",          "ram", 1 },
    { "versao",               "version", 1 },
    { "quem e voce",          "who are you", 1 },
    { "quem es tu",           "who are you", 1 },
    { "o que voce sabe fazer","help", 1 },
    { "o que voce faz",       "help", 1 },
    { "o que voce pode fazer","help", 1 },
    { "comandos",             "help", 1 },
    { "ajuda",                "help", 1 },
    // --- settings ------------------------------------------------------------------------------------
    { "volume",               "volume", 1 },
    { "som",                  "volume", 1 },
    { "brilho",               "brightness", 1 },
    { "luminosidade",         "brightness", 1 },
    { "tela",                 "brightness", 1 },
    { "aumenta",              "raise", 1 },
    { "aumente",              "raise", 1 },
    { "aumentar",             "raise", 1 },
    { "sobe",                 "raise", 1 },
    { "suba",                 "raise", 1 },
    { "subir",                "raise", 1 },
    { "abaixa",               "lower", 1 },
    { "abaixe",               "lower", 1 },
    { "abaixar",              "lower", 1 },
    { "baixa",                "lower", 1 },
    { "baixe",                "lower", 1 },
    { "diminui",              "lower", 1 },
    { "diminua",              "lower", 1 },
    { "diminuir",             "lower", 1 },
    { "reduz",                "lower", 1 },
    { "reduza",               "lower", 1 },
    { "coloca",               "set", 1 },
    { "coloque",              "set", 1 },
    { "poe",                  "set", 1 },
    { "ponha",                "set", 1 },
    { "define",               "set", 1 },
    { "defina",               "set", 1 },
    { "ajusta",               "set", 1 },
    { "ajuste",               "set", 1 },
    { "muda",                 "set", 1 },
    { "mude",                 "set", 1 },
    { "deixa",                "set", 1 },
    { "deixe",                "set", 1 },
    { "mais alto",            "higher", 0 },
    { "mais baixo",           "down", 0 },
    { "mais claro",           "brighter", 0 },
    { "mais escuro",          "darker", 0 },
    { "mais clara",           "brighter", 0 },
    { "mais escura",          "darker", 0 },
    { "escuro",               "darker", 0 },
    { "escura",               "darker", 0 },
    { "clara",                "brighter", 0 },
    { "no maximo",            "max", 0 },
    { "maximo",               "max", 0 },
    { "no minimo",            "min", 0 },
    { "minimo",               "min", 0 },
    { "pela metade",          "half", 0 },
    { "metade",               "half", 0 },
    { "por cento",            "percent", 0 },
    { "mudo",                 "mute", 1 },
    { "silencio",             "mute", 1 },
    { "silencia",             "mute", 1 },
    // --- open / close --------------------------------------------------------------------------------
    { "abre",                 "open", 1 },
    { "abra",                 "open", 1 },
    { "abrir",                "open", 1 },
    { "abri",                 "open", 1 },
    { "inicia",               "start", 1 },
    { "inicie",               "start", 1 },
    { "iniciar",              "start", 1 },
    { "executa",              "run", 1 },
    { "execute",              "run", 1 },
    { "roda",                 "run", 1 },
    { "rode",                 "run", 1 },
    { "mostra",               "show", 1 },
    { "mostre",               "show", 1 },
    { "me mostra",            "show", 1 },
    { "me mostre",            "show", 1 },
    { "fecha",                "close", 1 },
    { "feche",                "close", 1 },
    { "fechar",               "close", 1 },
    { "sai do",               "exit", 1 },
    { "sai da",               "exit", 1 },
    { "sair do",              "exit", 1 },
    { "sair da",              "exit", 1 },
    // --- app names (EN aliases of registry/app-aliases.json) ----------------------------------------
    { "calculadora",          "calculator", 1 },
    { "relogio",              "clock", 1 },
    { "despertador",          "alarm", 1 },
    { "cronometro",           "stopwatch", 1 },
    { "calendario",           "calendar", 1 },
    { "agenda",               "calendar", 1 },
    { "configuracoes",        "settings", 1 },
    { "configuracao",         "settings", 1 },
    { "ajustes",              "settings", 1 },
    { "musica",               "music", 1 },
    { "musicas",              "music", 1 },
    { "fotos",                "photos", 1 },
    { "foto",                 "photos", 1 },
    { "imagens",              "images", 1 },
    { "galeria",              "gallery", 1 },
    { "bloco de notas",       "notepad", 1 },
    { "notas",                "notes", 1 },
    { "arquivos",             "files", 1 },
    { "gerenciador de arquivos","files", 1 },
    { "gravador",             "recorder", 1 },
    { "gravador de voz",      "recorder", 1 },
    { "navegador",            "browser", 1 },
    { "jogos",                "games", 1 },
    { "jogo",                 "games", 1 },
    { "previsao do tempo",    "weather", 1 },
    { "clima",                "weather", 1 },
    { "lixeira",              "trash", 1 },
    { "contatos",             "contacts", 1 },
    { "planilha",             "spreadsheet", 1 },
    { "conversor",            "converter", 1 },
    { "conversor de unidades","unit converter", 1 },
    { "tarefas",              "tasks", 1 },
    { "atualizacoes",         "updates", 1 },
    { "controle remoto",      "remote", 1 },
    { "metronomo",            "metronome", 1 },
    { "autenticador",         "authenticator", 1 },
    { "desenho",              "draw", 1 },
    { "desenhar",             "draw", 1 },
    { "filmes",               "movies", 1 },
    { "videos",               "videos", 1 },
    { "monitor do sistema",   "system monitor", 1 },
    // --- glue (never a cue on its own) ----------------------------------------------------------------
    { "por favor",            "", 0 },
    { "pra mim",              "", 0 },
    { "para mim",             "", 0 },
    { "o",                    "the", 0 },
    { "a",                    "the", 0 },
    { "os",                   "the", 0 },
    { "as",                   "the", 0 },
    { "do",                   "of the", 0 },
    { "da",                   "of the", 0 },
    { "de",                   "of", 0 },
    { "para",                 "to", 0 },
    { "pra",                  "to", 0 },
    { "em",                   "to", 0 },
    { "no",                   "to", 0 },
    { "na",                   "to", 0 },
    { "e",                    "is", 0 },
    { "esta",                 "is", 0 },
    { "tem",                  "", 0 },
    { "resta",                "", 0 },
    { "sobrou",               "", 0 },
    { "qual",                 "what", 0 },
    { "quanto",               "how much", 0 },
    { "quanta",               "how much", 0 },
    { "quantos",              "how many", 0 },
    { "agora",                "now", 0 },
    { "hoje",                 "today", 0 },
    { "um pouco",             "", 0 },
    { "mais",                 "more", 0 },
    { "menos",                "less", 0 },
};

// Fold one UTF-8 Latin-1 code point (0xC3 xx) to ASCII; 0 if it is not a letter we fold.
static char pt_fold_c3(unsigned char d)
{
    switch (d) {
        case 0xA0: case 0xA1: case 0xA2: case 0xA3: case 0xA4: return 'a';   // à á â ã ä
        case 0x80: case 0x81: case 0x82: case 0x83: case 0x84: return 'a';   // À Á Â Ã Ä
        case 0xA7: case 0x87:                                  return 'c';   // ç Ç
        case 0xA8: case 0xA9: case 0xAA: case 0xAB:            return 'e';   // è é ê ë
        case 0x88: case 0x89: case 0x8A: case 0x8B:            return 'e';
        case 0xAC: case 0xAD: case 0xAE: case 0xAF:            return 'i';   // ì í î ï
        case 0x8C: case 0x8D: case 0x8E: case 0x8F:            return 'i';
        case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6: return 'o';   // ò ó ô õ ö
        case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: return 'o';
        case 0xB9: case 0xBA: case 0xBB: case 0xBC:            return 'u';   // ù ú û ü
        case 0x99: case 0x9A: case 0x9B: case 0x9C:            return 'u';
        default: return 0;
    }
}

// Fold to lowercase ASCII words separated by single spaces. Keeps digits and the characters a tool may read
// from the raw line (% . , : / - _ +); ?!;"() and other punctuation become separators.
static void pt_fold(const char *in, char *out, size_t cap)
{
    size_t n = 0; bool sp = true;
    for (const unsigned char *p = (const unsigned char *)in; *p && n + 1 < cap; p++) {
        char c = 0;
        if (*p == 0xC3 && p[1]) c = pt_fold_c3(*++p);
        else if (*p < 0x80 && (isalnum(*p) || strchr("%.,:/-_+'", *p))) c = (char)tolower(*p);
        if (c == '\'') c = 0;                      // "d'agua" -> "d agua"
        if (c) { out[n++] = c; sp = false; }
        else if (!sp) { out[n++] = ' '; sp = true; }
    }
    while (n && out[n - 1] == ' ') n--;
    // a sentence's final '.' / ',' is punctuation, not part of the last word
    while (n && (out[n - 1] == '.' || out[n - 1] == ',')) n--;
    out[n] = 0;
}

// Does the entry's word sequence `pt` start at `s` (a position in the folded line, on a word start)?
// Returns the length matched (up to a word boundary) or 0.
static size_t pt_match_at(const char *s, const char *pt)
{
    size_t l = strlen(pt);
    if (strncmp(s, pt, l) != 0) return 0;
    char e = s[l];
    if (e == ' ' || e == 0) return l;
    // trailing punctuation glued to the word ("calculadora," / "horas?") still matches
    if ((e == ',' || e == '.') && (s[l + 1] == ' ' || s[l + 1] == 0)) return l + 1;
    return 0;
}

bool a_pt_bridge(const char *in, char *out, size_t cap)
{
    char f[256];
    if (!in || !out || cap < 2) return false;
    pt_fold(in, f, sizeof f);
    size_t n = 0; bool cue = false;
    const char *s = f;
    while (*s) {
        while (*s == ' ') s++;
        if (!*s) break;
        const a_pt_word_t *best = NULL; size_t bl = 0;
        for (size_t i = 0; i < sizeof PT_WORDS / sizeof PT_WORDS[0]; i++) {
            size_t l = pt_match_at(s, PT_WORDS[i].pt);
            if (l > bl) { bl = l; best = &PT_WORDS[i]; }
        }
        const char *w; size_t wl;
        if (best) { w = best->en; wl = strlen(w); cue |= best->cue; s += bl; }
        else      { w = s; wl = strcspn(s, " "); s += wl; }
        if (wl == 0) continue;                                   // deleted glue ("por favor")
        if (n && n + 1 < cap) out[n++] = ' ';
        if (n + wl >= cap) wl = cap - 1 - n;
        memcpy(out + n, w, wl); n += wl;
    }
    out[n] = 0;
    return cue;
}

// ---------------------------------------------------------------------------------------------------------
// 2. Reply catalog
// ---------------------------------------------------------------------------------------------------------
// en/pt templates: "%s" in en is a wildcard capture (up to 4, non-empty); "%1".."%4" in pt is that capture,
// localized through PT_TERMS when it is an app id / weekday / month. Every other byte is literal.
// intent: NULL = any; else the entry applies only to that intent (the same EN text can need two translations).
typedef struct { const char *intent; const char *en; const char *pt; } a_pt_reply_t;

static const a_pt_reply_t PT_REPLIES[] = {
    { NULL,       "Opening %s.",                     "Abrindo %1." },
    { NULL,       "Closing %s.",                     "Fechando %1." },
    { NULL,       "Which one — %s or %s?",           "Qual delas: %1 ou %2?" },
    { NULL,       "Raising the volume.",             "Aumentando o volume." },
    { NULL,       "Lowering the volume.",            "Abaixando o volume." },
    { NULL,       "Setting the volume to %s.",       "Volume em %1." },
    { NULL,       "Raising the brightness.",         "Aumentando o brilho." },
    { NULL,       "Lowering the brightness.",        "Diminuindo o brilho." },
    { NULL,       "Setting the brightness to %s.",   "Brilho em %1." },
    { NULL,       "Battery: {value}.",               "Bateria: {value}." },
    { NULL,       "SD space: {value}.",              "Espaco no SD: {value}." },
    { "year",     "It's {value}.",                   "Estamos em {value}." },
    { "season",   "It's {value}.",                   "Estacao: {value}." },
    { NULL,       "Running {value}.",                "Versao: {value}." },
    { NULL,       "Up for {value}.",                 "Ligado ha {value}." },
    { "date",     "Today is %s, %s %s, %s.",         "Hoje e %1, %3 de %2 de %4." },
    { "date",     "Tomorrow is %s, %s %s, %s.",      "Amanha e %1, %3 de %2 de %4." },
    { "date",     "The day after tomorrow is %s, %s %s, %s.", "Depois de amanha e %1, %3 de %2 de %4." },
    { "date",     "Yesterday was %s, %s %s, %s.",    "Ontem foi %1, %3 de %2 de %4." },
    { NULL,       "I'm ANIMA, NucleoOS's offline assistant. I work with no internet, on the device.",
                  "Sou a ANIMA, a assistente offline do NucleoOS. Funciono sem internet, no proprio aparelho." },
    { NULL,       "I don't know.",                   "Nao sei." },
};

// Capture -> Portuguese: app ids (article included: "Abrindo a Calculadora"), weekdays, months.
// A capture not listed is copied as is.
typedef struct { const char *id; const char *pt; } a_pt_app_t;
static const a_pt_app_t PT_TERMS[] = {
    { "Sunday", "domingo" }, { "Monday", "segunda-feira" }, { "Tuesday", "terca-feira" },
    { "Wednesday", "quarta-feira" }, { "Thursday", "quinta-feira" }, { "Friday", "sexta-feira" },
    { "Saturday", "sabado" },
    { "January", "janeiro" }, { "February", "fevereiro" }, { "March", "marco" }, { "April", "abril" },
    { "May", "maio" }, { "June", "junho" }, { "July", "julho" }, { "August", "agosto" },
    { "September", "setembro" }, { "October", "outubro" }, { "November", "novembro" }, { "December", "dezembro" },
    { "calculator",      "a Calculadora" },     { "clock",          "o Relogio" },
    { "chrono",          "o Cronometro" },      { "calendar",       "o Calendario" },
    { "settings",        "as Configuracoes" },  { "media-player",   "o Player de musica" },
    { "photo-viewer",    "as Fotos" },          { "notepad",        "o Bloco de notas" },
    { "file-commander",  "os Arquivos" },       { "recorder",       "o Gravador" },
    { "browser",         "o Navegador" },       { "games",          "os Jogos" },
    { "weather",         "a Previsao do tempo" },{ "recycle-bin",   "a Lixeira" },
    { "contacts",        "os Contatos" },       { "spreadsheet",    "a Planilha" },
    { "unit-converter",  "o Conversor" },       { "tasks",          "as Tarefas" },
    { "updates",         "as Atualizacoes" },   { "terminal",       "o Terminal" },
    { "radio",           "o Radio" },           { "video-player",   "o Player de video" },
    { "paint",           "o Paint" },           { "ir-remote",      "o Controle remoto" },
    { "metronome",       "o Metronomo" },       { "authenticator",  "o Autenticador" },
    { "system-monitor",  "o Monitor do sistema" },{ "pomodoro",     "o Pomodoro" },
};

static const char *pt_term(const char *id, size_t len)
{
    for (size_t i = 0; i < sizeof PT_TERMS / sizeof PT_TERMS[0]; i++)
        if (strlen(PT_TERMS[i].id) == len && !strncmp(PT_TERMS[i].id, id, len)) return PT_TERMS[i].pt;
    return NULL;
}

// Match `s` against template `t` ("%s" = non-empty wildcard, backtracking). Captures go to cap[]/cl[].
static bool pt_tmatch(const char *t, const char *s, const char **cap, size_t *cl, int k, int kmax)
{
    if (!*t) return !*s;
    if (t[0] == '%' && t[1] == 's') {
        if (k >= kmax) return false;
        for (size_t l = strlen(s); l >= 1; l--) {          // greedy, then back off
            cap[k] = s; cl[k] = l;
            if (pt_tmatch(t + 2, s + l, cap, cl, k + 1, kmax)) return true;
        }
        return false;
    }
    return *t == *s && pt_tmatch(t + 1, s + 1, cap, cl, k, kmax);
}

bool a_pt_reply(const char *intent, char *reply, size_t cap)
{
    if (!reply || !reply[0]) return false;
    for (size_t i = 0; i < sizeof PT_REPLIES / sizeof PT_REPLIES[0]; i++) {
        const a_pt_reply_t *e = &PT_REPLIES[i];
        if (e->intent && (!intent || strcmp(e->intent, intent))) continue;
        const char *c[4]; size_t cl[4];
        if (!pt_tmatch(e->en, reply, c, cl, 0, 4)) continue;
        char out[320]; size_t n = 0;
        for (const char *p = e->pt; *p && n + 1 < sizeof out; p++) {
            if (p[0] == '%' && p[1] >= '1' && p[1] <= '4') {
                int k = p[1] - '1'; p++;
                const char *v = c[k]; size_t vl = cl[k];
                const char *t = pt_term(v, vl);
                if (t) { v = t; vl = strlen(t); }
                if (n + vl >= sizeof out) vl = sizeof out - 1 - n;
                memcpy(out + n, v, vl); n += vl;
            } else out[n++] = *p;
        }
        out[n] = 0;
        snprintf(reply, cap, "%s", out);
        return true;
    }
    return false;
}
