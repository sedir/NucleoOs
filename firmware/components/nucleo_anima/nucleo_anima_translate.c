// ANIMA offline translation tier — see nucleo_anima_translate.h.
// Network-free (libc only), so it compiles and runs in the host harness like the profile/learn tiers.
// Self-contained: it re-tokenizes the RAW input with a byte-for-byte port of a_tokenize() (the firmware
// normalizer is static in nucleo_anima.c), so the query phrase produces the same keys the generator wrote.
#include "nucleo_anima_translate.h"
#include "nucleo_board.h"      // NUCLEO_SD_MOUNT
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdbool.h>

#define T_MAX_TOKENS 24       // mirror firmware a_tokenize()
#define T_TOK_LEN    24       // mirror firmware (cap = 23 usable chars)

#define DICT_IT_EN  NUCLEO_SD_MOUNT "/data/anima/dict-it-en.tsv"   // IT key -> EN translations
#define DICT_EN_IT  NUCLEO_SD_MOUNT "/data/anima/dict-en-it.tsv"   // EN key -> IT translations
// Portuguese and Japanese (tools/anima/build_dict_multi.py). Japanese is ROMAJI on both sides (the native font
// is ASCII-only); pt<->ja pivots through the primary English sense and the value says "(via <english>)".
#define DICT_DIR    NUCLEO_SD_MOUNT "/data/anima/dict-"
// Fold an accented letter (the byte AFTER 0xC3) to bare ASCII; 0 if not one we fold. The Italian vowels of
// a_tokenize() plus Portuguese ã õ ç and the capitals (build_dict_multi.py norm_key() is the twin). The it/en
// dictionary keys are pure ASCII, so the wider set changes none of their lookups.
static char t_fold(unsigned char d)
{
    if (d >= 0x80 && d <= 0x9E && d != 0x97) d += 0x20;  // À..Þ -> à..þ (0x97 is ×)
    switch (d) {
        case 0xA0: case 0xA1: case 0xA2: case 0xA3: return 'a';   // à á â ã
        case 0xA7: return 'c';                                     // ç
        case 0xA8: case 0xA9: case 0xAA: return 'e';               // è é ê
        case 0xAC: case 0xAD: case 0xAE: return 'i';               // ì í î
        case 0xB2: case 0xB3: case 0xB4: case 0xB5: return 'o';    // ò ó ô õ
        case 0xB9: case 0xBA: case 0xBB: return 'u';               // ù ú û
        default: return 0;
    }
}

// Port of a_tokenize(): fold IT vowels, keep lowercased ASCII alnum, split on everything else.
static int t_tokenize(const char *in, char tok[T_MAX_TOKENS][T_TOK_LEN])
{
    int n = 0, len = 0;
    char cur[T_TOK_LEN];
    for (const unsigned char *p = (const unsigned char *)in; ; p++) {
        unsigned char c = *p;
        char out = 0;
        if (c == 0xC3 && p[1]) { out = t_fold(*++p); }
        else if (isalnum(c))   { out = (char)tolower(c); }
        if (out) {
            if (len < T_TOK_LEN - 1) cur[len++] = out;
        } else {
            if (len > 0 && n < T_MAX_TOKENS) { cur[len] = 0; memcpy(tok[n++], cur, len + 1); len = 0; }
            if (c == 0) break;
        }
    }
    return n;
}

static bool t_eq(const char *a, const char *b) { return strcmp(a, b) == 0; }
static bool t_starts(const char *s, const char *pre) { return strncmp(s, pre, strlen(pre)) == 0; }

