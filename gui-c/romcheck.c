/* Game > Check ROM Set: looks at the zip files of a game (the game, its parent set and the BIOS) and says what is missing or wrong:
   missing files, damaged zips, wrong checksums. The check runs in a thread so the window stays usable on big sets. */
#include "common.h"
#include "zipio.h"

#define WM_RC_DONE (WM_APP + 10)

typedef struct {
    HWND dlg;
    wchar_t title[300];
    wchar_t dir[560];
    wchar_t names[3][64];
    const wchar_t *role[3];
    int n;
} Job;

static void add(Buf *b, const wchar_t *w) { char *u = w_to_u8(w); buf_add(b, u); free(u); }
static void addf(Buf *b, const wchar_t *fmt, ...) {
    wchar_t tmp[1200];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(tmp, 1200, fmt, ap);
    va_end(ap);
    tmp[1199] = 0;
    add(b, tmp);
}

static void size_text(unsigned long v, wchar_t *out, int cap) {
    if (v >= 1024UL * 1024) swprintf(out, cap, L"%.1f MB", v / 1048576.0);
    else swprintf(out, cap, L"%lu KB", (v + 1023) / 1024);
}

static DWORD WINAPI check_thread(LPVOID p) {
    Job *j = (Job *)p;
    Buf b = {0};
    int i, bad = 0, missing = 0;
    wchar_t *text;
    addf(&b, L"%ls\r\nROMs folder: %ls\r\n\r\n", j->title, j->dir);
    for (i = 0; i < j->n; i++) {
        wchar_t path[700], sz[40];
        size_t len = 0;
        unsigned char *data;
        ZipReport rep;
        DWORD attr;
        int k;
        _snwprintf(path, 700, L"%ls\\%ls.zip", j->dir, j->names[i]);
        path[699] = 0;
        attr = GetFileAttributesW(path);
        if (attr == INVALID_FILE_ATTRIBUTES) {   /* an unpacked folder? */
            _snwprintf(path, 700, L"%ls\\%ls", j->dir, j->names[i]);
            attr = GetFileAttributesW(path);
            if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) { addf(&b, L"%ls (%ls): a folder, its files are not checked\r\n", j->names[i], j->role[i]); continue; }
            addf(&b, L"%ls.zip (%ls): MISSING - put %ls.zip into the ROMs folder\r\n", j->names[i], j->role[i], j->names[i]);
            missing++;
            continue;
        }
        _snwprintf(path, 700, L"%ls\\%ls.zip", j->dir, j->names[i]);
        data = (unsigned char *)read_file(path, &len);
        if (!data) { addf(&b, L"%ls.zip (%ls): found, but it cannot be read (in use, or no access?)\r\n", j->names[i], j->role[i]); bad++; continue; }
        if (zip_verify(data, len, &rep)) {
            size_text(rep.unpacked, sz, 40);
            addf(&b, L"%ls.zip (%ls): OK - %d files, %ls\r\n", j->names[i], j->role[i], rep.entries, sz);
        } else {
            addf(&b, L"%ls.zip (%ls): PROBLEM\r\n", j->names[i], j->role[i]);
            for (k = 0; k < rep.nproblems && k < 12; k++) { wchar_t *w = u8_to_w(rep.problems[k], -1); addf(&b, L"    - %ls\r\n", w); free(w); }
            if (rep.nproblems > 12) addf(&b, L"    - ... and %d more\r\n", rep.nproblems - 12);
            bad++;
        }
        free(data);
    }
    if (!missing && !bad) buf_add(&b, "\r\nAll files are there and intact. If the game still does not start, a file inside the set may be missing or a different version\r\n(ZiNc needs the file names of its own ROM list; this check cannot see inside a set).");
    else {
        buf_add(&b, "\r\n");
        if (missing) addf(&b, L"%d file(s) missing. ", missing);
        if (bad) addf(&b, L"%d file(s) damaged: download them again.", bad);
    }
    text = u8_to_w(b.s ? b.s : "", -1);
    buf_free(&b);
    if (!PostMessageW(j->dlg, WM_RC_DONE, 0, (LPARAM)text)) free(text);
    free(j);
    return 0;
}

static LRESULT CALLBACK dummy_unused(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }

void dlg_check_roms(HWND owner, const Game *g) {
    Modal md;
    Job *j = (Job *)calloc(1, sizeof *j);
    const wchar_t *cand[3], *role[3] = {L"game", L"parent set", L"BIOS"};
    int i, k;
    (void)dummy_unused;
    memset(&md, 0, sizeof md);
    md.kind = 7; md.owner = owner;
    wcscpy(md.title, L"Check ROM Set");
    make_modal(&md, 600, 400);
    md.c1 = mkat(md.dlg, L"EDIT", L"Checking the files…", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP, 10, 10, 580, 340, 0);
    SetWindowLongW(md.c1, GWL_EXSTYLE, GetWindowLongW(md.c1, GWL_EXSTYLE) | WS_EX_CLIENTEDGE);
    SetWindowPos(md.c1, NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
    mkat(md.dlg, L"BUTTON", L"Close", BS_DEFPUSHBUTTON | WS_TABSTOP, 510, 360, 80, 26, IDCANCEL);
    cand[0] = g->set; cand[1] = g->parent; cand[2] = g->bios;
    j->dlg = md.dlg;
    wcsncpy(j->title, g->title, 299);
    roms_dir_abs(j->dir, 560);
    for (i = 0; i < 3; i++) {
        int dup = 0;
        if (!cand[i][0]) continue;
        for (k = 0; k < j->n; k++) if (!_wcsicmp(j->names[k], cand[i])) dup = 1;
        if (dup) continue;
        wcsncpy(j->names[j->n], cand[i], 63);
        j->role[j->n++] = role[i];
    }
    if (j->n == 0) {
        SetWindowTextW(md.c1, L"ZiNc did not tell which ROM files this game needs.");
        free(j);
    } else CloseHandle(CreateThread(NULL, 0, check_thread, j, 0, NULL));
    run_modal(&md);
}
