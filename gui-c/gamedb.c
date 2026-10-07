/* the hardware and the year of the games (the games of ZiNc 1.1, by set name); a game that is not in the table has neither */
#include "common.h"

static const struct { const wchar_t *set, *hw; int year; } gt[] = {
    {L"danceyes", L"Namco System 11", 1996},
    {L"glpracr", L"Sony ZN-1 System", 1996},
    {L"glpracr2", L"Tecmo TPS", 1997},
    {L"glpracr3", L"Tecmo TPS", 2000},
    {L"shngmtkb", L"Tecmo TPS", 1998},
    {L"mfjump", L"Tecmo TPS", 1999},
    {L"cbaj", L"Tecmo TPS", 1998},
    {L"primglex", L"Namco System 11", 1996},
    {L"xevi3dg", L"Namco System 11", 1996},
    {L"souledga", L"Namco System 11", 1995},
    {L"souledgb", L"Namco System 11", 1995},
    {L"souledge", L"Namco System 11", 1995},
    {L"tekkenb", L"Namco System 11", 1994},
    {L"tekkena", L"Namco System 11", 1994},
    {L"tekken", L"Namco System 11", 1994},
    {L"starswep", L"Namco System 11", 1997},
    {L"tekken2a", L"Namco System 11", 1995},
    {L"tekken2b", L"Namco System 11", 1995},
    {L"tekken2", L"Namco System 11", 1995},
    {L"myangel3", L"Namco System 11", 1998},
    {L"tgmj", L"Sony ZN-2 System", 1998},
    {L"starglad", L"Sony ZN-1 System", 1996},
    {L"sfexj", L"Sony ZN-1 System", 1996},
    {L"sfexa", L"Sony ZN-1 System", 1996},
    {L"sfex", L"Sony ZN-1 System", 1996},
    {L"sfexp", L"Sony ZN-1 System", 1997},
    {L"sfexpu1", L"Sony ZN-1 System", 1997},
    {L"sfexpj", L"Sony ZN-1 System", 1997},
    {L"ts2j", L"Sony ZN-1 System", 1995},
    {L"ts2", L"Sony ZN-1 System", 1995},
    {L"stargld2", L"Sony ZN-2 System", 1998},
    {L"plsmaswd", L"Sony ZN-2 System", 1998},
    {L"sfex2", L"Sony ZN-2 System", 1998},
    {L"sfex2j", L"Sony ZN-2 System", 1998},
    {L"sfex2pj", L"Sony ZN-2 System", 1999},
    {L"sfex2pa", L"Sony ZN-2 System", 1999},
    {L"sfex2p", L"Sony ZN-2 System", 1999},
    {L"shiryu2", L"Sony ZN-2 System", 1999},
    {L"strider2", L"Sony ZN-2 System", 1999},
    {L"kikaioh", L"Sony ZN-2 System", 1998},
    {L"techromn", L"Sony ZN-2 System", 1998},
    {L"rvschool", L"Sony ZN-2 System", 1997},
    {L"rvschola", L"Sony ZN-2 System", 1997},
    {L"jgakuen", L"Sony ZN-2 System", 1997},
    {L"sncwgltd", L"Video System PSX", 1996},
    {L"beastrzb", L"Raizing/Eighting PS Arcade 95", 0},
    {L"beastrzr", L"Raizing/Eighting PS Arcade 95", 1997},
    {L"bldyror2", L"Raizing/Eighting PS Arcade 95", 1998},
    {L"brvblade", L"Tecmo TPS", 1998},
    {L"psyforcj", L"Taito FX-1A", 1996},
    {L"psyforce", L"Taito FX-1A", 1996},
    {L"psyfrcex", L"Taito FX-1A", 1996},
    {L"mgcldate", L"Taito FX-1A", 1998},
    {L"mgcldtex", L"Taito FX-1A", 1998},
    {L"sfchamp", L"Taito FX-1A", 1997},
    {L"gdarius", L"Taito FX-1B", 1997},
    {L"gdarius2", L"Taito FX-1B", 1997},
    {L"sws99", L"Namco System 12", 1999},
    {L"pacapp", L"Namco System 12", 1999},
    {L"aquarush", L"Namco System 12", 1999},
    {L"tekken3", L"Namco System 12", 1996},
    {L"mdhorse", L"Namco System 12", 1998},
    {L"fgtlayer", L"Namco System 12", 1998},
    {L"ehrgeiz", L"Namco System 12", 1998},
    {L"mrdrillr", L"Namco System 12", 1999},
    {L"raystorm", L"Taito FX-1B", 1996},
    {L"raystorj", L"Taito FX-1B", 1996},
    {L"ftimpcta", L"Taito FX-1B", 1996},
    {L"weddingr", L"Konami GV", 1997},
    {L"hyperath", L"Konami GV", 1996},
    {L"btchamp", L"Konami GV", 1996},
    {L"pbball96", L"Konami GV", 1996},
    {L"susume", L"Konami GV", 1996},
    {L"dunkmnic", L"Namco System 11", 1996},
    {L"dunkmnia", L"Namco System 11", 1996},
    {NULL, NULL, 0}};

const wchar_t *game_hardware(const Game *g) {
    int i;
    for (i = 0; gt[i].set; i++) if (!wcscmp(gt[i].set, g->set)) return gt[i].hw;
    return L"";
}

/* the year: from the date in the title (YYMMDD) when it has one, else from the table */
int game_year(const Game *g) {
    int i;
    const wchar_t *p = wcschr(g->title, L'(');
    while (p) {
        int k = 0;
        const wchar_t *q = p + 1;
        while (k < 6 && iswdigit(q[k])) k++;
        if (k == 6 && q[6] == L')') { int yy = (q[0] - L'0') * 10 + (q[1] - L'0'); return yy >= 90 ? 1900 + yy : 2000 + yy; }
        p = wcschr(p + 1, L'(');
    }
    for (i = 0; gt[i].set; i++) if (!wcscmp(gt[i].set, g->set)) return gt[i].year;
    return 0;
}