// An explicit translate verb ("traduci/traduce/traduco/tradurre/traducimi", "translate/translating").
static bool t_is_verb(const char *w)
{
    // Portuguese verb forms are listed exactly: "traduz" is also the prefix of the Italian NOUN "traduzione"
    static const char *const pt[] = { "traduz", "traduza", "traduzir", "traduzindo", "traduzo", "traduzam", "traduzi", NULL };
    for (int i = 0; pt[i]; i++) if (t_eq(w, pt[i])) return true;
    return t_starts(w, "traduc") || t_starts(w, "tradur") || t_starts(w, "translat");
}
// The noun "traduzione/translation" — a weaker trigger, accepted only WITH an explicit target language.
static bool t_is_noun(const char *w)
{
    return t_starts(w, "traduzion") || t_eq(w, "translation") || t_eq(w, "translations") || t_eq(w, "traducao");
}
// A target-language word -> 'e' English / 'i' Italian / 'p' Portuguese / 'j' Japanese / 0 (not a language).
// it/en/pt spellings: inglese/english/ingles, portoghese/portuguese/portugues, giapponese/japanese/japones.
static char t_lang(const char *w)
{
    if (t_starts(w, "ingles") || t_eq(w, "english")) return 'e';
    if (t_starts(w, "italian") || t_eq(w, "italiano")) return 'i';
    if (t_starts(w, "portug") || t_starts(w, "portog")) return 'p';
    if (t_starts(w, "japon") || t_starts(w, "japan") || t_starts(w, "giappon")) return 'j';
    return 0;
}
// Function words trimmed from the BORDERS of the target span (never from the middle, so "come stai"
// survives). Deliberately excludes come/how/say/etc. — those are real content unless part of a trigger.
static bool t_is_border_word_pt(const char *w)   // Portuguese ones, only for a pt user (en "no"/"do" are words)
{
    static const char *fw[] = { "o","os","as","do","da","dos","das","de","em","para","pra","no","na","um","uma",
                                "palavra","expressao","frase", NULL };
    for (int i = 0; fw[i]; i++) if (t_eq(w, fw[i])) return true;
    return false;
}
static bool t_is_border_word(const char *w)
{
    static const char *fw[] = { "di","del","dello","della","dell","la","il","lo","le","l","un","una","uno",
                                "parola","parole","frase","word","words","phrase","the","a","an","mi","me",
                                "per","in","into","to","verso","nel","nella","that","this", NULL };
    for (int i = 0; fw[i]; i++) if (t_eq(w, fw[i])) return true;
    return false;
}

// Is the token sequence `pat` (nul-terminated list) present consecutively starting somewhere in tok[]?
// Returns the start index or -1. Used for the multi-word frames ("come si dice", "how do you say").
static int t_phrase(char tok[T_MAX_TOKENS][T_TOK_LEN], int ntok, const char *const *pat)
{
    int np = 0; while (pat[np]) np++;
    for (int s = 0; s + np <= ntok; s++) {
        int k = 0;
        for (; k < np; k++) if (!t_eq(tok[s + k], pat[k])) break;
        if (k == np) return s;
    }
    return -1;
}

// Exact dictionary lookup by BINARY SEARCH over an SD file sorted by key in strcmp/byte order. The full
// FreeDict bilingual dictionary is ~60k lines / ~2 MB — a linear scan would crawl megabytes off the SD per
// query. Instead we bisect the byte range (~log2(size) ≈ 22 seeks), each step seeking to the midpoint and
// snapping to the next line start, then LINEAR-scan the final small window (robust against line-boundary
// edge cases). One ~1 KB line buffer, ZERO resident RAM — so the whole dictionary is queried on the MCU in
// a couple dozen disk reads. Returns 1 and fills `out` (readable translations) on an exact key hit, else 0.
static int t_lookup(const char *path, const char *key, char *out, size_t cap)
{
    FILE *f = fopen(path, "rb");                   // binary: ftell/fseek offsets must be byte-exact
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
    long hi = ftell(f), lo = 0;
    char line[1024];
    int c, found = 0;
    while (hi - lo > 4096) {                        // bisect down to a small window
        long mid = lo + (hi - lo) / 2;
        fseek(f, mid, SEEK_SET);
        while ((c = fgetc(f)) != EOF && c != '\n') {}   // snap to the start of the next line
        long ls = ftell(f);
        if (ls >= hi || !fgets(line, sizeof line, f)) { hi = mid; continue; }
        char *tab = strchr(line, '\t');
        if (!tab) { lo = ftell(f); continue; }
        *tab = 0;
        if (strcmp(line, key) < 0) lo = ftell(f);  // key sorts AFTER this line -> search right
        else hi = mid;                             // key sorts at/before this line -> search left
    }
    // linear scan the final window from lo; a bounded ~8 KB read, exact and edge-case-proof. NB: lo is
    // ALWAYS a line boundary here (it only ever advances to an ftell() taken right after a full line), so
    // we must NOT skip a "partial" line — that would drop the window's first line (the bug that hid "sole").
    fseek(f, lo, SEEK_SET);
    long scanned = 0;
    while (scanned < 8192 && fgets(line, sizeof line, f)) {
        scanned += (long)strlen(line);
        char *tab = strchr(line, '\t');
        if (!tab) continue;
        *tab = 0;
        int cmp = strcmp(line, key);
        if (cmp == 0) {
            char *val = tab + 1; size_t vl = strlen(val);
            while (vl && (val[vl-1] == '\n' || val[vl-1] == '\r' || val[vl-1] == ' ')) val[--vl] = 0;
            snprintf(out, cap, "%s", val); found = 1; break;
        }
        if (cmp > 0) break;                        // sorted: passed where the key would be
    }
    fclose(f);
    return found;
}

