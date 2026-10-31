#!/usr/bin/env python3
"""Gate: CI builds from pinned inputs only, and gh always names its repo.

  python3 tools/check_ci_pins.py [repo root]

Fails when, in .github/workflows/*.yml or .github/actions/*/action.yml:
  - a `uses:` is not a local action (./...) or owner/repo@<40-hex commit>
    with its version as a comment (`# v4.4.0`): a tag can be moved;
  - a `node-version:` is not an exact X.Y.Z (or an ${{ expression }} that
    reads one, e.g. NODE_VERSION from tools/toolchain.env);
and when tools/toolchain.env lacks ARM_TOOLCHAIN_SHA256 (64 hex),
PICO_SDK_COMMIT (40 hex), TINYUSB_REF (40 hex) or NODE_VERSION (X.Y.Z);
and when a `gh` command in a workflow, action or tools/*.sh that works on a
repo (release, run, pr, issue, workflow, attestation, secret, variable,
cache) has no -R / --repo (gh api names the repo in its path).
Standard library only.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else pathlib.Path(__file__).resolve().parent.parent)
USES = re.compile(r"^\s*(?:-\s*)?uses:\s*(\S+)\s*(#.*)?$")
PINNED = re.compile(r"^[\w.-]+/[\w./-]+@[0-9a-f]{40}$")
NODE = re.compile(r"^\s*node-version:\s*[\"']?([^\"'#\s]+)")
GH = re.compile(r"(?:^|[\s;(&|$`])gh\s+(release|run|pr|issue|workflow|attestation|secret|variable|cache)\b")
PINS = {"ARM_TOOLCHAIN_SHA256": r"[0-9a-f]{64}", "PICO_SDK_COMMIT": r"[0-9a-f]{40}",
        "TINYUSB_REF": r"[0-9a-f]{40}", "NODE_VERSION": r"\d+\.\d+\.\d+"}

problems = []
ci_files = sorted((ROOT / ".github/workflows").glob("*.yml")) + sorted((ROOT / ".github/actions").glob("*/action.yml"))
for f in ci_files:
    rel = f.relative_to(ROOT)
    for n, line in enumerate(f.read_text().splitlines(), 1):
        if re.match(r"\s*#", line):
            continue
        m = USES.match(line)
        if m and not m.group(1).startswith("./"):
            if not PINNED.match(m.group(1)) or not (m.group(2) and re.search(r"v\d", m.group(2))):
                problems.append(f"{rel}:{n}: `{m.group(1)}` is not pinned by full commit SHA with a `# vX.Y.Z` comment")
        m = NODE.match(line)
        if m and not (re.fullmatch(r"\d+\.\d+\.\d+", m.group(1)) or m.group(1).startswith("${{")):
            problems.append(f"{rel}:{n}: node-version {m.group(1)} is not an exact X.Y.Z")

scripts = ci_files + sorted((ROOT / "tools").glob("*.sh"))
for f in scripts:
    rel = f.relative_to(ROOT)
    for n, line in enumerate(f.read_text().splitlines(), 1):
        code = line.split(" #", 1)[0] if not line.lstrip().startswith("#") else ""
        if GH.search(code) and not re.search(r"(?:^|\s)(-R|--repo)[\s=]", code):
            problems.append(f"{rel}:{n}: gh command without -R/--repo: {line.strip()}")

env = dict(re.findall(r"^([A-Z0-9_]+)=(\S*)$", (ROOT / "tools/toolchain.env").read_text(), re.M))
for key, pattern in PINS.items():
    if not re.fullmatch(pattern, env.get(key, "")):
        problems.append(f"tools/toolchain.env: {key} missing or not {pattern}")

if problems:
    print(f"check_ci_pins: {len(problems)} problem(s):", file=sys.stderr)
    for p in problems:
        print(f"  {p}", file=sys.stderr)
    sys.exit(1)
print(f"check_ci_pins: {len(ci_files)} CI files, every action pinned by commit, Node and toolchain exact, gh names its repo")
