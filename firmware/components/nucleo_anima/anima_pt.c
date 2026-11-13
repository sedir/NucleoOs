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
// flags: PT_CUE   the entry names a device command/state (verb, setting, reading, app, tool) -> bridge the line;
//        PT_TEXT  after this entry the line is the USER'S TEXT (a reminder, a note, a file name): words are copied
//                 as typed, accents included, so "me lembra de ligar para a mae" keeps "ligar para a mae";
//        PT_KEEP  translated even inside the user's text (time words the tool must still parse: "amanha").
// Longest match wins, so phrases ("que horas sao") must simply be listed; order does not matter.
#define PT_CUE  1
#define PT_TEXT 2
#define PT_KEEP 4
typedef struct { const char *pt; const char *en; unsigned char flags; } a_pt_word_t;

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
    // --- reminders, notes, files: the rest of the line is the user's text (PT_TEXT) --------------------
    { "me lembra de",         "remind me to", PT_CUE | PT_TEXT },
    { "me lembre de",         "remind me to", PT_CUE | PT_TEXT },
    { "lembre-me de",         "remind me to", PT_CUE | PT_TEXT },
    { "me lembra",            "remind me to", PT_CUE | PT_TEXT },
    { "me lembre",            "remind me to", PT_CUE | PT_TEXT },
    { "lembre-me",            "remind me to", PT_CUE | PT_TEXT },
    { "lembra de",            "remind me to", PT_CUE | PT_TEXT },
    { "cria um lembrete para","remind me to", PT_CUE | PT_TEXT },
    { "crie um lembrete para","remind me to", PT_CUE | PT_TEXT },
    { "cria um lembrete de",  "remind me to", PT_CUE | PT_TEXT },
    { "lembrete para",        "remind me to", PT_CUE | PT_TEXT },
    { "lembrete de",          "remind me to", PT_CUE | PT_TEXT },
    { "anota ai",             "write a note:", PT_CUE | PT_TEXT },
    { "anota",                "write a note:", PT_CUE | PT_TEXT },
    { "anote",                "write a note:", PT_CUE | PT_TEXT },
    { "escreve uma nota",     "write a note:", PT_CUE | PT_TEXT },
    { "escreva uma nota",     "write a note:", PT_CUE | PT_TEXT },
    { "cria uma nota chamada","create a note called", PT_CUE | PT_TEXT },
    { "crie uma nota chamada","create a note called", PT_CUE | PT_TEXT },
    { "cria um arquivo chamado","create a file called", PT_CUE | PT_TEXT },
    { "crie um arquivo chamado","create a file called", PT_CUE | PT_TEXT },
    { "cria o arquivo",       "create the file", PT_CUE | PT_TEXT },
    { "crie o arquivo",       "create the file", PT_CUE | PT_TEXT },
    { "cria um arquivo",      "create a file", PT_CUE | PT_TEXT },
    { "crie um arquivo",      "create a file", PT_CUE | PT_TEXT },
    { "amanha",               "tomorrow", PT_KEEP },
    { "depois de amanha",     "the day after tomorrow", PT_KEEP },
    { "hoje",                 "today", PT_KEEP },
    { "meia hora",            "30 minutes", PT_KEEP },
    { "uma hora",             "1 hour", PT_KEEP },
    { "ao meio-dia",          "at 12:00", PT_KEEP },
    { "ao meio dia",          "at 12:00", PT_KEEP },
    { "a meia-noite",         "at 0:00", PT_KEEP },
    // --- timers / alarms -----------------------------------------------------------------------------
    { "timer",                "timer", PT_CUE },
    { "temporizador",         "timer", PT_CUE },
    { "contagem regressiva",  "countdown", PT_CUE },
    { "alarme",               "alarm", PT_CUE },
    { "me acorda",            "wake me", PT_CUE },
    { "me acorde",            "wake me", PT_CUE },
    { "um timer",             "timer", PT_CUE },
    { "um alarme",            "alarm", PT_CUE },
    { "liga",                 "start", PT_CUE },
    { "ligue",                "start", PT_CUE },
    { "de meia hora",         "for 30 minutes", PT_KEEP },
    { "por meia hora",        "for 30 minutes", PT_KEEP },
    { "em meia hora",         "in 30 minutes", PT_KEEP },
        { "cria",                 "create", PT_CUE },
    { "crie",                 "create", PT_CUE },
    // --- arithmetic and conversions --------------------------------------------------------------------
    { "quanto e",             "how much is", PT_CUE },
    { "quanto da",            "how much is", PT_CUE },
    { "quanto sao",           "how much is", PT_CUE },
    { "calcula",              "calculate", PT_CUE },
    { "calcule",              "calculate", PT_CUE },
    { "vezes",                "times", PT_CUE },
    { "multiplicado por",     "times", PT_CUE },
    { "dividido por",         "divided by", PT_CUE },
    { "elevado a",            "to the power of", PT_CUE },
    { "raiz quadrada de",     "square root of", PT_CUE },
    { "por cento de",         "percent of", PT_CUE },
    { "converte",             "convert", PT_CUE },
    { "converta",             "convert", PT_CUE },
    { "converter",            "convert", PT_CUE },
    { "quantos quilometros sao","how many km is", PT_CUE },
    { "quilometros",          "km", 0 },
    { "quilometro",           "km", 0 },
    { "metros",               "meters", 0 },
    { "metro",                "meter", 0 },
    { "centimetros",          "centimeters", 0 },
    { "milhas",               "miles", 0 },
    { "milha",                "mile", 0 },
    { "pes",                  "feet", 0 },
    { "polegadas",            "inches", 0 },
    { "quilos",               "kg", 0 },
    { "quilo",                "kg", 0 },
    { "gramas",               "grams", 0 },
    { "libras",               "pounds", 0 },
    { "litros",               "liters", 0 },
    { "litro",                "liter", 0 },
    { "galoes",               "gallons", 0 },
    { "graus celsius",        "celsius", 0 },
    { "graus fahrenheit",     "fahrenheit", 0 },
    { "celsius",              "celsius", 0 },
    { "fahrenheit",           "fahrenheit", 0 },
    // --- calendar questions --------------------------------------------------------------------------
    { "quantos dias faltam para", "how many days until", PT_CUE },
    { "quantos dias faltam pro",  "how many days until", PT_CUE },
    { "quantos dias ate",     "how many days until", PT_CUE },
    { "em que dia cai",       "what day is", PT_CUE },
    { "que dia cai",          "what day is", PT_CUE },
    { "natal",                "christmas", 0 },
    { "ano novo",             "new year", 0 },
    { "amanha e que dia",     "what day is tomorrow", PT_CUE },
    { "que dia e amanha",     "what day is tomorrow", PT_CUE },
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