// Multi-word frames that signal a translation request ("come si dice", "how do you say"). File-scope so
// both the translator and the detect-only entry point share one definition.
static const char *const T_FR_csd[]  = { "come", "si", "dice", NULL };
static const char *const T_FR_csdn[] = { "come", "si", "dicono", NULL };
static const char *const T_FR_css[]  = { "come", "si", "scrive", NULL };    // "come si scrive X in inglese"
static const char *const T_FR_cssn[] = { "come", "si", "scrivono", NULL };
static const char *const T_FR_hdys[] = { "how", "do", "you", "say", NULL };
static const char *const T_FR_hdis[] = { "how", "do", "i", "say", NULL };
static const char *const T_FR_hts[]  = { "how", "to", "say", NULL };
static const char *const T_FR_hdyw[] = { "how", "do", "you", "spell", NULL };
static const char *const T_FR_hdywr[]= { "how", "do", "you", "write", NULL };
static const char *const T_FR_csdz[] = { "como", "se", "diz", NULL };       // pt: "como se diz X em japones"
static const char *const T_FR_csfa[] = { "como", "se", "fala", NULL };
static const char *const T_FR_csec[] = { "como", "se", "escreve", NULL };
static const char *const T_FR_cedg[] = { "como", "eu", "digo", NULL };
static const char *const *const T_FRAMES[] = { T_FR_csd, T_FR_csdn, T_FR_css, T_FR_cssn,
                                               T_FR_hdys, T_FR_hdis, T_FR_hts, T_FR_hdyw, T_FR_hdywr,
                                               T_FR_csdz, T_FR_csfa, T_FR_csec, T_FR_cedg, NULL };

// Mark each present frame's tokens in `excl` (when non-NULL) and return whether any frame matched.
static bool t_mark_frames(char tok[T_MAX_TOKENS][T_TOK_LEN], int ntok, bool *excl)
{
    bool found = false;
    for (int fi = 0; T_FRAMES[fi]; fi++) {
        int at = t_phrase(tok, ntok, T_FRAMES[fi]);
        if (at >= 0) { found = true; if (excl) for (int k = 0; T_FRAMES[fi][k]; k++) excl[at + k] = true; }
    }
    return found;
}

// Detect-ONLY: is `raw` a translation request? No dictionary lookup. Lets the orchestrator route to the
// online teacher (Grok) in hybrid/online mode, keeping the dictionary as the offline floor / fallback.
bool nucleo_anima_translate_is_request(const char *raw)
{
    if (!raw || !raw[0]) return false;
    char tok[T_MAX_TOKENS][T_TOK_LEN];
    int ntok = t_tokenize(raw, tok);
    if (ntok < 1) return false;
    bool verb = false, noun = false; char lang = 0;
    for (int i = 0; i < ntok; i++) {
        if (t_is_verb(tok[i])) verb = true;
        else if (t_is_noun(tok[i])) noun = true;
        char l = t_lang(tok[i]); if (l && !lang) lang = l;
    }
    return verb || t_mark_frames(tok, ntok, NULL) || (noun && lang);
}

