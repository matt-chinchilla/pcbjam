#!/usr/bin/env bash
# Copyright (C) 2026 Circuit Center Inc
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Engine run 15: the export and notice check. Run before build G, before every train build and
# every repair build (the integrator), and by every author before handing over.
#
#   scripts/kicad/check-run15-exports.sh [--join] [--since <rev>] [--kicad <kicad tree>]
#                                        [--kicad-since <rev>]
#
# It reads the merged image's binding sources as the build compiles them (pcbnew_embind.cpp and
# eeschema_embind.cpp with KICAD_MERGED_EMBIND defined; symedit_embind.cpp, fpedit_embind.cpp,
# dsedit_embind.cpp and kicad_editor_embind.cpp without it; every local include followed in
# place, comments dropped, #if arms it cannot decide counted both ways) and fails when:
#   1. a kicad... name is registered (function("kicad...")) more than once across them;
#   2. a registered name is in neither list of wasm/bindings/run15-exports.txt;
#   3. a run 15 name is registered outside its lane's own files (cc_eN_*.inc; dsedit_embind.cpp
#      is lane e6's; kicad_editor_embind.cpp and the two cc_rules incs are e0's);
#   4. with --join: a run 15 name of the list is not registered;
#   5. a file added since <rev> (default: the run 15 base) lacks the notice in its first ten
#      lines ("Copyright (C) 2026 Circuit Center Inc" and "SPDX-License-Identifier:
#      GPL-3.0-or-later"), or a file changed since <rev> adds no "Modified by Circuit Center Inc
#      on <date>" line (with --kicad, the same for the KiCad tree since --kicad-since);
#   6. an added file, or an added line of a changed file, names an agent, a tool, a model or a
#      session.
# Exit 0 when every check passes, 1 when any fails (each failure on its own line), 2 on usage.
set -euo pipefail

PCBJAM="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# The commits engine run 15 starts from (pcbjam cc/main and kicad cc/main on 2026-11-18).
RUN15_PCBJAM_BASE=9706f071ab96f6cb25c7f33e599c967eb8c34257
RUN15_KICAD_BASE=1b214bab4c341d3a0af7ce2ec4a46288df9cc443
JOIN=0
SINCE="$RUN15_PCBJAM_BASE"
KICAD=""
KSINCE="$RUN15_KICAD_BASE"

while [ $# -gt 0 ]; do
  case "$1" in
    --join) JOIN=1; shift ;;
    --since) SINCE="${2:?}"; shift 2 ;;
    --kicad) KICAD="$(cd "${2:?}" && pwd)"; shift 2 ;;
    --kicad-since) KSINCE="${2:?}"; shift 2 ;;
    *) echo "usage: check-run15-exports.sh [--join] [--since <rev>] [--kicad <tree>] [--kicad-since <rev>]" >&2; exit 2 ;;
  esac
done

exec python3 -I - "$PCBJAM" "$JOIN" "$SINCE" "$KICAD" "$KSINCE" <<'PY'
import codecs, os, re, subprocess, sys

pcbjam, join, since, kicad, ksince = sys.argv[1], sys.argv[2] == "1", sys.argv[3], sys.argv[4], sys.argv[5]
bind = os.path.join(pcbjam, "wasm", "bindings")
fails = []

def fail(msg):
    fails.append(msg)

# ── the list ──
listed_new, listed_old = {}, set()
listfile = os.path.join(bind, "run15-exports.txt")
for n, raw in enumerate(open(listfile, encoding="utf-8"), 1):
    line = raw.split("#", 1)[0].strip()
    if not line:
        continue
    parts = line.split()
    if parts[0] == "new" and len(parts) >= 3:
        if parts[1] in listed_new:
            fail(f"run15-exports.txt:{n}: {parts[1]} listed twice")
        listed_new[parts[1]] = parts[2]
    elif parts[0] == "run14" and len(parts) == 2:
        listed_old.add(parts[1])
    else:
        fail(f"run15-exports.txt:{n}: not a list line: {raw.strip()}")
for name in set(listed_new) & listed_old:
    fail(f"run15-exports.txt: {name} is both a run 14 and a run 15 name")

