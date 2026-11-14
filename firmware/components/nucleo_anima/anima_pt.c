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
//      not know returns false; the caller declines untranslated knowledge. A separate streamed
//      Portuguese SD pack in anima_pt_knowledge.c handles broad frozen knowledge.
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
//        PT_KEEP  translated even inside the user's text (time words the tool must still parse: "amanha");
//        PT_UNIT  a measurement unit: a line with a number and TWO units is a conversion ("quantos km sao
//                 13 milhas", "13 milhas em km") even with no conversion verb.
// Longest match wins, so phrases ("que horas sao") must simply be listed; order does not matter.
#define PT_CUE  1
#define PT_TEXT 2
#define PT_KEEP 4
#define PT_UNIT 8
typedef struct { const char *pt; const char *en; unsigned char flags; } a_pt_word_t;

static const a_pt_word_t PT_WORDS[] = {
    { "obrigado", "thanks", PT_CUE },
    { "obrigada", "thanks", PT_CUE },
    { "muito obrigado", "thank you", PT_CUE },
    { "muito obrigada", "thank you", PT_CUE },
    { "valeu",            "thanks", PT_CUE },
    { "brigado",          "thanks", PT_CUE },
    // --- the IDEAS deck / skill templates (app_anima.cpp LEAVES, pt catalog) -> the en template phrasing ----
    { "qual a versao do nucleoos", "what nucleoos version is this", PT_CUE },
    { "qual e a versao do nucleoos", "what nucleoos version is this", PT_CUE },
    { "que dia da semana e",  "what day is", PT_CUE },
    { "que dia da semana cai","what day is", PT_CUE },
    { "quantos dias tem",     "how many days in", PT_CUE },
    { "qual a idade de quem nasceu em", "how old is someone born in", PT_CUE },
    { "quantos anos tem quem nasceu em", "how old is someone born in", PT_CUE },
    { "que horas sao em",     "what time is it in", PT_CUE },
    { "que horas sao no",     "what time is it in", PT_CUE },
    { "que horas sao na",     "what time is it in", PT_CUE },
    { "qual o signo de quem nasceu em", "what star sign is someone born on", PT_CUE },
    { "qual o signo de quem nasceu no dia", "what star sign is someone born on", PT_CUE },
    { "quantas horas de",     "how many hours from", PT_CUE },
    { "quantas horas sao de", "how many hours from", PT_CUE },
    { "logaritmo base",       "logarithm base", PT_CUE },
    { "logaritmo na base",    "logarithm base", PT_CUE },
    { "maximo divisor comum de", "greatest common divisor of", PT_CUE },
    { "mdc de",               "greatest common divisor of", PT_CUE },
    { "minimo multiplo comum de", "least common multiple of", PT_CUE },
    { "mmc de",               "least common multiple of", PT_CUE },
    { "fatorial de",          "factorial of", PT_CUE },
    { "e um numero primo",    "is prime", PT_CUE },
    { "e primo",              "is prime", PT_CUE },
    { "escreve",              "write", PT_CUE },
    { "escreva",              "write", PT_CUE },
    { "em romano",            "in roman numerals", PT_CUE },
    { "em algarismos romanos","in roman numerals", PT_CUE },
    { "em numeros romanos",   "in roman numerals", PT_CUE },
    { "em binario",           "to binary", PT_CUE },
    { "para binario",         "to binary", PT_CUE },
    { "em hexadecimal",       "to hex", PT_CUE },
    { "area de circulo com raio", "area of a circle radius", PT_CUE },
    { "area do circulo com raio", "area of a circle radius", PT_CUE },
    { "area do circulo de raio",  "area of a circle radius", PT_CUE },
    { "circunferencia de circulo com raio", "circumference of a circle radius", PT_CUE },
    { "circunferencia do circulo de raio",  "circumference of a circle radius", PT_CUE },
    { "area de retangulo",    "area of a rectangle", PT_CUE },
    { "area do retangulo",    "area of a rectangle", PT_CUE },
    { "area de triangulo base","area of a triangle base", PT_CUE },
    { "area do triangulo base","area of a triangle base", PT_CUE },
    { "altura",               "height", 0 },
    { "hipotenusa com catetos","hypotenuse with legs", PT_CUE },
    { "volume de cubo lado",  "volume of a cube side", PT_CUE },
    { "volume do cubo de lado","volume of a cube side", PT_CUE },
    { "volume de esfera raio","volume of a sphere radius", PT_CUE },
    { "volume da esfera de raio","volume of a sphere radius", PT_CUE },
    { "tensao com",           "voltage with", PT_CUE },
    { "potencia eletrica com","electrical power with", PT_CUE },
    { "velocidade",           "speed for", PT_CUE },
    { "quais meus eventos hoje", "agenda today", PT_CUE },
    { "quais sao meus eventos hoje", "agenda today", PT_CUE },
    { "meus eventos de hoje", "agenda today", PT_CUE },
    { "minha agenda",         "agenda today", PT_CUE },
    { "agenda de hoje",       "agenda today", PT_CUE },
    { "o que tenho hoje",     "agenda today", PT_CUE },
    { "meus compromissos",    "agenda today", PT_CUE },
    { "clima em",             "weather in", PT_CUE },
    { "clima amanha em",      "weather tomorrow in", PT_CUE },
    { "tempo em",             "weather in", PT_CUE },
    { "previsao do tempo em", "weather in", PT_CUE },
    { "previsao para amanha em", "weather tomorrow in", PT_CUE },
    // the word to translate is the user's text: copied as typed (the translator reads pt words and languages)
    { "traduz",               "translate", PT_CUE | PT_TEXT },
    { "traduza",              "translate", PT_CUE | PT_TEXT },
    { "traduzir",             "translate", PT_CUE | PT_TEXT },
    { "traducao de",          "translate", PT_CUE | PT_TEXT },
    { "como se diz",          "how do you say", PT_CUE | PT_TEXT },   // "como se diz obrigado em japones":
    { "como se fala",         "how do you say", PT_CUE | PT_TEXT },   // "obrigado" stays the word, not "thanks"
    { "como se escreve",      "how do you say", PT_CUE | PT_TEXT },
    { "como eu digo",         "how do you say", PT_CUE | PT_TEXT },
    // city names the world clock / weather know in English
    { "toquio",               "tokyo", 0 },
    { "londres",              "london", 0 },
    { "nova york",            "new york", 0 },
    { "nova iorque",          "new york", 0 },
    { "lisboa",               "lisbon", 0 },
    { "roma",                 "rome", 0 },
    { "pequim",               "beijing", 0 },
    { "moscou",               "moscow", 0 },
    { "berlim",               "berlin", 0 },
    { "madri",                "madrid", 0 },
    { "sao paulo",            "sao paulo", 0 },   // longest match: "sao" alone is not the verb here
    { "rio de janeiro",       "rio de janeiro", 0 },
    { "cidade do mexico",     "mexico city", 0 },
    { "buenos aires",         "buenos aires", 0 },
    // months (dates in templates: "25 de dezembro de 2026", "10 de marco", "fevereiro")
    { "janeiro", "january", 0 }, { "fevereiro", "february", 0 }, { "marco", "march", 0 }, { "abril", "april", 0 },
    { "maio", "may", 0 }, { "junho", "june", 0 }, { "julho", "july", 0 }, { "agosto", "august", 0 },
    { "setembro", "september", 0 }, { "outubro", "october", 0 }, { "novembro", "november", 0 }, { "dezembro", "december", 0 },
    { "quanto de memoria ram esta livre", "how much free ram", PT_CUE },
    { "qual a memoria livre", "how much free ram", PT_CUE },
    { "quanto de ram livre", "how much free ram", PT_CUE },
    { "quanta ram livre tenho", "how much free ram", PT_CUE },
    { "memoria livre", "free ram", PT_CUE },
    { "livre", "free", 0 },
    { "ha quanto tempo ligado", "what is your uptime", PT_CUE },
    { "ha quanto tempo esta ligado", "what is your uptime", PT_CUE },
    { "qual a minha rede", "am i connected", PT_CUE },   // the en cascade has no "what network am i on"
    { "qual minha rede", "am i connected", PT_CUE },
    { "em que rede estou", "am i connected", PT_CUE },
    { "estou conectado", "am i connected", PT_CUE },
    { "qual meu ip", "what is my ip", PT_CUE },
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
    { "quilometros",          "km", PT_UNIT },
    { "quilometro",           "km", PT_UNIT },
    { "metros",               "meters", PT_UNIT },
    { "metro",                "meter", PT_UNIT },
    { "centimetros",          "centimeters", PT_UNIT },
    { "milhas",               "miles", PT_UNIT },
    { "milha",                "mile", PT_UNIT },
    { "pes",                  "feet", PT_UNIT },
    { "polegadas",            "inches", PT_UNIT },
    { "quilos",               "kg", PT_UNIT },
    { "quilo",                "kg", PT_UNIT },
    { "gramas",               "grams", PT_UNIT },
    { "libras",               "pounds", PT_UNIT },
    { "litros",               "liters", PT_UNIT },
    { "litro",                "liter", PT_UNIT },
    { "galoes",               "gallons", PT_UNIT },
    { "graus celsius",        "celsius", PT_UNIT },
    { "graus fahrenheit",     "fahrenheit", PT_UNIT },
    { "celsius",              "celsius", PT_UNIT },
    { "fahrenheit",           "fahrenheit", PT_UNIT },
    { "km",                   "km", PT_UNIT },
    { "c",                    "c", PT_UNIT },   // "quantos F sao 30 C": counts only with a number and a 2nd unit
    { "f",                    "f", PT_UNIT },   // abbreviations: same word in English, flagged as units
    { "kg",                   "kg", PT_UNIT },
    { "cm",                   "cm", PT_UNIT },
    { "mm",                   "mm", PT_UNIT },
    { "mi",                   "miles", PT_UNIT },
    { "lb",                   "pounds", PT_UNIT },
    { "lbs",                  "pounds", PT_UNIT },
    { "oz",                   "ounces", PT_UNIT },
    { "onca",                 "ounce", PT_UNIT },
    { "oncas",                "ounces", PT_UNIT },
    { "ml",                   "ml", PT_UNIT },
    { "quantos quilometros sao","how many km is", PT_CUE },
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
    { "desconto",             "", 0 },   // "com 20% de desconto": "com" already became "minus"
    { "de desconto",          "", 0 },
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
    { "quantas",              "how many", 0 },
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

// Portuguese cardinal atoms; -1 means this is not a number.
static int pt_numword(const char *f)
{
    static const struct { const char *w; int v; } N[] = {
        {"um",1},{"uma",1},{"dois",2},{"duas",2},{"tres",3},{"quatro",4},{"cinco",5},{"seis",6},{"sete",7},
        {"oito",8},{"nove",9},{"dez",10},{"onze",11},{"doze",12},{"treze",13},{"quatorze",14},{"catorze",14},
        {"quinze",15},{"dezesseis",16},{"dezessete",17},{"dezoito",18},{"dezenove",19},{"vinte",20},{"trinta",30},
        {"quarenta",40},{"cinquenta",50},{"sessenta",60},{"setenta",70},{"oitenta",80},{"noventa",90},
        {"zero",0},{"cem",100},{"cento",100},{"duzentos",200},{"trezentos",300},{"quatrocentos",400},
        {"quinhentos",500},{"seiscentos",600},{"setecentos",700},{"oitocentos",800},{"novecentos",900} };
    for (size_t i = 0; i < sizeof N / sizeof N[0]; i++) if (!strcmp(N[i].w, f)) return N[i].v;
    return -1;
}
static bool pt_isnum(const pt_tok_t *t) { return isdigit((unsigned char)t->f[0]) || pt_numword(t->f) >= 0; }

// Consume a whole cardinal before interpreting its unit. Treating only the last word of
// "vinte e cinco minutos" as a number silently scheduled FIVE minutes instead of 25.
static int pt_number(const pt_tok_t *w, int n, int i, int *used)
{
    *used = 1;
    int value = pt_numword(w[i].f);
    if (value < 0) return -1;
    int last = value;
    while (i + *used + 1 < n && !strcmp(w[i + *used].f, "e")) {
        int next = pt_numword(w[i + *used + 1].f);
        if (next <= 0 || !((last >= 100 && next < 100) || (last >= 20 && last < 100 && next < 10))) break;
        value += next; last = next; *used += 2;
    }
    return value;
}

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
    // between two numbers: "de 9 a 17" -> "9 to 17", "4 por 5" -> "4 by 5", "catetos 3 e 4" -> "3 and 4"
    if (prev_num && next_num) {
        if (!strcmp(f, "a") || !strcmp(f, "ate")) return "to";
        if (!strcmp(f, "por")) return "by";
        if (!strcmp(f, "e") && isdigit((unsigned char)w[i - 1].f[0]) && isdigit((unsigned char)w[i + 1].f[0])) return "and";
    }
    // "10 ohm e 2 amp": "e" between a measured value and the next number
    if (!strcmp(f, "e") && i >= 2 && next_num && isdigit((unsigned char)w[i + 1].f[0]) && isdigit((unsigned char)w[i - 2].f[0]) &&
        !pt_isnum(&w[i - 1])) return "and";
    // "quantos km sao 13 milhas": the verb before a number ("Sao Paulo" stays a name)
    if (!strcmp(f, "sao") && next_num) return "is";
    // "80 euros com 20% (de) desconto" -> "80 euros minus 20%" (the en calculator's discount phrasing)
    if (!strcmp(f, "com") && next_num && i + 2 < n &&
        (!strcmp(w[i + 2].f, "desconto") || (i + 3 < n && !strcmp(w[i + 2].f, "de") && !strcmp(w[i + 3].f, "desconto")))) return "minus";
    // "22% (de) IVA (de|sobre) 100 euros" -> "100 plus 22%": the en calculator adds a percentage this way
    if (isdigit((unsigned char)f[0]) && strchr(f, '%')) {
        int k = i + 1;
        if (k < n && !strcmp(w[k].f, "de")) k++;
        if (k < n && (!strcmp(w[k].f, "iva") || !strcmp(w[k].f, "imposto"))) {
            k++;
            if (k < n && (!strcmp(w[k].f, "de") || !strcmp(w[k].f, "sobre") || !strcmp(w[k].f, "em"))) k++;
            if (k < n && isdigit((unsigned char)w[k].f[0])) {
                snprintf(num, numcap, "%s plus %s", w[k].f, f); *used = k + 1 - i; return num;
            }
        }
    }
    // "as 7" / "para as 7" / "ate as 7" -> "at 7"
    if ((!strcmp(f, "para") || !strcmp(f, "ate")) && i + 2 < n && !strcmp(w[i + 1].f, "as") && pt_isnum(&w[i + 2])) { *used = 2; return "at"; }
    if ((!strcmp(f, "as") || !strcmp(f, "a")) && next_num && !(i + 2 < n && pt_unit(w[i + 2].f))) return "at";
    // "daqui a 5 minutos" / "em 5 minutos" -> "in 5 minutes"; "de/por/durante 5 minutos" -> "for 5 minutes"
    int next_used = 1;
    if (next_num) pt_number(w, n, i + 1, &next_used);
    bool num_unit = next_num && i + 1 + next_used < n && pt_unit(w[i + 1 + next_used].f);
    if (!strcmp(f, "daqui") && i + 1 < n && !strcmp(w[i + 1].f, "a")) { *used = 2; return "in"; }
    if (!strcmp(f, "dentro") && i + 1 < n && !strcmp(w[i + 1].f, "de")) { *used = 2; return "in"; }
    if (num_unit && !strcmp(f, "em")) return "in";
    if (num_unit && (!strcmp(f, "de") || !strcmp(f, "por") || !strcmp(f, "durante"))) return "for";
    // a number word before a unit or next to an operator -> digits ("cinco minutos" -> "5 minutes")
    int number_used = 1;
    int v = pt_number(w, n, i, &number_used);
    int after = i + number_used;
    if (v >= 0 && ((after < n && (pt_unit(w[after].f) || !strcmp(w[after].f, "vezes") || !strcmp(w[after].f, "mais") ||
                              !strcmp(w[after].f, "menos"))) || (i > 0 && !strcmp(w[i - 1].f, "as")))) {
        *used = number_used; snprintf(num, numcap, "%d", v); return num;
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
    char num[32];   // a context rewrite can be "100 plus 22%"
    if (!in || !out || cap < 2) return false;
    int nw = pt_split(in, w);
    size_t n = 0; bool cue = false, text = false, literal_text = false, has_num = false; int units = 0;
    for (int i = 0; i < nw; ) {
        // A note/file body is user data, including numbers, punctuation and
        // spacing. Only reminder frames continue parsing a date/time suffix.
        if (literal_text) {
            const char *tail = w[i].raw;
            size_t len = strlen(tail);
            if (n && n + 1 < cap) out[n++] = ' ';
            if (len >= cap - n) len = cap - n - 1;
            memcpy(out + n, tail, len); n += len;
            break;
        }
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
                if ((best->flags & PT_UNIT) && !text) units++;
                if (best->flags & PT_TEXT) {
                    text = true;
                    literal_text = strncmp(best->en, "remind me", 9) != 0;
                }
            } else if (text) { e = w[i].raw; el = w[i].rawlen; }        // as typed, accents kept
            else             { e = w[i].f;   el = strlen(w[i].f); }
        }
        for (int k = i; k < i + used && k < nw; k++) if (pt_isnum(&w[k])) has_num = true;
        i += used;
        if (el == 0) continue;                                           // deleted glue ("por favor")
        if (n && n + 1 < cap) out[n++] = ' ';
        if (n + el >= cap) el = cap - 1 - n;
        memcpy(out + n, e, el); n += el;
    }
    out[n] = 0;
    return cue || (has_num && units >= 2);                               // "13 milhas em km": a conversion
}

