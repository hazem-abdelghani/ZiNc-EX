#!/usr/bin/env python3
"""Builds ../../gui-c/combo_data.c from the SFEX2 Plus FAQ v1.5 by Chris MacDonald (GameFAQs).
Usage: python gen_presets.py path/to/sfex2plus_faq.txt [-v]   (the FAQ is copyrighted and is not bundled)
Each character's "Short Moveslist" is parsed; commands that cannot be expressed as a plain input sequence
(air-only moves, follow-ups "during X", hold-and-release moves, ...) are skipped and reported."""
import re, sys

SRC = next((a for a in sys.argv[1:] if not a.startswith('-')), 'sfex2plus_faq.txt')  # path to the FAQ text (not included in the repo)
L = open(SRC, encoding='latin-1').read().split('\n')

# ---- character regions
hdr = []
for i in range(1, len(L) - 1):
    if re.match(r'^ =+$', L[i - 1]) and re.match(r'^ =+$', L[i + 1]) and L[i].strip():
        hdr.append((i, L[i].strip()))
a = next(i for i, (n, t) in enumerate(hdr) if t.startswith('2.  CHARACTER'))
b = next(i for i, (n, t) in enumerate(hdr) if t.startswith('4.  SECRETS'))
regions = []
for (n, t), (n2, _) in zip(hdr[a + 1:b], hdr[a + 2:b + 1]):
    if re.match(r'^\d+\.', t):
        continue  # "3. HIDDEN CHARACTER MOVELISTS"
    regions.append((t, n, n2))

def pretty(title):
    m = re.match(r'^(.*?)\s{2,}\((.*)\)$', title)
    name, extra = (m.group(1), m.group(2)) if m else (title, '')
    name = name.strip().title().replace('Vega Ii', 'Vega II')
    if 'CPU' in extra.upper(): name += ' (CPU version)'
    q = re.search(r'known as "([^"]+)"', extra)
    if q:
        name += ' (%s in the West)' % q.group(1)
    return name

DIRS = {'d': '2', 'df': '3', 'f': '6', 'db': '1', 'b': '4', 'u': '8', 'uf': '9', 'ub': '7', 'ub~uf': '8'}
MOT = {'qcf': '236', 'qcb': '214', 'hcf': '41236', 'hcb': '63214'}
BTN = {'P': 'HP', 'K': 'HK', 'PPP': 'LP+MP+HP', 'KKK': 'LK+MK+HK', 'PP': 'MP+HP', 'KK': 'MK+HK'}


def buttons(spec):
    spec = spec.strip()
    first = re.split(r'\s*/\s*', spec)[0].strip()
    if first in BTN:
        return BTN[first]
    if re.fullmatch(r'[LMH][PK]', first):
        return first
    return None

def elem(e):
    e = e.strip()
    el = e.lower()
    if el in MOT: return MOT[el]
    if el in DIRS: return DIRS[el]
    m = re.fullmatch(r'rotate (360|720)', el)
    if m: return '632147' * (2 if m.group(1) == '720' else 1)
    return None

def conv(cmd):
    c = re.sub(r'\([^)]*\)', ' ', cmd)
    c = re.sub(r'\s+', ' ', c).strip().rstrip(',').strip()
    if re.match(r'(?i)in air|hold|direct|wait|any dir|tap [pk]\b', c): return None
    if re.search(r"(?i)when (icon|')", c): return None
    m = re.fullmatch(r'Press the same strength P \+ K', c)
    if m: return 'HP+HK'
    m = re.fullmatch(r'Press (PPP|KKK)', c)
    if m: return BTN[m.group(1)]
    m = re.fullmatch(r'Tap ([bf]),([bf])', c)
    if m: return '%s ~ %s' % (DIRS[m.group(1)], DIRS[m.group(2)])
    # chain of buttons and directions: LP,LP,f,LK,HP
    if re.fullmatch(r'(?:(?:[LMH][PK]|[dbuf]{1,2}),)+(?:[LMH][PK]|[dbuf]{1,2})', c):
        out = []
        for p in c.split(','):
            out.append(p if re.fullmatch(r'[LMH][PK]', p) else DIRS[p])
        return ' '.join(out)
    c = re.sub(r'(?i)^when close, ', '', c)
    charge = False
    if c.lower().startswith('charge '):
        charge = True; c = c[7:]
    if re.search(r'(?i)\bwhen\b', c) and ' + ' not in c.split(' when')[0]:
        return None
    btn, tail = None, ''
    if ' + ' in c:
        motion, rest = c.split(' + ', 1)
        m = re.match(r'^([A-Za-z]+(?: / [A-Za-z]+)*)(.*)$', rest)
        if not m: return None
        btn, tail = m.group(1), m.group(2).strip()
    else:
        motion = c
    if tail and not (tail.startswith(',') or tail.startswith('to counter')):
        return None
    if re.match(r'(?i)^(?:,\s*)?(?:then|tap|move|hold)\b', tail) is None and tail.startswith(',') is False and tail:
        pass
    # alternatives "b / f" -> first
    motion = re.sub(r'\s*/\s*[A-Za-z~]+', '', motion)
    elems = [elem(x) for x in re.split(r'\s*,\s*', motion)]
    if not elems or any(e is None for e in elems): return None
    bt = buttons(btn) if btn else None
    if btn and bt is None: return None
    toks = []
    rest_dirs = elems
    if charge:
        if len(elems[0]) != 1: return None
        # charge down-back instead of plain back/down: counts as both, and the character crouches in place
        # instead of walking away while charging
        first = '1' if elems[0] in '24' else elems[0]
        toks.append('%s*C' % first)
        rest_dirs = elems[1:]
    if rest_dirs:
        toks.append(''.join(rest_dirs))
    if bt:
        last = toks[-1] if toks else ''
        if not toks or last.endswith('C'):
            toks.append(bt)
        else:
            # up-type finishers (flash kick style): press the direction on its own first, then add the button
            if last[-1] in '789' and len(last) <= 3:
                toks[-1] = last
                toks.append(last[-1] + '+' + bt)
            else:
                toks[-1] += '+' + bt
    return ' '.join(toks)

