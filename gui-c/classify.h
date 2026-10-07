#ifndef CLASSIFY_H
#define CLASSIFY_H
#include <wchar.h>
const wchar_t *classify_maker(const wchar_t *title, const wchar_t *bios);   /* "Capcom", "Namco", ... or "Other" */
const wchar_t *classify_genre(const wchar_t *title);                       /* "Fighting", "Shooter", "Racing", "Sports", "Puzzle" or "Other" */
const wchar_t *classify_region(const wchar_t *title);   /* from the words between ( ) in the title: "US", "JP", "ASIA", "WORLD" or "Other" */
#endif