// Small, reviewed Portuguese seed corpus. Exact complete questions only: an
// unknown subject must never borrow a nearby fact. The separate Portuguese SD
// pack broadens coverage; these seeds still need no SD reads, heap or static RAM.
static const struct { const char *q, *en, *pt; } PT_KNOWLEDGE[] = {
    { "qual a capital do brasil", "Brazil's capital is Brasilia.", "A capital do Brasil e Brasilia." },
    { "qual e a capital do brasil", "Brazil's capital is Brasilia.", "A capital do Brasil e Brasilia." },
    { "qual a capital da franca", "France's capital is Paris.", "A capital da Franca e Paris." },
    { "qual a capital do japao", "Japan's capital is Tokyo.", "A capital do Japao e Toquio." },
    { "quem foi santos dumont", "Santos Dumont was a Brazilian aviation pioneer.", "Santos Dumont foi um pioneiro brasileiro da aviacao." },
    { "quem e santos dumont", "Santos Dumont was a Brazilian aviation pioneer.", "Santos Dumont foi um pioneiro brasileiro da aviacao." },
    { "o que e nucleoos", "NucleoOS runs offline on the M5Stack Cardputer.", "NucleoOS funciona offline no M5Stack Cardputer." },
    { "o que e ram", "RAM holds data and programs while a device runs; its contents are lost when power is removed.", "A RAM guarda dados e programas em uso; seu conteudo se perde quando a energia e desligada." },
    { "o que e psram", "PSRAM is external RAM that manages its own refresh.", "PSRAM e uma memoria RAM externa que gerencia sua propria atualizacao." },
    { "o cardputer tem psram", "The Cardputer has no PSRAM.", "O Cardputer nao tem PSRAM." },
    { "o que e flash", "Flash is memory that keeps its contents without power.", "Flash e uma memoria que preserva os dados sem energia." },
    { "o que e um microcontrolador", "A microcontroller combines a processor, memory and peripherals on one chip.", "Um microcontrolador reune processador, memoria e perifericos em um chip." },
    { "o que e dns", "DNS maps domain names to network addresses.", "O DNS associa nomes de dominio a enderecos de rede." },
    { "o que e http", "HTTP is a protocol for exchanging resources between clients and servers.", "HTTP e um protocolo de troca de recursos entre clientes e servidores." },
    { "o que e html", "HTML describes the structure of web pages.", "HTML descreve a estrutura das paginas da web." },
    { "o que e css", "CSS describes how web pages are presented.", "CSS define a apresentacao visual das paginas da web." },
    { "o que e json", "JSON is a text format for exchanging structured data.", "JSON e um formato de texto para trocar dados estruturados." },
    { "o que e um led", "An LED is a semiconductor diode that emits light.", "Um LED e um diodo semicondutor que emite luz." },
    { "o que e fotossintese", "Photosynthesis uses light energy to produce organic compounds from carbon dioxide and water.", "A fotossintese usa energia da luz para produzir compostos organicos a partir de dioxido de carbono e agua." },
    { "o que e dna", "DNA stores genetic information in living organisms.", "O DNA armazena informacoes geneticas nos seres vivos." },
};