# the short move lists of the FAQ differ from the move descriptions in a few places; the descriptions are right
FAQ_FIXES = {('Guile', 'Somersault Kick'): 'Charge d,u + K'}   # the list says "+ P", the description says "Charge d,u + K"
chars = []
skipped = []
for title, a0, b0 in regions:
    name = pretty(title)
    # English names of Meteor Combos + Japanese->English map
    jp2en, meteor = {}, set()
    sect = None
    i = a0
    last_jp = None
    while i < b0:
        l = L[i]
        mm = re.search(r'-+\s+\[ ([^\]]+) \]', l)
        if mm: sect = mm.group(1)
        if l.startswith(' Japanese:'): last_jp = l.split(':', 1)[1].strip()
        elif l.startswith(' English:'):
            en = l.split(':', 1)[1].strip()
            jp = last_jp or en
            jp2en[jp] = en
            if sect and sect.startswith('Meteor'):
                meteor.add(jp); meteor.add(en)
            last_jp = None
        i += 1
    # short list
    s = next((i for i in range(a0, b0) if 'Short Moveslist' in L[i]), None)
    if s is None: s = a0 + 1   # e.g. Vega: the list follows the title directly
    groups, cur = [], []
    for i in range(s + 1, b0):
        if 'Normal Throws' in L[i] and '---' in L[i]: break
        if L[i].startswith(' ---') or L[i].startswith(' ==='): break
        if not L[i].strip():
            if cur: groups.append(cur); cur = []
            continue
        cur.append(L[i])
    if cur: groups.append(cur)
    moves = []
    for gi, g in enumerate(groups):
        for line in g:
            m = re.match(r'^ (.{1,30}?)\s{2,}(.+)$', line)
            if not m: continue
            mname, cmd = m.group(1).strip(), m.group(2).strip()
            cmd = FAQ_FIXES.get((name, mname), cmd)
            seq = conv(cmd)
            if cmd.startswith('...') or mname.startswith('...'):
                seq = None
            if seq is None:
                skipped.append((name, mname, cmd)); continue
            en = jp2en.get(mname)
            disp = mname if not en or en == mname else '%s (%s)' % (mname, en)
            en_base = re.sub(r'\s*\([^)]*\)\s*$', '', en or '')
            meteor_base = {re.sub(r'\s*\([^)]*\)\s*$', '', x) for x in meteor}
            if mname in meteor or (en and en in meteor) or (en_base and en_base in meteor_base) or (gi >= 2 and re.search(r'\+ (PPP|KKK)\b', cmd)): kind = 'Meteor'
            elif gi == 0: kind = 'Guard Break' if 'same strength' in cmd else 'Throw'
            elif gi >= 2: kind = 'Super'
            else: kind = 'Special'
            moves.append((kind, disp, seq, cmd))
    if moves and 'CPU' not in name: chars.append((name, moves))   # CPU Garuda only adds a counter move

def q(s): return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'
out = ['/* Generated from the SFEX2 Plus FAQ moves lists (input-plugin/presets/gen_presets.py). DO NOT EDIT. */', '#include "common.h"', '']
for i, (name, moves) in enumerate(chars):
    out.append('static const ComboMove moves%d[] = {' % i)
    for kind, disp, seq, cmd in moves:
        out.append('    {%s, %s, %s, %s},' % (q(kind), q(disp), q(seq), q(cmd)))
    out.append('};')
out.append('const ComboChar g_comboChars[] = {')
for i, (name, moves) in enumerate(chars):
    out.append('    {%s, moves%d, %d},' % (q(name), i, len(moves)))
out.append('};')
out.append('const int g_nComboChars = %d;' % len(chars))
open('../../gui-c/combo_data.c', 'w').write('\n'.join(out) + '\n')
print('characters:', len(chars), ' moves:', sum(len(m) for _, m in chars), ' skipped:', len(skipped))
if '-v' in sys.argv:
    for n, m, c in skipped: print('SKIP  %-14s %-32s %s' % (n, m, c))
    for name, moves in chars:
        print('==', name)
        for k, d, s, c in moves: print('  %-11s %-40s %-24s | %s' % (k, d[:40], s, c))