// One word of the line: where it is in the input (copied as typed inside the user's text) and its folded form
// (lowercase ASCII, used for matching and emitted everywhere else). Kept small: this frame is on the query path.
#define PT_MAXW  32
#define PT_WLEN  20
typedef struct { const char *raw; unsigned char rawlen; char f[PT_WLEN]; } pt_tok_t;

// Split on whitespace; fold each word. Sentence punctuation (?!;"() and a final , .) leaves the folded form,
// and ?! leaves the raw one too. Returns the number of words.
static int pt_split(const char *in, pt_tok_t *w)
{
    int n = 0;
    const unsigned char *p = (const unsigned char *)in;
    while (*p && n < PT_MAXW) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        if (!*p) break;
        const unsigned char *st = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') p++;
        size_t rl = (size_t)(p - st);
        while (rl && strchr("?!;\")(", st[rl - 1])) rl--;          // "horas?" -> "horas"
        int k = 0;
        for (size_t i = 0; i < rl && k < PT_WLEN - 1; i++) {
            char c = 0;
            if (st[i] == 0xC3 && i + 1 < rl) c = pt_fold_c3(st[++i]);
            else if (st[i] < 0x80 && (isalnum(st[i]) || strchr("%.,:/-_+", st[i]))) c = (char)tolower(st[i]);
            if (c) w[n].f[k++] = c;
        }
        while (k && strchr(".,:", w[n].f[k - 1])) k--;   // "calculadora," / "anota:" -> the bare word
        w[n].f[k] = 0;
        if (!k && !rl) continue;
        if (rl > 255) rl = 255;
        w[n].raw = (const char *)st; w[n].rawlen = (unsigned char)rl;
        n++;
    }
    return n;
}

