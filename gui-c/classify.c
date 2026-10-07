/* Guesses the maker and the genre of a game from its title and BIOS name (ZiNc's list does not tell them), for the list filters.
   No Windows code: testable on any host. */
#include <wchar.h>
#include <wctype.h>
#include <string.h>
#include "classify.h"

static int has(const wchar_t *lowTitle, const wchar_t *word) { return wcsstr(lowTitle, word) != NULL; }

static void lower(const wchar_t *s, wchar_t *out, int cap) {
    int i;
    for (i = 0; s[i] && i < cap - 1; i++) out[i] = (wchar_t)towlower((wint_t)s[i]);
    out[i] = 0;
}

typedef struct { const wchar_t *word, *label; } Rule;

static const wchar_t *first_match(const wchar_t *low, const Rule *r) {
    for (; r->word; r++) if (has(low, r->word)) return r->label;
    return NULL;
}

const wchar_t *classify_maker(const wchar_t *title, const wchar_t *bios) {
    static const Rule byTitle[] = {
        {L"tekken", L"Namco"}, {L"soul edge", L"Namco"}, {L"xevious", L"Namco"}, {L"libero grande", L"Namco"}, {L"point blank", L"Namco"},
        {L"prop cycle", L"Namco"}, {L"dunk mania", L"Namco"}, {L"pocket racer", L"Namco"}, {L"star sweep", L"Namco"}, {L"starswep", L"Namco"},
        {L"street fighter", L"Capcom"}, {L"rival schools", L"Capcom"}, {L"star gladiator", L"Capcom"}, {L"plasma sword", L"Capcom"},
        {L"marvel", L"Capcom"}, {L"x-men", L"Capcom"}, {L"strider", L"Capcom"}, {L"darkstalkers", L"Capcom"}, {L"vampire", L"Capcom"},
        {L"capcom", L"Capcom"}, {L"sfex", L"Capcom"}, {L"battle circuit", L"Capcom"}, {L"jojo", L"Capcom"},
        {L"psychic force", L"Taito"}, {L"g-darius", L"Taito"}, {L"raystorm", L"Taito"}, {L"ray storm", L"Taito"}, {L"gaia crusaders", L"Taito"},
        {L"toshinden", L"Takara"}, {L"bloody roar", L"Hudson"}, {L"beastorizer", L"Raizing"}, {L"brave blade", L"Tecmo"},
        {L"toukon", L"Tecmo"}, {L"dead or alive", L"Tecmo"}, {NULL, NULL}};
    wchar_t t[300], b[80];
    const wchar_t *m;
    lower(title, t, 300);
    lower(bios, b, 80);
    if ((m = first_match(t, byTitle))) return m;
    if (has(b, L"cpzn")) return L"Capcom";
    if (has(b, L"taito")) return L"Taito";
    if (has(b, L"tps")) return L"Tecmo";
    if (has(b, L"atpsx")) return L"Atari";
    if (has(b, L"acpsx")) return L"Acclaim";
    return L"Other";
}

const wchar_t *classify_genre(const wchar_t *title) {
    static const Rule rules[] = {
        {L"tekken", L"Fighting"}, {L"street fighter", L"Fighting"}, {L"sfex", L"Fighting"}, {L"rival schools", L"Fighting"},
        {L"star gladiator", L"Fighting"}, {L"plasma sword", L"Fighting"}, {L"marvel", L"Fighting"}, {L"x-men", L"Fighting"},
        {L"soul edge", L"Fighting"}, {L"toshinden", L"Fighting"}, {L"bloody roar", L"Fighting"}, {L"beastorizer", L"Fighting"},
        {L"psychic force", L"Fighting"}, {L"darkstalkers", L"Fighting"}, {L"vampire", L"Fighting"}, {L"capcom vs", L"Fighting"},
        {L"battle arena", L"Fighting"}, {L"brave blade", L"Fighting"}, {L"toukon", L"Fighting"}, {L"dead or alive", L"Fighting"},
        {L"fighting", L"Fighting"}, {L"primal rage", L"Fighting"}, {L"last bronx", L"Fighting"}, {L"fatal", L"Fighting"},
        {L"strider", L"Action"},
        {L"gradius", L"Shooter"}, {L"darius", L"Shooter"}, {L"raystorm", L"Shooter"}, {L"ray storm", L"Shooter"}, {L"striker", L"Shooter"},
        {L"1945", L"Shooter"}, {L"gunbarich", L"Shooter"}, {L"shoot", L"Shooter"}, {L"xevious", L"Shooter"}, {L"point blank", L"Shooter"},
        {L"gaia crusaders", L"Shooter"}, {L"star sweep", L"Shooter"}, {L"starswep", L"Shooter"},
        {L"racing", L"Racing"}, {L"racer", L"Racing"}, {L"drive", L"Racing"}, {L"rally", L"Racing"}, {L"kart", L"Racing"},
        {L"libero grande", L"Sports"}, {L"dunk", L"Sports"}, {L"soccer", L"Sports"}, {L"football", L"Sports"}, {L"baseball", L"Sports"},
        {L"golf", L"Sports"}, {L"tennis", L"Sports"}, {L"olympic", L"Sports"},
        {L"bust a move", L"Puzzle"}, {L"bust-a-move", L"Puzzle"}, {L"puzzle", L"Puzzle"}, {L"tetris", L"Puzzle"}, {L"mahjong", L"Puzzle"},
        {L"bubble", L"Puzzle"}, {L"gals panic", L"Puzzle"}, {L"magical", L"Puzzle"},
        {NULL, NULL}};
    wchar_t t[300];
    const wchar_t *g;
    lower(title, t, 300);
    if ((g = first_match(t, rules))) return g;
    return L"Other";
}

/* the region from the words between ( ) in the title, e.g. "Tekken 3 (US, TET2/VER.A)": US, JP, ASIA, WORLD, or Other when none says it.
   Whole words only ("us" is not found inside "plus" or "austria"); the first region word of the first group that has one wins. */
const wchar_t *classify_region(const wchar_t *title) {
    static const Rule words[] = {
        {L"us", L"US"}, {L"usa", L"US"}, {L"jp", L"JP"}, {L"jpn", L"JP"}, {L"japan", L"JP"},
        {L"asia", L"ASIA"}, {L"world", L"WORLD"}, {NULL, NULL}};
    const wchar_t *p = title ? title : L"";
    while ((p = wcschr(p, L'(')) != NULL) {
        wchar_t w[16];
        int n = 0;
        for (p++; *p && *p != L')'; p++) {
            if (iswalnum((wint_t)*p)) { if (n < 15) w[n++] = (wchar_t)towlower((wint_t)*p); }
            else if (n) { const Rule *r; w[n] = 0; for (r = words; r->word; r++) if (!wcscmp(w, r->word)) return r->label; n = 0; }
        }
        if (n) { const Rule *r; w[n] = 0; for (r = words; r->word; r++) if (!wcscmp(w, r->word)) return r->label; }
        if (!*p) break;
    }
    return L"Other";
}