# ── the merged image's sources, as the build compiles them ──
def strip_comments(text):
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif c == "/" and i + 1 < n and text[i + 1] == "*":
            j = text.find("*/", i + 2)
            seg = text[i:(n if j < 0 else j + 2)]
            out.append("\n" * seg.count("\n"))
            i = n if j < 0 else j + 2
        elif c == '"' or c == "'":
            m = re.match(r'R"([^(\s]*)\(', text[i - 1:i + 20]) if c == '"' and i > 0 and text[i - 1] == "R" else None
            if m:
                end = text.find(")" + m.group(1) + '"', i)
                end = n if end < 0 else end + len(m.group(1)) + 2
                out.append(text[i:end]); i = end; continue
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1]); i = j + 1
        else:
            out.append(c); i += 1
    return "".join(out)

TRUE, FALSE, UNKNOWN = 1, 0, 2

def cond_value(expr, defines, base):
    expr = expr.strip()
    m = re.fullmatch(r'__has_include\s*\(\s*"([^"]+)"\s*\)', expr)
    if m:
        return TRUE if os.path.exists(os.path.join(base, m.group(1))) else FALSE
    m = re.fullmatch(r'(!?)\s*defined\s*\(?\s*(\w+)\s*\)?', expr)
    if m:
        known = {"KICAD_MERGED_EMBIND": "KICAD_MERGED_EMBIND" in defines, "__EMSCRIPTEN__": True}
        if m.group(2) in known:
            v = known[m.group(2)]
            return TRUE if (v != bool(m.group(1))) else FALSE
    return UNKNOWN

regs = []   # (name, file, line, tu)
FUNC = re.compile(r'\bfunction\s*\(\s*"(kicad\w*)"')

def scan(path, defines, tu, seen):
    real = os.path.realpath(path)
    text = open(path, encoding="utf-8", errors="replace").read()
    if "#pragma once" in text:
        if real in seen:
            return
        seen.add(real)
    text = strip_comments(text)
    stack = []   # each: [active_before, taken_any, this_value]
    def active():
        return all(fr[2] != FALSE for fr in stack)
    for ln, line in enumerate(text.split("\n"), 1):
        s = line.strip()
        d = re.match(r'#\s*(ifdef|ifndef|if|elif|else|endif|include)\b(.*)', s)
        if d:
            kw, rest = d.group(1), d.group(2).strip()
            if kw == "ifdef":
                stack.append([None, None, cond_value("defined(" + rest + ")", defines, os.path.dirname(path))])
            elif kw == "ifndef":
                stack.append([None, None, cond_value("!defined(" + rest + ")", defines, os.path.dirname(path))])
            elif kw == "if":
                stack.append([None, None, cond_value(rest, defines, os.path.dirname(path))])
            elif kw == "elif":
                prev = stack[-1][2]
                v = cond_value(rest, defines, os.path.dirname(path))
                stack[-1][2] = FALSE if prev == TRUE else (UNKNOWN if prev == UNKNOWN else v)
            elif kw == "else":
                prev = stack[-1][2]
                stack[-1][2] = FALSE if prev == TRUE else (TRUE if prev == FALSE else UNKNOWN)
            elif kw == "endif":
                stack.pop()
            elif kw == "include" and active():
                m = re.match(r'"([^"]+)"', rest)
                if m:
                    inc = os.path.join(os.path.dirname(path), m.group(1))
                    if os.path.exists(inc):
                        scan(inc, defines, tu, seen)
            continue
        if not active():
            continue
        for m in FUNC.finditer(line):
            regs.append((m.group(1), os.path.relpath(path, pcbjam), ln, tu))

TUS = [("pcbnew_embind.cpp", {"KICAD_MERGED_EMBIND"}), ("eeschema_embind.cpp", {"KICAD_MERGED_EMBIND"}),
       ("symedit_embind.cpp", set()), ("fpedit_embind.cpp", set()), ("dsedit_embind.cpp", set()),
       ("kicad_editor_embind.cpp", set())]
for tu, defines in TUS:
    p = os.path.join(bind, tu)
    if not os.path.exists(p):
        fail(f"wasm/bindings/{tu}: missing (a merged image TU)")
        continue
    scan(p, defines, tu, set())

by_name = {}
for r in regs:
    by_name.setdefault(r[0], []).append(r)