// Does entry `pt` (space-separated folded words) match the words starting at w[i]? Returns how many words.
static int pt_match_words(const pt_tok_t *w, int n, int i, const char *pt)
{
    int used = 0;
    while (*pt) {
        if (i + used >= n) return 0;
        size_t l = strcspn(pt, " ");
        if (strlen(w[i + used].f) != l || strncmp(w[i + used].f, pt, l)) return 0;
        used++; pt += l; while (*pt == ' ') pt++;
    }
    return used;
}

// Small Portuguese number words -> value (0 = not one).
static int pt_numword(const char *f)
{
    static const struct { const char *w; int v; } N[] = {
        {"um",1},{"uma",1},{"dois",2},{"duas",2},{"tres",3},{"quatro",4},{"cinco",5},{"seis",6},{"sete",7},
        {"oito",8},{"nove",9},{"dez",10},{"onze",11},{"doze",12},{"treze",13},{"quatorze",14},{"catorze",14},
        {"quinze",15},{"dezesseis",16},{"dezessete",17},{"dezoito",18},{"dezenove",19},{"vinte",20},{"trinta",30},
        {"quarenta",40},{"quarenta e cinco",45},{"cinquenta",50},{"sessenta",60},{"noventa",90},{"cem",100} };
    for (size_t i = 0; i < sizeof N / sizeof N[0]; i++) if (!strcmp(N[i].w, f)) return N[i].v;
    return 0;
}
static bool pt_isnum(const pt_tok_t *t) { return isdigit((unsigned char)t->f[0]) || pt_numword(t->f); }

// A time unit after a number -> its English word ("5 horas" -> "5 hours"); NULL if not a unit.
static const char *pt_unit(const char *f)
{
    static const char *const U[][2] = { {"segundo","second"},{"segundos","seconds"},{"minuto","minute"},
        {"minutos","minutes"},{"min","min"},{"hora","hour"},{"horas","hours"},{"dia","day"},{"dias","days"},
        {"semana","week"},{"semanas","weeks"} };
    for (size_t i = 0; i < sizeof U / sizeof U[0]; i++) if (!strcmp(U[i][0], f)) return U[i][1];
    return NULL;
}

// Context rules that a word table cannot express (they look at the neighbours). Return the English for w[i]
// (consuming *used words) or NULL. They apply in the user's text too: they are time/arithmetic structure.
static const char *pt_context(const pt_tok_t *w, int n, int i, int *used, char *num, size_t numcap)
{
    const char *f = w[i].f;
    bool next_num = i + 1 < n && pt_isnum(&w[i + 1]);
    bool prev_num = i > 0 && pt_isnum(&w[i - 1]);
    *used = 1;
    // "para/pare o cronometro" at the start of a line is the verb "to stop", not the preposition "to"
    if (i == 0 && (!strcmp(f, "para") || !strcmp(f, "pare")) && !next_num) return "stop";
    // "7 da manha" -> "7am", "7 da tarde/noite" -> "7pm" (the timer reads am/pm glued to the number)
    if (isdigit((unsigned char)f[0]) && !strchr(f, ':') && i + 2 < n && !strcmp(w[i + 1].f, "da")) {
        const char *ap = !strcmp(w[i + 2].f, "manha") ? "am"
                       : (!strcmp(w[i + 2].f, "tarde") || !strcmp(w[i + 2].f, "noite")) ? "pm" : NULL;
        if (ap) { snprintf(num, numcap, "%s%s", f, ap); *used = 3; return num; }
    }
    // "as 7" / "para as 7" / "ate as 7" -> "at 7"
    if ((!strcmp(f, "para") || !strcmp(f, "ate")) && i + 2 < n && !strcmp(w[i + 1].f, "as") && pt_isnum(&w[i + 2])) { *used = 2; return "at"; }
    if ((!strcmp(f, "as") || !strcmp(f, "a")) && next_num && !(i + 2 < n && pt_unit(w[i + 2].f))) return "at";
    // "daqui a 5 minutos" / "em 5 minutos" -> "in 5 minutes"; "de/por/durante 5 minutos" -> "for 5 minutes"
    bool num_unit = next_num && i + 2 < n && pt_unit(w[i + 2].f);
    if (!strcmp(f, "daqui") && i + 1 < n && !strcmp(w[i + 1].f, "a")) { *used = 2; return "in"; }
    if (!strcmp(f, "dentro") && i + 1 < n && !strcmp(w[i + 1].f, "de")) { *used = 2; return "in"; }
    if (num_unit && !strcmp(f, "em")) return "in";
    if (num_unit && (!strcmp(f, "de") || !strcmp(f, "por") || !strcmp(f, "durante"))) return "for";
    // a number word before a unit or next to an operator -> digits ("cinco minutos" -> "5 minutes")
    int v = pt_numword(f);
    if (v && ((i + 1 < n && (pt_unit(w[i + 1].f) || !strcmp(w[i + 1].f, "vezes") || !strcmp(w[i + 1].f, "mais") ||
                              !strcmp(w[i + 1].f, "menos"))) || (i > 0 && !strcmp(w[i - 1].f, "as")))) {
        snprintf(num, numcap, "%d", v); return num;
    }
    if (prev_num) { const char *u = pt_unit(f); if (u) return u; }
    // arithmetic between two numbers: "12 mais 3", "10 menos 4", "6 x 7"
    if (prev_num && next_num) {
        if (!strcmp(f, "mais"))  return "plus";
        if (!strcmp(f, "menos")) return "minus";
    }
    return NULL;
}

