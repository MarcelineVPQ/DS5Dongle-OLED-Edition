#!/usr/bin/env python3
"""Loop-pass candidates for the firmware, listed mechanically (the AVIEW
"serial-loop pass": never review hot paths by eye). The doc with the verdict
tables lives once, in the app repo: docs/LOOP_PASS_2026-10.md
(MarcelineVPQ/DS5Dongle-OLED-Config-Web). See CLAUDE.md "Loop pass".

Every for / while / do loop in src/ (lib/ and the commented-out code
excluded), one TSV row each:

  file  line  function  ctx  wait  nest  flash  verdict  head

  ctx    where it runs, from a name-based call graph over src/:
         core1      reached from core1_entry (the RAM-resident audio core)
         core0-loop reached from main()'s forever loop (every pass, ~µs budget)
         callback   a TinyUSB tud_*_cb / BTstack handler / flash_safe_execute
                    callback (runs inside tud_task / cyw43_arch_poll on core0,
                    or with core0's IRQs off for the flash callback)
         isr        an IRQ / alarm / repeating-timer handler
         boot       only from main() before its loop, or from init code
         (several, joined with +; "-" = not reached from any root: dead or
         host-test-only)
  wait   forever (while(1) / for(;;)), spin (empty body or sleep/busy_wait/
         tight_loop_contents in it), +timeout when the loop checks a time,
         deadline or timeout; "-" otherwise
  nest   loops around it in the same function (0 = outermost)
  flash  "flash" when the body calls flash erase/program/save functions

Usage: python3 tools/list_loops.py           the TSV
       python3 tools/list_loops.py --check   the gate (tests/host/run.sh, CI):
                                             exit 1 on a loop without a valid
                                             "// loop: VERDICT — reason" comment
       python3 tools/list_loops.py --md      the per-file tables for the doc

A verdict is a comment on the loop's line or in the comment block right above:
    // loop: SERIAL-REQUIRED — one BT report per call; order is the protocol
Verdicts: PARALLELIZE BATCH SERIAL-REQUIRED HOT-PATH-OK FIX-HOT-PATH NOT-WORTH.
HOT-PATH-OK states its N: a number, or "N =" / "N ≤" (--check fails without).
No dependencies beyond the Python standard library.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
VERDICTS = ["PARALLELIZE", "BATCH", "SERIAL-REQUIRED", "HOT-PATH-OK", "FIX-HOT-PATH", "NOT-WORTH"]
VERDICT_RE = re.compile(r"loop:\s*(" + "|".join(VERDICTS) + r")\s+—\s+\S")
# HOT-PATH-OK states its N (CLAUDE.md "Loop pass"): a decimal number (hex
# literals such as 0xFC don't count) or "N =" / "N ≤" / "N <".
HOT_N_RE = re.compile(r"(?<![0-9A-Za-z_])\d|\bN\s*(=|≤|<)")
KEYWORDS = {"if", "for", "while", "switch", "return", "sizeof", "do", "else", "case", "catch",
            "static_assert", "alignof", "decltype", "defined", "__attribute__", "new", "delete"}
RAM_MACROS = {"CORE0_RAM_FUNC", "__not_in_flash_func", "__no_inline_not_in_flash_func",
              "__time_critical_func"}
FLASH_CALLS = re.compile(r"^(flash_range_(erase|program)|flash_sector_write|flash_safe_execute|"
                         r"config_save|save_slots_to_flash|remap_save|calib_save|\w+_save_to_flash)$")
TIME_TOKENS = re.compile(r"^(time_us_\d+|time_reached|get_absolute_time|make_timeout_time_\w+|"
                         r"absolute_time_diff_us|\w*deadline\w*|\w*timeout\w*|\w*_until\w*|to_ms_since_boot|"
                         r"board_millis|now_us)$", re.I)
# Functions the code in src/ doesn't call by name but other code does every
# frame: src/ram_mem.c's overrides, called by libopus on core1 and by the
# SDK / TinyUSB / BTstack on core0.
EXTERNAL_CALLERS = {"memcpy": ("core1", "core0-loop"), "memset": ("core1", "core0-loop"),
                    "memmove": ("core1", "core0-loop")}
DRAIN_CALLS = re.compile(r"(try_remove|try_add|pop|get_next|read)\w*$")
SPIN_CALLS = {"tight_loop_contents", "sleep_us", "sleep_ms", "busy_wait_us", "busy_wait_us_32",
              "busy_wait_ms", "busy_wait_at_least_cycles", "__wfe", "__wfi", "__nop", "__sev"}
ISR_REGISTRARS = re.compile(r"^(irq_set_exclusive_handler|irq_add_shared_handler|"
                            r"gpio_set_irq_enabled_with_callback|gpio_set_irq_callback|"
                            r"add_repeating_timer_\w+|add_alarm_\w+|alarm_pool_add_\w+)$")
CB_REGISTRARS = re.compile(r"^(\w*register\w*|\w*set_timer_handler|\w*add_event_handler|"
                           r"flash_safe_execute|multicore_launch_core1)$")
TOKEN = re.compile(r"[A-Za-z_]\w*|\d[\w.]*|::|->|&&|\|\||[{}()\[\];,<>=!+\-*/%&|^~?:.#]")


def strip(text: str) -> str:
    """Blank comments, strings and char literals (keeping line breaks), and
    keep only the first branch of each #if/#elif/#else group's braces sane:
    preprocessor lines become blank, and the lines of every branch after the
    first are kept but their brace effect is undone at #endif by the parser
    (see Parser.directive)."""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r"[^\n]", " ", text[i:j]))
            i = j
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            j += 1
            out.append(c + " " * max(0, j - i - 2) + c)
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


class Fn:
    def __init__(self, name, line, ram):
        self.name, self.line, self.ram = name, line, ram
        self.calls, self.regs, self.loops = set(), [], []
        self.ctx = set()


def tokens_of(path):
    """Tokens with line numbers; preprocessor directives become ('#dir', kind)."""
    toks = []
    text = strip(path.read_text(encoding="utf-8", errors="replace"))
    lines = text.split("\n")
    cont = False
    for ln, line in enumerate(lines, 1):
        s = line.strip()
        if cont or s.startswith("#"):
            if not cont:
                m = re.match(r"#\s*(if|ifdef|ifndef|elif|else|endif)\b", s)
                if m:
                    toks.append(("#" + m.group(1), ln))
            cont = s.endswith("\\")
            continue
        for m in TOKEN.finditer(line):
            toks.append((m.group(0), ln))
    return toks


class Parser:
    """A brace-aware walk: functions at file / namespace / class scope, the
    loops in them, the calls they make. #if groups: each later branch starts
    from the depth the group started with, and #endif keeps the first
    branch's depth (the usual ctags trick)."""

    def __init__(self, rel, toks):
        self.rel, self.toks = rel, toks
        self.fns = []

    def parse(self):
        toks = self.toks
        # Pass 1: match braces / parens, honouring #if groups.
        stack, ifs = [], []
        self.match = {}
        clean = []
        for t, ln in toks:
            if t.startswith("#"):
                if t in ("#if", "#ifdef", "#ifndef"):
                    ifs.append([list(stack), None])
                elif t in ("#elif", "#else") and ifs:
                    if ifs[-1][1] is None:
                        ifs[-1][1] = list(stack)
                    stack = list(ifs[-1][0])
                elif t == "#endif" and ifs:
                    start, first = ifs.pop()
                    if first is not None:
                        stack = first
                continue
            idx = len(clean)
            clean.append((t, ln))
            if t in "({[" and len(t) == 1:
                stack.append(idx)
            elif t in ")}]" and len(t) == 1 and stack:
                self.match[stack.pop()] = idx
        self.t = clean
        rev = {}
        for a, b in self.match.items():
            rev[b] = a
        self.rev = rev
        self.scope(0, len(clean), None)
        return self.fns

    def tok(self, i):
        return self.t[i][0] if 0 <= i < len(self.t) else ""

    def scope(self, i, end, cls):
        """File / namespace / class scope: find function bodies."""
        while i < end:
            t = self.tok(i)
            if t == "{":
                close = self.match.get(i, end)
                j = i - 1
                while self.tok(j) in ("const", "override", "noexcept", "final", "volatile"):
                    j -= 1
                if self.tok(j) in (")", "}"):
                    j = self.ctor_params(j)
                if self.tok(j) == ")" and j in self.rev:
                    name, ram = self.fn_name(self.rev[j])
                    if name and self.tok(self.prev_stmt_end(i) + 1) != "=":
                        fn = Fn((cls + "::" if cls else "") + name, self.t[i][1], ram)
                        fn.end = self.t[close][1] if close < len(self.t) else self.t[-1][1]
                        self.fns.append(fn)
                        self.body(i + 1, close, fn, 0)
                        i = close + 1
                        continue
                # namespace / extern "C" / class / struct / union: recurse
                head = [self.tok(k) for k in range(self.prev_stmt_end(i) + 1, i)]
                if head and head[0] in ("namespace", "extern"):
                    self.scope(i + 1, close, cls)
                elif any(h in ("class", "struct", "union") for h in head) and "=" not in head:
                    k = max(idx for idx, h in enumerate(head) if h in ("class", "struct", "union"))
                    nm = head[k + 1] if k + 1 < len(head) and re.match(r"[A-Za-z_]\w*$", head[k + 1]) else cls
                    self.scope(i + 1, close, nm)
                i = close + 1
                continue
            i += 1

    def ctor_params(self, j):
        """X() : a(1), b{} {  ->  the ')' of X's parameters (else j)."""
        k = j
        while self.tok(k) in (")", "}") and k in self.rev:
            k = self.rev[k] - 1
            if not re.match(r"[A-Za-z_]\w*$", self.tok(k)):
                return j
            k -= 1
            if self.tok(k) == ":":
                return k - 1 if self.tok(k - 1) == ")" else j
            if self.tok(k) != ",":
                return j
            k -= 1
        return j

    def prev_stmt_end(self, i):
        j = i - 1
        while j >= 0 and self.tok(j) not in (";", "}", "{"):
            if self.tok(j) in (")", "]") and j in self.rev:
                j = self.rev[j]
            j -= 1
        return j

    def fn_name(self, open_paren):
        j = open_paren - 1
        t = self.tok(j)
        if t == ")" and j in self.rev:  # MACRO(name)(args)
            inner = self.rev[j]
            if self.tok(inner - 1) in RAM_MACROS:
                return self.tok(inner + 1), True
            return None, False
        if re.match(r"[A-Za-z_~]\w*$", t) and t not in KEYWORDS:
            name = t
            if self.tok(j - 1) == "~":
                name = "~" + name
            if self.tok(j - 1) == "::" or self.tok(j - 2) == "::":
                k = j - 1 if self.tok(j - 1) == "::" else j - 2
                name = self.tok(k - 1) + "::" + name
            if self.tok(j - 1) == "operator":
                return None, False
            return name, False
        if t == "operator" or self.tok(j - 1) == "operator":
            return None, False
        return None, False

    def stmt_end(self, i, end):
        """Index after the statement starting at i."""
        t = self.tok(i)
        if t == "{":
            return self.match.get(i, end) + 1
        if t in ("for", "while", "if", "switch") and self.tok(i + 1) == "(":
            close = self.match.get(i + 1, end)
            k = self.stmt_end(close + 1, end)
            if t == "if" and self.tok(k) == "else":
                k = self.stmt_end(k + 1, end)
            return k
        if t == "do":
            k = self.stmt_end(i + 1, end)  # body, then while (...) ;
            if self.tok(k) == "while":
                k = self.match.get(k + 1, end) + 1
            return k + 1 if self.tok(k) == ";" else k
        if t == "else":
            return self.stmt_end(i + 1, end)
        depth = 0
        while i < end:
            t = self.tok(i)
            if t in ("(", "[", "{"):
                i = self.match.get(i, end) + 1
                continue
            if t == ";":
                return i + 1
            i += 1
        return end

    def body(self, i, end, fn, nest):
        while i < end:
            t = self.tok(i)
            if t in ("for", "while") and self.tok(i + 1) == "(":
                close = self.match.get(i + 1, end)
                bstart = close + 1
                bend = self.stmt_end(bstart, end)
                self.add_loop(fn, i, i + 1, close, bstart, bend, nest, "while" if t == "while" else "for")
                self.body(i + 2, close, fn, nest + 1)  # calls in the header run per pass too
                self.body(bstart, bend, fn, nest + 1)
                i = bend
                continue
            if t == "do":
                bend = self.stmt_end(i + 1, end)
                cond_open = bend + 1 if self.tok(bend) == "while" else None
                close = self.match.get(cond_open, bend) if cond_open else bend
                self.add_loop(fn, i, cond_open, close, i + 1, bend, nest, "do")
                self.body(i + 1, bend, fn, nest + 1)
                if cond_open:
                    self.body(cond_open + 1, close, fn, nest + 1)
                i = (close + 1) if cond_open else bend
                continue
            if re.match(r"[A-Za-z_]\w*$", t) and self.tok(i + 1) == "(" and t not in KEYWORDS:
                fn.calls.add(t)
                close = self.match.get(i + 1, end)
                args = [self.tok(k) for k in range(i + 2, close)]
                if ISR_REGISTRARS.match(t) or CB_REGISTRARS.match(t):
                    kind = "isr" if ISR_REGISTRARS.match(t) else "callback"
                    for a in args:
                        if re.match(r"[A-Za-z_]\w*$", a):
                            fn.regs.append((a, kind))
            i += 1

    def add_loop(self, fn, kw, cond_open, cond_close, bstart, bend, nest, kind):
        head_toks = [self.tok(k) for k in range(kw, (cond_close + 1) if cond_open else kw + 1)]
        body_toks = [self.tok(k) for k in range(bstart, bend)]
        cond = [self.tok(k) for k in range(cond_open + 1, cond_close)] if cond_open else []
        forever = (kind in ("while", "do") and cond in (["1"], ["true"])) or (kind == "for" and cond == [";", ";"])
        meaningful = [b for b in body_toks if b not in ("{", "}", ";")]
        drain = not meaningful and any(DRAIN_CALLS.search(c) for c in cond)
        spin = (not meaningful and not drain) or any(b in SPIN_CALLS for b in body_toks)
        timeout = any(TIME_TOKENS.match(x) for x in cond + body_toks)
        counted = kind == "for" and any(x in ("<", "<=") for x in cond)
        wait = "forever" if forever else ("spin" if spin else "drain" if drain else "-")
        if spin and counted and not timeout:
            wait += "+bounded"
        if timeout and wait != "-":
            wait += "+timeout"
        elif timeout:
            wait = "timeout"
        flash = "flash" if any(FLASH_CALLS.match(b) for b in body_toks) else "-"
        line = self.t[kw][1]
        if kind == "do":
            head = "do { ... } while (" + " ".join(cond) + ")"
        else:
            head = " ".join(head_toks)
        head = re.sub(r"\s+([,;)\]])", r"\1", re.sub(r"([(\[])\s+", r"\1", head))[:100]
        fn.loops.append(dict(line=line, nest=nest, wait=wait, flash=flash, head=head, kind=kind))


