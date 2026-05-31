#!/usr/bin/env python3
"""
Route parity checker for receiver web stacks.

Validates for each project:
1) Route URIs in page_registration_factory.cpp are in parity with page_definitions.cpp
   (allowing known non-page routes such as /static/helpers.js).
2) Legacy receiver_page* orphan modules are not functionally active.

Exit codes:
  0 = checks passed
  1 = one or more checks failed
"""

from __future__ import annotations

import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, List, Set


ALLOWED_FACTORY_ONLY_ROUTES = {
    "/static/helpers.js",
}

LEGACY_RETIRED_SENTINEL = "Legacy unregistered /receiver page retired."


@dataclass
class ProjectSpec:
    name: str
    root: Path
    factory: Path
    definitions: Path
    legacy_glob: str


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="ignore")


def parse_factory_routes(factory_text: str) -> Set[str]:
    # Matches: { "/route", ::register_xxx }
    pattern = re.compile(r"\{\s*\"([^\"]+)\"\s*,\s*::register_[^}]+\}")
    return set(pattern.findall(factory_text))


def parse_definition_routes(definitions_text: str) -> Set[str]:
    # Matches: { "/route", "Title", subtype_xxx, ... }
    pattern = re.compile(r"\{\s*\"([^\"]+)\"\s*,\s*\"[^\"]*\"\s*,")
    return set(pattern.findall(definitions_text))


def validate_legacy_files(files: Iterable[Path]) -> List[str]:
    violations: List[str] = []
    for file_path in files:
        content = read_text(file_path)
        if LEGACY_RETIRED_SENTINEL not in content:
            violations.append(f"{file_path}: missing retired sentinel")
    return violations


def check_project(spec: ProjectSpec) -> List[str]:
    issues: List[str] = []

    if not spec.factory.exists():
        issues.append(f"{spec.name}: missing factory file: {spec.factory}")
        return issues
    if not spec.definitions.exists():
        issues.append(f"{spec.name}: missing definitions file: {spec.definitions}")
        return issues

    factory_routes = parse_factory_routes(read_text(spec.factory))
    definition_routes = parse_definition_routes(read_text(spec.definitions))

    missing_in_factory = sorted(definition_routes - factory_routes)
    missing_in_definitions = sorted(
        route for route in (factory_routes - definition_routes)
        if route not in ALLOWED_FACTORY_ONLY_ROUTES
    )

    if missing_in_factory:
        issues.append(
            f"{spec.name}: routes in page_definitions.cpp but not in page_registration_factory.cpp: {missing_in_factory}"
        )
    if missing_in_definitions:
        issues.append(
            f"{spec.name}: routes in page_registration_factory.cpp but not in page_definitions.cpp: {missing_in_definitions}"
        )

    legacy_files = sorted(spec.root.glob(spec.legacy_glob))
    if legacy_files:
        legacy_issues = validate_legacy_files(legacy_files)
        if legacy_issues:
            issues.append(
                f"{spec.name}: legacy receiver_page* files are present but not fully retired: {legacy_issues}"
            )
        else:
            issues.append(
                f"{spec.name}: legacy receiver_page* files still exist physically ({len(legacy_files)} files)"
            )

    return issues


def main() -> int:
    if len(sys.argv) != 3:
        print("Usage: python route_parity_checker.py <espnowreceiver_2_root> <espnowreceiver_LCD_root>")
        return 1

    rx2_root = Path(sys.argv[1])
    lcd_root = Path(sys.argv[2])

    projects = [
        ProjectSpec(
            name="espnowreceiver_2",
            root=rx2_root,
            factory=rx2_root / "lib" / "webserver" / "page_registration_factory.cpp",
            definitions=rx2_root / "lib" / "webserver" / "page_definitions.cpp",
            legacy_glob="lib/webserver/pages/receiver_page*.*",
        ),
        ProjectSpec(
            name="espnowreceiver_LCD",
            root=lcd_root,
            factory=lcd_root / "lib" / "webserver_lcd" / "page_registration_factory.cpp",
            definitions=lcd_root / "lib" / "webserver_lcd" / "page_definitions.cpp",
            legacy_glob="lib/webserver_lcd/pages/receiver_page*.*",
        ),
    ]

    all_issues: List[str] = []
    for spec in projects:
        all_issues.extend(check_project(spec))

    if all_issues:
        print("Route parity checker: FAIL")
        for issue in all_issues:
            print(f" - {issue}")
        return 1

    print("Route parity checker: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