def owner_of(relpath):
    base = os.path.basename(relpath)
    m = re.fullmatch(r'cc_(e\d+)_\w+\.inc', base)
    if m:
        return m.group(1)
    if base == "dsedit_embind.cpp":
        return "e6"
    if base in ("kicad_editor_embind.cpp", "cc_rules_pcb.inc", "cc_rules_sch.inc"):
        return "e0"
    return None

for name, rs in sorted(by_name.items()):
    if len(rs) > 1:
        where = ", ".join(f"{f}:{l}" for _, f, l, _ in rs)
        fail(f"{name}: registered {len(rs)} times ({where})")
    if name not in listed_new and name not in listed_old:
        fail(f"{name}: registered at {rs[0][1]}:{rs[0][2]} but in neither list of run15-exports.txt")
    if name in listed_new:
        for _, f, l, _ in rs:
            own = owner_of(f)
            if own != listed_new[name]:
                fail(f"{name}: registered at {f}:{l}, a file of {own or 'no lane'}; the list gives it to {listed_new[name]}")

if join:
    for name, lane in sorted(listed_new.items()):
        if name not in by_name:
            fail(f"{name}: listed for {lane} but not registered (--join)")

# ── notices and names in what changed ──
NOTICE = ("Copyright (C) 2026 Circuit Center Inc", "SPDX-License-Identifier: GPL-3.0-or-later")
MODIFIED = re.compile(r"Modified by Circuit Center Inc on 20\d\d-\d\d-\d\d")
# The words are kept rot13 encoded, so this file names none of them.
BANNED = re.compile(codecs.decode('\\o(pynhqr|naguebcvp|bchf\\f*\\q|fbaarg|unvxh|pungtcg|bcranv|tcg-?\\q|pbcvybg|pbqrk|trzvav|yyz|fhontrag|pb-nhguberq-ol|pynhqr-frffvba|trarengrq (ol|jvgu) (na? )?(nv|nffvfgnag|zbqry))\\o|pynhqr\\.nv|frffvba_[0-9N-Mn-m]{8,}|\\ojs_[0-9n-s]{6,}', "rot13"), re.I)

def git(repo, *args):
    return subprocess.run(["git", "-C", repo, *args], capture_output=True, text=True, check=True).stdout

def check_tree(repo, rev, label):
    try:
        git(repo, "rev-parse", "--verify", "--quiet", rev + "^{commit}")
    except subprocess.CalledProcessError:
        fail(f"{label}: {rev} is not a commit here")
        return
    # Committed and uncommitted changes since rev, untracked files included.
    status = git(repo, "diff", "--name-status", "--no-renames", rev, "--")
    changes = [l.split("\t", 1) for l in status.splitlines() if l]
    for f in git(repo, "ls-files", "--others", "--exclude-standard").splitlines():
        changes.append(["A", f])
    for kind, f in changes:
        p = os.path.join(repo, f)
        if kind == "D" or not os.path.isfile(p):
            continue
        try:
            text = open(p, encoding="utf-8").read()
        except UnicodeDecodeError:
            continue
        if kind == "A":
            head = "\n".join(text.split("\n")[:10])
            if not all(s in head for s in NOTICE):
                fail(f"{label}/{f}: a new file without the notice in its first ten lines")
            m = BANNED.search(text)
            if m:
                fail(f"{label}/{f}: names {m.group(0)!r}")
        else:
            added = [l[1:] for l in git(repo, "diff", rev, "--", f).splitlines()
                     if l.startswith("+") and not l.startswith("+++")]
            if not any(MODIFIED.search(l) for l in added):
                fail(f"{label}/{f}: changed, but adds no 'Modified by Circuit Center Inc on <date>' line")
            for l in added:
                m = BANNED.search(l)
                if m:
                    fail(f"{label}/{f}: an added line names {m.group(0)!r}")
                    break

check_tree(pcbjam, since, "pcbjam")
if kicad:
    check_tree(kicad, ksince, "kicad")

n_new = sum(1 for n in listed_new if n in by_name)
print(f"check-run15-exports: {len(by_name)} names registered in the merged image, "
      f"{n_new} of {len(listed_new)} run 15 names, {len(fails)} failures")
for f in fails:
    print("FAIL " + f)
sys.exit(1 if fails else 0)
PY