def build_graph():
    """Parse src/ and work out where each function runs (Fn.ctx). Returns
    (fns, by_file: rel path -> source lines). Shared with list_lifetimes.py."""
    files = sorted(p for p in (ROOT / "src").rglob("*") if p.suffix in (".c", ".cpp", ".h"))
    fns, by_file = [], {}
    for p in files:
        rel = p.relative_to(ROOT).as_posix()
        parsed = Parser(rel, tokens_of(p)).parse()
        for f in parsed:
            f.file = rel
        by_file[rel] = p.read_text(encoding="utf-8", errors="replace").split("\n")
        fns += parsed
    byname = {}
    for f in fns:
        byname.setdefault(f.name.split("::")[-1], []).append(f)

    def spread(roots, tag):
        queue = list(roots)
        while queue:
            f = queue.pop()
            if tag in f.ctx:
                continue
            f.ctx.add(tag)
            for c in f.calls:
                queue += byname.get(c, [])

    # Roots. core1: core1_entry. core0's loop: what main() calls inside its
    # forever loop. Callbacks: TinyUSB's tud_*_cb + anything handed to a
    # register / set_timer_handler / flash_safe_execute call. ISRs: IRQ,
    # alarm and repeating-timer handlers.
    spread(byname.get("core1_entry", []), "core1")
    main_fn = next((f for f in fns if f.name == "main" and f.file == "src/main.cpp"), None)
    if main_fn:
        loop_calls = main_loop_calls(main_fn)
        spread([g for c in loop_calls for g in byname.get(c, [])], "core0-loop")
    cbs = [f for f in fns if re.match(r"(tud_\w+_cb|tuh_\w+_cb|usbd_app_driver_get_cb)$", f.name)]
    # A function whose address is taken (&handler, a driver table, a
    # callback argument) is called back by BTstack / TinyUSB / the SDK.
    cbs += [g for name in address_taken(files, set(byname)) for g in byname.get(name, [])]
    cbs = [g for g in cbs if "core1" not in g.ctx]  # multicore_launch_core1(core1_entry)
    regs = [(a, k) for f in fns for a, k in f.regs]
    spread(cbs + [g for a, k in regs if k == "callback" for g in byname.get(a, [])], "callback")
    spread([g for a, k in regs if k == "isr" for g in byname.get(a, [])], "isr")
    if main_fn:
        spread([main_fn], "boot")
    # TinyUSB class driver tables (usbd_class_driver_t {.open = ..., .control_xfer_cb = ...})
    for name, tags in EXTERNAL_CALLERS.items():
        for tag in tags:
            for g in byname.get(name, []):
                g.ctx.discard(tag)
                spread([g], tag)
    for f in fns:
        if not f.ctx:
            f.ctx.add("-")
    return fns, by_file


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else ""
    fns, by_file = build_graph()
    rows = []
    for f in fns:
        lines = by_file[f.file]
        for lp in f.loops:
            ctx = "+".join(sorted(f.ctx - {"boot"}) or sorted(f.ctx))
            rows.append(dict(file=f.file, line=lp["line"], fn=f.name, ctx=ctx, wait=lp["wait"],
                             nest=lp["nest"], flash=lp["flash"], verdict=verdict_of(lines, lp["line"]),
                             head=lp["head"]))
    rows.sort(key=lambda r: (r["file"], r["line"]))

    if mode == "--check":
        bad = [r for r in rows if not r["verdict"] or r["verdict"].startswith("INVALID")]
        if bad:
            print(f"list_loops: {len(bad)} loop(s) without a valid verdict comment (CLAUDE.md \"Loop pass\"):", file=sys.stderr)
            for r in bad:
                print(f"  {r['file']}:{r['line']}  {r['fn']}  [{r['ctx']} {r['wait']}]  {r['verdict'] or 'no // loop: VERDICT — reason'}", file=sys.stderr)
            print("Add \"// loop: <" + "|".join(VERDICTS) + "> — <reason>\" on the loop's line or the line above.", file=sys.stderr)
            sys.exit(1)
        counts = {v: sum(r["verdict"].startswith(f"loop: {v} ") for r in rows) for v in VERDICTS}
        print(f"list_loops: {len(rows)} loops, all with a verdict (" + ", ".join(f"{v} {counts[v]}" for v in VERDICTS) + ")")
    elif mode == "--md":
        cur = None
        for r in rows:
            if r["file"] != cur:
                cur = r["file"]
                print(f"\n### {cur}\n\n| Line | Function | Runs on | Wait | Nest | Verdict | Reason |\n|---|---|---|---|---|---|---|")
            m = re.match(r"loop:\s*(\S+)\s+—\s+(.*?)\s*(\*/)?$", r["verdict"])
            v, why = (m.group(1), m.group(2)) if m else ("?", r["verdict"])
            wait = r["wait"] + (" flash" if r["flash"] != "-" else "")
            print(f"| {r['line']} | `{r['fn']}` | {r['ctx']} | {wait} | {r['nest']} | **{v}** | {why.replace('|', chr(92) + '|')} |")
    else:
        print("\t".join(["file", "line", "function", "ctx", "wait", "nest", "flash", "verdict", "head"]))
        for r in rows:
            print("\t".join(str(r[k]) for k in ["file", "line", "fn", "ctx", "wait", "nest", "flash", "verdict", "head"]))


