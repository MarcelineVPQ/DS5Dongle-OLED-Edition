#!/usr/bin/env python3
"""README parity check: the 7 READMEs (one per language the app ships) stay in
step with README.md, the reference. Exit 1 with one line per problem.

  python3 tools/check_readmes.py [repo dir]

Fails when:
  - one of the 7 READMEs is missing;
  - a language bar differs (same order and targets everywhere, the file's own
    language in bold without a link);
  - the heading structure (count and levels of # sections) differs from
    README.md;
  - a relative link or image path doesn't resolve, or an anchor (#id) points to
    a heading id that doesn't exist in its file;
  - the Development-channel note's version (X.Y.Z-dev.N) differs from
    README.md's.

When README.md changes, mirror the change to all 7 files (CLAUDE.md
"Releases").
"""
import os
import re
import sys
import unicodedata
from urllib.parse import unquote

# (file, name in the language bar); README.md first, the reference.
READMES = [
    ("README.md", "English"),
    ("README.CN.md", "中文"),
    ("README.DE.md", "Deutsch"),
    ("README.ES.md", "Español"),
    ("README.FR.md", "Français"),
    ("README.JA.md", "日本語"),
    ("README.PT-BR.md", "Português (Brasil)"),
]

FENCE = re.compile(r"^\s*(```|~~~)")
HEADING = re.compile(r"^(#{1,6})\s+(.*?)\s*#*\s*$")
MD_LINK = re.compile(r"!?\[(?:[^\[\]]|\[[^\]]*\])*\]\(\s*<?([^)\s>]+)>?(?:\s+\"[^\"]*\")?\s*\)")
HTML_REF = re.compile(r"""<(?:img|a)\b[^>]*?\b(?:src|href)\s*=\s*["']([^"']+)["']""", re.I)
DEV_VERSION = re.compile(r"\b\d+\.\d+\.\d+-dev\.\d+\b")


def language_bar(current):
    parts = []
    for path, name in READMES:
        parts.append(f"**{name}**" if path == current else f"[{name}]({path})")
    return " · ".join(parts)


def slug(text):
    """GitHub's heading id (github-slugger): lowercase, punctuation and
    symbols (emoji too) dropped, but '-', '_' and combining marks such as the
    emoji's U+FE0F kept; each space becomes '-'."""
    text = re.sub(r"`([^`]*)`", r"\1", text)  # inline code keeps its text
    text = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", text)  # links keep their text
    text = re.sub(r"<[^>]+>", "", text)
    out = []
    for ch in text.lower():
        cat = unicodedata.category(ch)
        if ch in "-_ ":
            out.append(ch)
        elif cat[0] in "LNM":
            out.append(ch)
    return "".join(out).replace(" ", "-")


def parse(path):
    """Headings [(line, level, text)], heading ids, and link targets [(line, target)]
    outside fenced code."""
    headings, ids, links, seen = [], set(), [], {}
    in_fence = False
    with open(path, encoding="utf-8") as f:
        lines = f.read().split("\n")
    for n, line in enumerate(lines, 1):
        if FENCE.match(line):
            in_fence = not in_fence
            continue
        if in_fence:
            continue
        m = HEADING.match(line)
        if m:
            level, text = len(m.group(1)), m.group(2)
            headings.append((n, level, text))
            base = slug(text)
            count = seen.get(base, 0)
            ids.add(base if count == 0 else f"{base}-{count}")
            seen[base] = count + 1
        for rx in (MD_LINK, HTML_REF):
            for t in rx.findall(re.sub(r"`[^`]*`", "", line)):
                links.append((n, t))
    for m in re.finditer(r"""\b(?:id|name)\s*=\s*["']([^"']+)["']""", "\n".join(lines)):
        ids.add(m.group(1))
    return lines, headings, ids, links


def section_of(headings, line):
    title = "(top)"
    for n, _, text in headings:
        if n > line:
            break
        title = text
    return title


def main():
    root = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), ".."))
    problems = []
    parsed = {}
    for path, _ in READMES:
        full = os.path.join(root, path)
        if not os.path.isfile(full):
            problems.append(f"{path}: missing (every README.md change goes to all {len(READMES)} languages)")
            continue
        parsed[path] = parse(full)

    ref = parsed.get("README.md")
    ref_dev = sorted(set(DEV_VERSION.findall("\n".join(ref[0])))) if ref else []

    for path, (lines, headings, ids, links) in parsed.items():
        # Language bar: exactly one, identical order and targets.
        want = language_bar(path)
        bars = [n for n, l in enumerate(lines, 1) if sum(f"]({r})" in l for r, _ in READMES) >= len(READMES) - 2]
        if not bars:
            problems.append(f"{path}: (top): no language bar; expected: {want}")
        for n in bars:
            if lines[n - 1].strip() != want:
                problems.append(f"{path}:{n}: {section_of(headings, n)}: language bar differs; expected: {want}")

        # Heading structure against README.md.
        if ref and path != "README.md":
            ref_levels = [lvl for _, lvl, _ in ref[1]]
            levels = [lvl for _, lvl, _ in headings]
            if levels != ref_levels:
                i = next((i for i, (a, b) in enumerate(zip(levels, ref_levels)) if a != b), min(len(levels), len(ref_levels)))
                at = headings[i] if i < len(headings) else None
                en = ref[1][i] if i < len(ref[1]) else None
                problems.append(
                    f"{path}: heading structure differs from README.md ({len(levels)} headings vs {len(ref_levels)}) "
                    f"at heading #{i + 1}: "
                    f"{'line %d %s %s' % (at[0], '#' * at[1], at[2]) if at else '(none)'} "
                    f"vs README.md {'line %d %s %s' % (en[0], '#' * en[1], en[2]) if en else '(none)'}")

        # Links and images.
        for n, target in links:
            if re.match(r"^[a-z][a-z0-9+.-]*:", target, re.I) or target.startswith("//"):
                continue  # absolute URL
            file_part, _, anchor = target.partition("#")
            file_part = unquote(file_part)
            dest = path if not file_part else os.path.normpath(os.path.join(os.path.dirname(path), file_part))
            where = f"{path}:{n}: {section_of(headings, n)}"
            if file_part and not os.path.exists(os.path.join(root, dest)):
                problems.append(f"{where}: link target '{target}' doesn't exist")
                continue
            if anchor:
                anchor = unquote(anchor)
                if dest in parsed:
                    target_ids = parsed[dest][2]
                elif dest.endswith(".md") and os.path.isfile(os.path.join(root, dest)):
                    target_ids = parse(os.path.join(root, dest))[2]
                else:
                    continue
                if anchor not in target_ids:
                    problems.append(f"{where}: anchor '#{anchor}' matches no heading id in {dest}")

        # Development-channel version.
        if ref and path != "README.md":
            dev = sorted(set(DEV_VERSION.findall("\n".join(lines))))
            if dev != ref_dev:
                n = next((i for i, l in enumerate(lines, 1) if DEV_VERSION.search(l)), 1)
                problems.append(
                    f"{path}:{n}: {section_of(headings, n)}: Development-channel version {', '.join(dev) or '(none)'} "
                    f"differs from README.md's {', '.join(ref_dev) or '(none)'}")

    for p in problems:
        print(f"check_readmes: {p}", file=sys.stderr)
    if problems:
        print(f"check_readmes: {len(problems)} problem(s); README.md is the reference, mirror changes to all {len(READMES)} READMEs",
              file=sys.stderr)
        return 1
    print(f"check_readmes: {len(READMES)} READMEs in step (language bar, {len(ref[1])} headings, links, dev version {', '.join(ref_dev) or '-'})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
