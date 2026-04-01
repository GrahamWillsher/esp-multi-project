#!/usr/bin/env python3
"""Hardening guardrails for active ESP web/network paths.

This script provides a small, reviewable set of static checks that protect
the hardening work completed in the receiver/transmitter codebases.

Current coverage:
- raw `httpd_resp_send*` usage in active web/network handler code
- raw allocation usage in the same hot paths, with explicit OTA exceptions

It is intentionally scoped to current hardening hotspots so the signal stays
high and the check can be introduced without a large false-positive backlog.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path


HTTP_SEND_PATTERNS = (
    "httpd_resp_send",
    "httpd_resp_sendstr",
    "httpd_resp_send_chunk",
    "httpd_resp_send_err",
    "httpd_resp_send_408",
    "httpd_resp_send_500",
)

HTTP_SEND_REGEX = re.compile(
    r"\b(" + "|".join(re.escape(name) for name in HTTP_SEND_PATTERNS) + r")\s*\("
)

ALLOCATION_REGEX = re.compile(r"\b(?:malloc|ps_malloc|free)\s*\(")
RAW_NEW_DELETE_REGEX = re.compile(r"(?:=\s*new\s+|\bdelete\s+[A-Za-z_(])")
LEGACY_SPEC_LAYOUT_REGEX = re.compile(
    r"\b(?:build_spec_page_html_header|build_spec_page_html_footer|build_spec_page_nav_links)\s*\("
)

DEFAULT_SCAN_ROOT = Path(__file__).resolve().parents[2]


@dataclass(frozen=True)
class RuleSet:
    name: str
    files: tuple[str, ...]
    pattern: re.Pattern[str]
    allowed_line_patterns: tuple[re.Pattern[str], ...]
    description: str


RULES = (
    RuleSet(
        name="unchecked-http-send",
        files=(
            "ESPnowtransmitter2/espnowtransmitter2/src/network/**/*.cpp",
            "ESPnowtransmitter2/espnowtransmitter2/src/network/**/*.h",
            "espnowreceiver_2/lib/webserver/**/*.cpp",
            "espnowreceiver_2/lib/webserver/**/*.h",
        ),
        pattern=HTTP_SEND_REGEX,
        allowed_line_patterns=(
            re.compile(r"\bconst\s+esp_err_t\s+rc\s*=\s*httpd_resp_send\s*\("),
            re.compile(r"\bconst\s+esp_err_t\s+rc\s*=\s*httpd_resp_send_chunk\s*\("),
            re.compile(r"\bconst\s+esp_err_t\s+send_rc\s*=\s*httpd_resp_send\s*\("),
            re.compile(r"\b(?:const\s+)?esp_err_t\s+\w+\s*=\s*httpd_resp_send(?:str|_chunk|_err|_408|_500)?\s*\("),
            re.compile(r"\breturn\s+httpd_resp_send(?:str|_chunk|_err|_408|_500)?\s*\("),
            re.compile(r"httpd_resp_send(?:str|_chunk|_err|_408|_500)?\s*\([^;]*(?:==|!=)\s*ESP_OK"),
        ),
        description="Raw httpd response sends must go through checked helper/result patterns.",
    ),
    RuleSet(
        name="allocation-policy",
        files=(
            "ESPnowtransmitter2/espnowtransmitter2/src/network/**/*.cpp",
            "ESPnowtransmitter2/espnowtransmitter2/src/network/**/*.h",
            "espnowreceiver_2/lib/webserver/**/*.cpp",
            "espnowreceiver_2/lib/webserver/**/*.h",
        ),
        pattern=ALLOCATION_REGEX,
        allowed_line_patterns=(
            re.compile(r"\bbuffer\s*=\s*static_cast<[^>]+>\(ps_malloc\s*\("),
            re.compile(r"\bbuffer\s*=\s*static_cast<[^>]+>\(malloc\s*\("),
            re.compile(r"\bfree\s*\(\s*buffer\s*\)"),
            re.compile(r"\bspecs_section\s*=\s*static_cast<[^>]+>\(ps_malloc\s*\("),
            re.compile(r"\bfree\s*\(\s*specs_section\s*\)"),
            re.compile(r"\bfree\s*\(\s*publish_buffer_\s*\)"),
            re.compile(r"\bpublish_buffer_\s*=\s*static_cast<[^>]+>\(ps_malloc\s*\("),
        ),
        description="Raw allocation calls are blocked in active handler paths unless explicitly reviewed.",
    ),
    RuleSet(
        name="legacy-spec-layout-string-api",
        files=(
            "ESPnowtransmitter2/espnowtransmitter2/**/*.cpp",
            "ESPnowtransmitter2/espnowtransmitter2/**/*.h",
            "espnowreceiver_2/**/*.cpp",
            "espnowreceiver_2/**/*.h",
            "ESP32common/**/*.cpp",
            "ESP32common/**/*.h",
            "esp32common/**/*.cpp",
            "esp32common/**/*.h",
        ),
        pattern=LEGACY_SPEC_LAYOUT_REGEX,
        allowed_line_patterns=(
            re.compile(r"\bString\s+build_spec_page_html_header\s*\("),
            re.compile(r"\bString\s+build_spec_page_html_footer\s*\("),
            re.compile(r"\bString\s+build_spec_page_nav_links\s*\("),
            re.compile(r"deprecated\("),
        ),
        description="Legacy String-based spec-layout builders are disallowed; use send_spec_page_response instead.",
    ),
    RuleSet(
        name="raw-new-delete-policy",
        files=(
            "ESPnowtransmitter2/espnowtransmitter2/src/network/**/*.cpp",
            "ESPnowtransmitter2/espnowtransmitter2/src/network/**/*.h",
            "espnowreceiver_2/lib/webserver/api/**/*.cpp",
            "espnowreceiver_2/lib/webserver/api/**/*.h",
            "espnowreceiver_2/lib/webserver/common/**/*.cpp",
            "espnowreceiver_2/lib/webserver/common/**/*.h",
        ),
        pattern=RAW_NEW_DELETE_REGEX,
        allowed_line_patterns=(
            re.compile(r"=\s*delete\s*;"),
        ),
        description="Raw new/delete is blocked in active web/network handler paths unless explicitly reviewed.",
    ),
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run hardening guardrail checks.")
    parser.add_argument(
        "--root",
        type=Path,
        default=DEFAULT_SCAN_ROOT,
        help="Workspace root that contains esp32common, espnowreceiver_2, and ESPnowtransmitter2.",
    )
    return parser.parse_args()


def strip_line_comment(line: str) -> str:
    if "//" not in line:
        return line
    return line.split("//", 1)[0]


def iter_files(root: Path, patterns: tuple[str, ...]) -> list[Path]:
    files: set[Path] = set()
    for pattern in patterns:
        files.update(path for path in root.glob(pattern) if path.is_file())
    return sorted(files)


def is_allowed(line: str, allowed_patterns: tuple[re.Pattern[str], ...]) -> bool:
    return any(pattern.search(line) for pattern in allowed_patterns)


def check_rule(root: Path, rule: RuleSet) -> list[str]:
    violations: list[str] = []
    for file_path in iter_files(root, rule.files):
        relative_path = file_path.relative_to(root).as_posix()
        for line_number, raw_line in enumerate(file_path.read_text(encoding="utf-8").splitlines(), start=1):
            line = strip_line_comment(raw_line)
            if not line.strip():
                continue
            if not rule.pattern.search(line):
                continue
            if is_allowed(line, rule.allowed_line_patterns):
                continue
            violations.append(f"{relative_path}:{line_number}: {rule.description}")
    return violations


def main() -> int:
    args = parse_args()
    root = args.root.resolve()

    if not root.exists():
        print(f"error: root does not exist: {root}", file=sys.stderr)
        return 2

    all_violations: list[str] = []
    for rule in RULES:
        violations = check_rule(root, rule)
        if violations:
            print(f"[FAIL] {rule.name}: {len(violations)} violation(s)")
            all_violations.extend(violations)
        else:
            print(f"[PASS] {rule.name}")

    if all_violations:
        print()
        for violation in all_violations:
            print(violation)
        return 1

    print()
    print("Hardening guardrails passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())