bool a_pt_bridge(const char *in, char *out, size_t cap)
{
    pt_tok_t w[PT_MAXW];
    char num[12];
    if (!in || !out || cap < 2) return false;
    int nw = pt_split(in, w);
    size_t n = 0; bool cue = false, text = false;
    for (int i = 0; i < nw; ) {
        const char *e = NULL; size_t el = 0; int used = 1;
        const char *c = pt_context(w, nw, i, &used, num, sizeof num);
        if (c) { e = c; el = strlen(c); }
        else {
            const a_pt_word_t *best = NULL; int bu = 0;
            for (size_t k = 0; k < sizeof PT_WORDS / sizeof PT_WORDS[0]; k++) {
                if (text && !(PT_WORDS[k].flags & PT_KEEP)) continue;   // the user's text: only time words translate
                int u = pt_match_words(w, nw, i, PT_WORDS[k].pt);
                if (u > bu) { bu = u; best = &PT_WORDS[k]; }
            }
            if (best) {
                e = best->en; el = strlen(e); used = bu;
                if (best->flags & PT_CUE) cue = true;
                if (best->flags & PT_TEXT) text = true;
            } else if (text) { e = w[i].raw; el = w[i].rawlen; }        // as typed, accents kept
            else             { e = w[i].f;   el = strlen(w[i].f); }
        }
        i += used;
        if (el == 0) continue;                                           // deleted glue ("por favor")
        if (n && n + 1 < cap) out[n++] = ' ';
        if (n + el >= cap) el = cap - 1 - n;
        memcpy(out + n, e, el); n += el;
    }
    out[n] = 0;
    return cue;
}