bool a_pt_smalltalk(const char *in, anima_result_t *out)
{
    pt_tok_t w[PT_MAXW];
    int n = pt_split(in, w);
    // Whole-line small talk. The replies are English rows of the reply catalog (the done: epilogue localizes them).
    // greet=1 marks a greeting: "oi, tudo bem?" = a greeting followed by another row -> that row answers.
    static const struct { const char *q, *reply; unsigned char greet; } rows[] = {
        { "oi", "Hello!", 1 }, { "ola", "Hello!", 1 }, { "e ai", "Hello!", 1 }, { "opa", "Hello!", 1 },
        { "bom dia", "Good morning!", 1 }, { "boa tarde", "Good afternoon!", 1 }, { "boa noite", "Good evening!", 1 },
        { "tudo bem", "I'm well, ready to help.", 0 }, { "tudo bom", "I'm well, ready to help.", 0 },
        { "tudo certo", "I'm well, ready to help.", 0 }, { "beleza", "I'm well, ready to help.", 0 },
        { "como vai", "I'm well, ready to help.", 0 }, { "como vai voce", "I'm well, ready to help.", 0 },
        { "como voce esta", "I'm well, ready to help.", 0 }, { "como esta", "I'm well, ready to help.", 0 },
        { "como voce vai", "I'm well, ready to help.", 0 }, { "e voce", "I'm well, ready to help.", 0 },
        { "tudo bem com voce", "I'm well, ready to help.", 0 },
        { "tchau", "See you soon!", 0 }, { "ate logo", "See you soon!", 0 }, { "ate mais", "See you soon!", 0 },
        { "ate amanha", "See you soon!", 0 }, { "ate depois", "See you soon!", 0 }, { "falou", "See you soon!", 0 },
        { "qual o seu nome", "I'm ANIMA, NucleoOS's offline assistant.", 0 },
        { "qual e o seu nome", "I'm ANIMA, NucleoOS's offline assistant.", 0 },
        { "qual seu nome", "I'm ANIMA, NucleoOS's offline assistant.", 0 },
        { "como voce se chama", "I'm ANIMA, NucleoOS's offline assistant.", 0 },
        { "como te chamas", "I'm ANIMA, NucleoOS's offline assistant.", 0 },
        { "desculpa", "No problem!", 0 }, { "desculpe", "No problem!", 0 }, { "foi mal", "No problem!", 0 },
        { "ok", "Okay!", 0 }, { "certo", "Okay!", 0 }, { "legal", "Okay!", 0 }, { "entendi", "Okay!", 0 },
        { "show", "Okay!", 0 }, { "perfeito", "Okay!", 0 },
        { "voce e incrivel", "Thank you!", 0 }, { "bom trabalho", "Thank you!", 0 }, { "muito bem", "Thank you!", 0 },
        { "parabens", "Thank you!", 0 }, { "voce e otima", "Thank you!", 0 }, { "voce e demais", "Thank you!", 0 },
        { "de nada", "Anytime!", 0 }, { "por nada", "Anytime!", 0 },
    };
    // a leading greeting followed by another row ("oi tudo bem", "bom dia, como vai"): the second row answers
    int start = 0;
    for (size_t i = 0; i < sizeof rows / sizeof rows[0] && !start; i++) {
        if (!rows[i].greet) continue;
        int u = pt_match_words(w, n, 0, rows[i].q);
        if (u > 0 && u < n) start = u;
    }
    for (int pass = 0; pass < 2; pass++) {
        int from = pass ? 0 : start;                  // with a greeting prefix, try the remainder first
        if (pass == 0 && !start) continue;
        for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++) {
            if (n - from < 1 || pt_match_words(w, n, from, rows[i].q) != n - from) continue;
            memset(out, 0, sizeof *out);
            out->tier = ANIMA_TIER_COMMAND; out->action = ANIMA_ACT_ANSWER; out->confidence = 100;
            snprintf(out->intent, sizeof out->intent, "%s", rows[i].greet ? "greeting" : "smalltalk");
            snprintf(out->state, sizeof out->state, "idle");
            snprintf(out->reply, sizeof out->reply, "%s", rows[i].reply);
            return true;
        }
    }
    for (size_t i = 0; i < sizeof PT_KNOWLEDGE / sizeof PT_KNOWLEDGE[0]; i++) {
        if (n < 1 || pt_match_words(w, n, 0, PT_KNOWLEDGE[i].q) != n) continue;
        memset(out, 0, sizeof *out);
        out->tier = ANIMA_TIER_FACT; out->action = ANIMA_ACT_ANSWER; out->confidence = 100;
        snprintf(out->intent, sizeof out->intent, "knowledge");
        snprintf(out->state, sizeof out->state, "idle");
        snprintf(out->reply, sizeof out->reply, "%s", PT_KNOWLEDGE[i].en);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------------------------------------
// 2. Reply catalog
// ---------------------------------------------------------------------------------------------------------
// en/pt templates: "%s" in en is a wildcard capture (up to 5, non-empty); "%1".."%5" in pt is that capture,
// localized through PT_TERMS (app ids, weekdays, months, units...). Every other byte is literal.
// intent: NULL = any; else the entry applies only to that intent (the same EN text can need two translations).
typedef struct { const char *intent; const char *en; const char *pt; } a_pt_reply_t;

static const a_pt_reply_t PT_REPLIES[] = {
    { NULL, "Hello!", "Ola!" },
    { NULL, "Good morning!", "Bom dia!" },
    { NULL, "Good afternoon!", "Boa tarde!" },
    { NULL, "Good evening!", "Boa noite!" },
    { NULL, "You're welcome!", "De nada!" },
    { "calc", "sqrt(%s) = %s.", "A raiz quadrada de %1 e %2." },
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
    // small talk (a_pt_smalltalk) and the en L1 small-talk replies
    { NULL,       "I'm well, ready to help.",        "Tudo bem! Como posso ajudar?" },
    { NULL,       "See you soon!",                   "Ate logo!" },
    { NULL,       "I'm ANIMA, NucleoOS's offline assistant.", "Sou a ANIMA, a assistente offline do NucleoOS." },
    { NULL,       "No problem!",                     "Sem problema!" },
    { NULL,       "Okay!",                           "Certo!" },
    { NULL,       "Thank you!",                      "Obrigada!" },
    { NULL,       "Anytime!",                        "Disponha!" },
    // math, number theory, geometry, physics (the en solver's formats; symbols and numbers stay)
    { "calc",     "%s^%s = %s.",                     "%1 elevado a %2 = %3." },
    { "calc",     "log base %s of %s = %s.",         "Logaritmo na base %1 de %2 = %3." },
    { "calc",     "gcd(%s, %s) = %s.",               "MDC de %1 e %2 = %3." },
    { "calc",     "lcm(%s, %s) = %s.",               "MMC de %1 e %2 = %3." },
    { "calc",     "%s! = %s.",                       "Fatorial de %1 = %2." },
    { "prime",    "Is %s prime? Yes.",               "%1 e primo? Sim." },
    { "prime",    "Is %s prime? No.",                "%1 e primo? Nao." },
    { "prime",    "Is %s prime? No (%s).",           "%1 e primo? Nao (%2)." },
    { "roman",    "%s in roman numerals = %s.",      "%1 em algarismos romanos = %2." },
    { "base",     "%s in base %s = %s.",             "%1 na base %2 = %3." },
    { "geo",      "Circle area = %s",                "Area do circulo = %1" },
    { "geo",      "Circumference = %s",              "Circunferencia = %1" },
    { "geo",      "Rectangle area = %s",             "Area do retangulo = %1" },
    { "geo",      "Triangle area = %s",              "Area do triangulo = %1" },
    { "geo",      "Hypotenuse = %s",                 "Hipotenusa = %1" },
    { "geo",      "Cube volume = %s",                "Volume do cubo = %1" },
    { "geo",      "Sphere volume = %s",              "Volume da esfera = %1" },
    { "ohm",      "V=%s",                            "V=%1" },
    { "phys",     "v = %s",                          "v = %1" },
    // calendar skills
    { "date",     "%s %s has %s days.",              "Em %1 de %2 ha %3 dias." },
    { "date",     "%s has %s days.",                 "%1 tem %2 dias." },
    { "date",     "Someone born in %s is %s or %s in %s (depending on the month).",
                  "Quem nasceu em %1 tem %2 ou %3 anos em %4 (depende do mes)." },
    { "date",     "Someone born on %s %s is a %s.",  "Quem nasceu em %2 de %1 e de %3." },
    { "date",     "Someone born on %s %s is an %s.", "Quem nasceu em %2 de %1 e de %3." },
    { "date",     "From %s to %s: %s hours.",        "De %1 a %2: %3 horas." },
    { "date",     "From %s to %s: %s.",              "De %1 a %2: %3." },
    { "worldclock", "In %s it's %s.",                "Em %1 sao %2." },
    // translator (IT<->EN dictionary only)
    { "translate", "I don't have \"%s\" in the offline IT<->EN dictionary.",
                   "Nao tenho \"%1\" no dicionario offline italiano-ingles." },
    { "translate", "\"%s\" in English: %s.",       "\"%1\" em ingles: %2." },
    { "translate", "\"%s\" in Italian: %s.",       "\"%1\" em italiano: %2." },
    { "translate", "\"%s\" in Portuguese: %s.",    "\"%1\" em portugues: %2." },
    { "translate", "\"%s\" in Japanese: %s.",      "\"%1\" em japones: %2." },
    { "translate", "I don't have \"%s\" in the offline dictionaries.",
                   "Nao tenho \"%1\" nos dicionarios offline." },
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
    { "Aries", "Aries" }, { "Taurus", "Touro" }, { "Gemini", "Gemeos" }, { "Cancer", "Cancer" }, { "Leo", "Leao" },
    { "Virgo", "Virgem" }, { "Libra", "Libra" }, { "Scorpio", "Escorpiao" }, { "Sagittarius", "Sagitario" },
    { "Capricorn", "Capricornio" }, { "Aquarius", "Aquario" }, { "Pisces", "Peixes" },
    { "Tokyo", "Toquio" }, { "London", "Londres" }, { "New York", "Nova York" }, { "Lisbon", "Lisboa" }, { "Rome", "Roma" },
    { "Beijing", "Pequim" }, { "Moscow", "Moscou" }, { "Berlin", "Berlim" }, { "Mexico City", "Cidade do Mexico" },
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
char g_anima_pt_bridged[256];  // host harness only: the pt line after the bridge (pt-check / debugging)
#endif

bool a_pt_reply(const char *intent, char *reply, size_t cap)
{
#ifdef ANIMA_HOST
    g_anima_pt_miss = reply && reply[0];
#endif
    if (intent && !strcmp(intent, "pt_knowledge")) {
#ifdef ANIMA_HOST
        g_anima_pt_miss = false;
#endif
        return true;
    }
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
    for (size_t i = 0; i < sizeof PT_KNOWLEDGE / sizeof PT_KNOWLEDGE[0]; i++) {
        if (strcmp(reply, PT_KNOWLEDGE[i].en)) continue;
        snprintf(reply, cap, "%s", PT_KNOWLEDGE[i].pt);
#ifdef ANIMA_HOST
        g_anima_pt_miss = false;
#endif
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
