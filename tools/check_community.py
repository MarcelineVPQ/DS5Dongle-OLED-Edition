#!/usr/bin/env python3
"""Community files check: the files GitHub's community standards checklist
looks for stay in the repo. Exit 1 with one line per missing file.

  python3 tools/check_community.py [repo dir]

Fails when one of these is missing: README.md, CODE_OF_CONDUCT.md,
CONTRIBUTING.md, LICENSE, SECURITY.md, an issue form in .github/ISSUE_TEMPLATE/,
.github/pull_request_template.md.
"""
import os
import sys

sys.dont_write_bytecode = True  # no tools/__pycache__ from the import
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from check_readmes import missing  # noqa: E402

COMMUNITY = [
    "README.md",
    "CODE_OF_CONDUCT.md",
    "CONTRIBUTING.md",
    "LICENSE",
    "SECURITY.md",
    ".github/ISSUE_TEMPLATE/bug_report.yml",
    ".github/pull_request_template.md",
]


def main():
    root = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), ".."))
    absent = missing(root, COMMUNITY)
    for path in absent:
        print(f"check_community: {path}: missing (GitHub's community standards; CONTRIBUTING.md)", file=sys.stderr)
    if absent:
        return 1
    print(f"check_community: {len(COMMUNITY)} community files present")
    return 0


if __name__ == "__main__":
    sys.exit(main())