// ---------------------------------------------------------------------------------------------------------
// 2. Reply catalog
// ---------------------------------------------------------------------------------------------------------
// en/pt templates: "%s" in en is a wildcard capture (up to 5, non-empty); "%1".."%5" in pt is that capture,
// localized through PT_TERMS (app ids, weekdays, months, units...). Every other byte is literal.
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
    { NULL,       "{value}.",                        "{value}." },   // the executor fills a pt value
    { NULL,       "{value}",                         "{value}" },
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
    // reminders / timers / alarms (tool_event, tool_timer)
    { "add_event", "Reminder \"%s\" today at %s.",     "Lembrete \"%1\" hoje as %2." },
    { "add_event", "Reminder \"%s\" tomorrow at %s.",  "Lembrete \"%1\" amanha as %2." },
    { "add_event", "Reminder \"%s\" in %s days at %s.","Lembrete \"%1\" daqui a %2 dias as %3." },
    { "add_event", "Reminder \"%s\" today.",           "Lembrete \"%1\" hoje." },
    { "add_event", "Reminder \"%s\" tomorrow.",        "Lembrete \"%1\" amanha." },
    { "add_event", "Reminder \"%s\" in %s days.",      "Lembrete \"%1\" daqui a %2 dias." },
    { "add_event", "Reminder \"%s\" — I'll alert you at %s.", "Lembrete \"%1\": aviso as %2." },
    { "add_event", "%s of %s — I'll alert you at %s.", "%1 de %2: aviso as %3." },
    { "add_event", "%s set for %s tomorrow.",         "%1 para amanha as %2." },
    { "add_event", "%s set for %s.",                  "%1 para as %2." },
    // files and notes
    { NULL,       "Creating %s in %s.",               "Criando %1 em %2." },
    { NULL,       "Creating %s with: %s",             "Criando %1 com: %2" },
    // arithmetic, percentages, conversions
    { "calc",     "It's %s.",                         "Da %1." },
    { "percent",  "%s% of %s = %s.",                  "%1% de %2 = %3." },
    { "convert",  "%s %s = %s %s.",                   "%1 %2 = %3 %4." },
    // calendar questions (anima_solve.c): "December 25, 2026" -> "25 de dezembro de 2026"
    { "date",     "%s %s, %s is today.",              "%2 de %1 de %3 e hoje." },
    { "date",     "1 day until %s %s, %s.",           "Falta 1 dia para %2 de %1 de %3." },
    { "date",     "%s days until %s %s, %s.",         "Faltam %1 dias para %3 de %2 de %4." },
    { "date",     "%s %s, %s was %s days ago.",       "%2 de %1 de %3 foi ha %4 dias." },
    { "date",     "%s %s, %s falls on a %s.",         "%2 de %1 de %3 cai em %4." },
    { "date",     "In %s days it will be %s, %s %s, %s.", "Daqui a %1 dias sera %2, %4 de %3 de %5." },
    { "date",     "%s days ago it was %s, %s %s, %s.", "Ha %1 dias foi %2, %4 de %3 de %5." },
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
    { "Alarm", "Alarme" }, { "reminder", "lembrete" }, { "Documents", "Documentos" }, { "Notes", "Notas" },
    { "miles", "milhas" }, { "mile", "milha" }, { "meters", "metros" }, { "feet", "pes" }, { "inches", "polegadas" },
    { "pounds", "libras" }, { "grams", "gramas" }, { "liters", "litros" }, { "gallons", "galoes" },
    { "kilometers", "quilometros" }, { "centimeters", "centimetros" }, { "ounces", "oncas" },
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

#ifdef ANIMA_HOST
bool g_anima_pt_miss;   // host harness only: the last pt reply had no catalog row (pt-check lists them)
#endif

bool a_pt_reply(const char *intent, char *reply, size_t cap)
{
#ifdef ANIMA_HOST
    g_anima_pt_miss = reply && reply[0];
#endif
    if (!reply || !reply[0]) return false;
    for (size_t i = 0; i < sizeof PT_REPLIES / sizeof PT_REPLIES[0]; i++) {
        const a_pt_reply_t *e = &PT_REPLIES[i];
        if (e->intent && (!intent || strcmp(e->intent, intent))) continue;
        const char *c[5]; size_t cl[5];
        if (!pt_tmatch(e->en, reply, c, cl, 0, 5)) continue;
        char out[320]; size_t n = 0;
        for (const char *p = e->pt; *p && n + 1 < sizeof out; p++) {
            if (p[0] == '%' && p[1] >= '1' && p[1] <= '5') {
                int k = p[1] - '1'; p++;
                const char *v = c[k]; size_t vl = cl[k];
                const char *t = pt_term(v, vl);
                if (t) { v = t; vl = strlen(t); }
                if (n + vl >= sizeof out) vl = sizeof out - 1 - n;
                memcpy(out + n, v, vl); n += vl;
            } else out[n++] = *p;
        }
        out[n] = 0;
#ifdef ANIMA_HOST
        g_anima_pt_miss = false;
#endif
        snprintf(reply, cap, "%s", out);
        return true;
    }
    return false;
}

// A timer/alarm/reminder with no text of its own is saved with the English label ("off=1;time=07:00;text=Alarm").
// The calendar shows that text, so it is relabelled. A user's own text is never touched (it is not a label).
void a_pt_event_label(char *content, size_t cap)
{
    static const char *const L[][2] = { {"Alarm","Alarme"}, {"Timer","Timer"}, {"reminder","lembrete"} };
    char *t = content ? strstr(content, ";text=") : NULL;
    if (!t) return;
    t += 6;
    for (size_t i = 0; i < sizeof L / sizeof L[0]; i++)
        if (!strcmp(t, L[i][0])) { snprintf(t, cap - (size_t)(t - content), "%s", L[i][1]); return; }
}