// Look `key` up in dict-<src>-<dst>.tsv (src/dst: two-letter codes).
static int t_lookup_pair(const char *src, const char *dst, const char *key, char *out, size_t cap)
{
    char path[64];
    snprintf(path, sizeof path, DICT_DIR "%s-%s.tsv", src, dst);
    return t_lookup(path, key, out, cap);
}
static const char *t_code(char l) { return l == 'e' ? "en" : l == 'i' ? "it" : l == 'p' ? "pt" : "ja"; }
// Language name in the reply: Italian for an Italian user, English otherwise (the pt reply catalog localizes it).
static const char *t_name(char l, bool en)
{
    if (en) return l == 'e' ? "English" : l == 'i' ? "Italian" : l == 'p' ? "Portuguese" : "Japanese";
    // "lingua giapponese": anima_text's cleanup excises " in giapponese ..." clauses (knowledge transliterations)
    return l == 'e' ? "inglese" : l == 'i' ? "italiano" : l == 'p' ? "portoghese" : "lingua giapponese";
}

int nucleo_anima_translate(const char *raw, bool en, anima_result_t *r)
{
    return nucleo_anima_translate_ex(raw, en, false, r);
}

int nucleo_anima_translate_ex(const char *raw, bool en, bool pt, anima_result_t *r)
{
    if (!raw || !raw[0]) return 0;
    char tok[T_MAX_TOKENS][T_TOK_LEN];
    int ntok = t_tokenize(raw, tok);
    if (ntok < 1) return 0;                        // empty input -> nothing to do

    // --- classify each token; detect the trigger -------------------------------------------------
    bool excl[T_MAX_TOKENS] = { 0 };
    bool verb = false, noun = false;
    char lang = 0;                                  // target language: 'e' EN, 'i' IT, 0 = auto
    for (int i = 0; i < ntok; i++) {
        if (t_is_verb(tok[i])) { excl[i] = true; verb = true; continue; }
        if (t_is_noun(tok[i])) { excl[i] = true; noun = true; continue; }
        char l = t_lang(tok[i]);
        if (l) { excl[i] = true; if (!lang) lang = l; }
    }
    // Multi-word frames. Each present frame flags the trigger and masks its own tokens (excl).
    bool phrase = t_mark_frames(tok, ntok, excl);

    bool trigger = verb || phrase || (noun && lang);
    if (!trigger) return 0;                         // not a translation request -> let the cascade run

    // --- isolate the target span: the longest run of non-excluded tokens, border-trimmed -----------
    int bs = -1, bl = 0, cs = -1, cl = 0;
    for (int i = 0; i <= ntok; i++) {
        if (i < ntok && !excl[i]) { if (cs < 0) { cs = i; cl = 1; } else cl++; }
        else { if (cl > bl) { bl = cl; bs = cs; } cs = -1; cl = 0; }
    }
    int s = bs, e = bs + bl;                        // [s, e)
    while (s < e && (t_is_border_word(tok[s]) || (pt && t_is_border_word_pt(tok[s])))) s++;          // trim leading
    while (e > s && (t_is_border_word(tok[e - 1]) || (pt && t_is_border_word_pt(tok[e - 1])))) e--; // and trailing

    // A NUMBER is not an IT<->EN word: "traduci 10 in binario" is a base conversion, "traduci 5" is nothing.
    // Decline so the math/base skill owns it — don't swallow the turn with a dictionary miss.
    for (int i = s; i < e; i++) {
        bool alldig = tok[i][0] != 0;
        for (const char *p = tok[i]; *p; p++) if (!isdigit((unsigned char)*p)) { alldig = false; break; }
        if (alldig) return 0;
    }

    r->tier = ANIMA_TIER_COMMAND;
    r->action = ANIMA_ACT_ANSWER;
    snprintf(r->intent, sizeof r->intent, "translate");
    snprintf(r->state, sizeof r->state, "tool");

    if (s >= e) {                                   // a translate verb, but nothing to translate
        r->confidence = 60;
        snprintf(r->reply, sizeof r->reply,
                 en ? "What should I translate? e.g. \"translate dog to Italian\"."
                    : "Cosa traduco? es. \"traduci cane in inglese\".");
        return 1;
    }
    bool newlang = pt || lang == 'p' || lang == 'j';   // Portuguese/Japanese involved: the multi-pair search

    // Rebuild the normalized key from the span (single-spaced) — same shape gen_dicts.py wrote.
    char key[T_MAX_TOKENS * T_TOK_LEN];
    int ko = 0;
    for (int i = s; i < e; i++) {
        if (i > s && ko < (int)sizeof key - 1) key[ko++] = ' ';
        for (const char *q = tok[i]; *q && ko < (int)sizeof key - 1; q++) key[ko++] = *q;
    }
    key[ko] = 0;

    // A single-letter source is degenerate and an accent-fold trap: "traduci è" folds to "e" and would
    // return "and" (è = copula "is", NOT the conjunction "e"/and). Decline rather than assert a false hit.
    if (ko <= 1) return 0;

    // --- Portuguese / Japanese: try the source languages that have a dictionary INTO the target, the user's
    // own language first. No target ("traduz cachorro"): Portuguese -> English, else into Portuguese. ----------
    char val[512], itv[512], env[512];
    if (newlang) {
        char cand[4][2]; int nc = 0;                   // (source, target) pairs to try
        #define TRY(sr, tg) do { if (nc < 4) { cand[nc][0] = (sr); cand[nc][1] = (tg); nc++; } } while (0)
        if (lang == 'e')      { if (pt) TRY('p','e'); TRY('i','e'); if (!pt) TRY('p','e'); TRY('j','e'); }
        else if (lang == 'p') { TRY('e','p'); TRY('j','p'); }
        else if (lang == 'j') { if (pt) TRY('p','j'); TRY('e','j'); if (!pt) TRY('p','j'); }
        else if (lang == 'i') { TRY('e','i'); }
        else                  { TRY('p','e'); TRY('e','p'); TRY('j','p'); TRY('i','e'); }
        #undef TRY
        // An Italian user into Portuguese/Japanese: no it-pt/it-ja dictionary, so pivot through the PRIMARY
        // English sense of the Italian word ("cane" -> "dog" -> "inu"), marked "(via dog)" like the built pairs.
        if (!en && !pt && (lang == 'p' || lang == 'j') && t_lookup(DICT_IT_EN, key, itv, sizeof itv)) {
            char ek[64]; int k = 0;
            for (const char *q = itv; *q && *q != ',' && *q != ';' && k < (int)sizeof ek - 1; q++) ek[k++] = *q;
            while (k > 0 && ek[k - 1] == ' ') k--;
            ek[k] = 0;
            if (ek[0] && t_lookup_pair("en", t_code(lang), ek, env, sizeof env)) {
                char one[96]; k = 0;                   // first target word only: the pivot is one sense
                for (const char *q = env; *q && *q != ';' && k < (int)sizeof one - 1; q++) one[k++] = *q;
                one[k] = 0;
                r->confidence = 90;
                snprintf(r->reply, sizeof r->reply, "\"%s\" in %s: %s (via %s).", key, t_name(lang, en), one, ek);
                snprintf(r->trace, sizeof r->trace, "dizionario · it->en->%s", t_code(lang));
                return 1;
            }
        }
        for (int c = 0; c < nc; c++) {
            if (!t_lookup_pair(t_code(cand[c][0]), t_code(cand[c][1]), key, val, sizeof val)) continue;
            r->confidence = 95;
            snprintf(r->reply, sizeof r->reply, "\"%s\" in %s: %s.", key, t_name(cand[c][1], en), val);
            snprintf(r->trace, sizeof r->trace, en ? "dict lookup · %s->%s" : "dizionario · %s->%s",
                     t_code(cand[c][0]), t_code(cand[c][1]));
            return 1;
        }
        r->confidence = 55;
        snprintf(r->reply, sizeof r->reply, en ? "I don't have \"%s\" in the offline dictionaries."
                                               : "Non ho \"%s\" nei dizionari offline.", key);
        snprintf(r->trace, sizeof r->trace, en ? "dict miss" : "dizionario: assente");
        return 1;
    }

    // --- lookup, honoring the requested direction (or auto-detecting it) ---------------------------
    bool to_en = false, hit = false;
    bool in_it = t_lookup(DICT_IT_EN, key, itv, sizeof itv);   // key is an Italian headword
    bool in_en = t_lookup(DICT_EN_IT, key, env, sizeof env);   // key is an English headword
    // HOMOGRAPH GUARD: a word that exists in BOTH languages ("male", "sole", "estate", "fame", "camera",
    // "fine") is ambiguous about its SOURCE — the engine must NOT confidently emit one assumed-source
    // reading ("translate male" / "how do you say male" -> "evil" for the English word "male"). Show BOTH
    // labelled readings so a single answer can never mislead. Applies to EVERY path (explicit direction,
    // "come si dice"/"how do you say" frames, and bare auto) — the frame/auto path set no t_lang, which is
    // exactly where the IT-first default used to surface the wrong-language sense at conf 95.
    // DIREZIONE ESPLICITA ("in inglese"/"in italiano") -> la SORGENTE e' l'ALTRA lingua, quindi NON e'
    // ambigua: traduci in quel verso. (Era il bug: "traduci cane in inglese" dava il dump omografo invece
    // di "dog" perche' la guardia omografo scattava PRIMA della direzione.)
    if (lang == 'e')      { hit = in_it; to_en = true;  if (hit) snprintf(val, sizeof val, "%s", itv); }
    else if (lang == 'i') { hit = in_en; to_en = false; if (hit) snprintf(val, sizeof val, "%s", env); }
    // AUTO (nessuna direzione): se la parola esiste in ENTRAMBE, la SORGENTE e' ambigua -> mostra i due
    // sensi etichettati per non ingannare ("translate male" -> non assumere l'EN "evil"). Solo qui.
    else if (in_it && in_en) {
        r->tier = ANIMA_TIER_COMMAND; r->action = ANIMA_ACT_ANSWER; r->confidence = 75;
        snprintf(r->intent, sizeof r->intent, "translate"); snprintf(r->state, sizeof r->state, "tool");
        snprintf(r->reply, sizeof r->reply,
                 en ? "\"%s\" exists in both languages. IT->EN: %s. EN->IT: %s."
                    : "\"%s\" esiste in entrambe le lingue. IT->EN: %s. EN->IT: %s.", key, itv, env);
        snprintf(r->trace, sizeof r->trace, en ? "dict · homograph" : "dizionario · omografo");
        return 1;
    }
    else {                                          // auto, non-omografo: deduci la lingua sorgente
        if (in_it) { hit = true; to_en = true;  snprintf(val, sizeof val, "%s", itv); }
        else if (in_en) { hit = true; to_en = false; snprintf(val, sizeof val, "%s", env); }
    }

    const char *ln = to_en ? (en ? "English" : "inglese") : (en ? "Italian" : "italiano");
    if (hit) {
        r->confidence = 95;
        snprintf(r->reply, sizeof r->reply, "\"%s\" %s %s: %s.",
                 key, en ? "in" : "in", ln, val);
        snprintf(r->trace, sizeof r->trace, en ? "dict lookup · %s" : "dizionario · %s", ln);
        return 1;
    }

    // An English/Italian user may still type a Portuguese or Japanese (romaji) word: into English only.
    if (lang == 'e' || lang == 0) {
        static const char SRC[2] = { 'p', 'j' };
        for (int c = 0; c < 2; c++) {
            if (!t_lookup_pair(t_code(SRC[c]), "en", key, val, sizeof val)) continue;
            r->confidence = 95;
            snprintf(r->reply, sizeof r->reply, "\"%s\" in %s: %s.", key, t_name('e', en), val);
            snprintf(r->trace, sizeof r->trace, en ? "dict lookup · %s->en" : "dizionario · %s->en", t_code(SRC[c]));
            return 1;
        }
    }
    // Miss on an explicit translation request: decline honestly and STOP (no later tier fabricates one).
    r->confidence = 55;
    snprintf(r->reply, sizeof r->reply,
             en ? "I don't have \"%s\" in the offline IT<->EN dictionary."
                : "Non ho \"%s\" nel dizionario offline IT<->EN.", key);
    snprintf(r->trace, sizeof r->trace, en ? "dict miss" : "dizionario: assente");
    return 1;
}