def address_taken(files, names):
    out = set()
    for p in files:
        t = [x for x in tokens_of(p) if not x[0].startswith("#")]
        for i, (tok, _) in enumerate(t):
            if tok not in names or (i + 1 < len(t) and t[i + 1][0] == "("):
                continue
            prev = t[i - 1][0] if i else ""
            if prev == "(" and i > 1 and t[i - 2][0] in RAM_MACROS:
                continue  # CORE0_RAM_FUNC(name)(...): the definition
            if prev in ("&", "=", ",", "(", "{") and not (prev == "(" and i > 1 and t[i - 2][0] == "sizeof"):
                out.add(tok)
    return out


def main_loop_calls(main_fn):
    """Calls main() makes inside its forever loop: re-read main.cpp's tokens."""
    p = ROOT / main_fn.file
    parser = Parser(main_fn.file, tokens_of(p))
    parser.parse()
    t = parser.t
    calls = set()
    for i, (tok, ln) in enumerate(t):
        if tok == "while" and parser.tok(i + 1) == "(" and parser.tok(i + 2) in ("1", "true") and ln > main_fn.line:
            close = parser.match[i + 1]
            end = parser.stmt_end(close + 1, len(t))
            for k in range(close + 1, end):
                if re.match(r"[A-Za-z_]\w*$", t[k][0]) and parser.tok(k + 1) == "(":
                    calls.add(t[k][0])
            break
    return calls


def verdict_of(lines, line):
    v = _verdict_text(lines, line)
    if v.startswith("loop: HOT-PATH-OK") and not HOT_N_RE.search(re.sub(r"0x[0-9A-Fa-f]+", "", v.split("—", 1)[1])):
        return "INVALID (HOT-PATH-OK without its N) " + v
    return v


def _verdict_text(lines, line):
    own = lines[line - 1]
    if VERDICT_RE.search(own):
        return own[own.index("loop:"):].strip()
    i = line - 2
    while i >= 0 and re.match(r"\s*(//|/\*|\*)", lines[i]):
        if VERDICT_RE.search(lines[i]):
            return lines[i][lines[i].index("loop:"):].strip()
        if "loop:" in lines[i]:
            return "INVALID " + lines[i].strip()
        i -= 1
    if "loop:" in own:
        return "INVALID " + own.strip()
    return ""


if __name__ == "__main__":
    main()
