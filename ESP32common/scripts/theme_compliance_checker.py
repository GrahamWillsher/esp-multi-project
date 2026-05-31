#!/usr/bin/env python3
"""
Theme Compliance Checker
=======================

Validates that transmitter and receiver pages follow the theme cascade contract:
- /transmitter/* pages MUST NOT contain hardcoded #4CAF50 (receiver green)
- /receiver/* pages MUST NOT contain hardcoded #2196F3 (transmitter blue)
- All theme colors MUST come from CSS variables (--primary-color) or semantic classes

Usage:
    python theme_compliance_checker.py <project_path>

Exit codes:
    0 = All checks passed
    1 = Compliance violations found
"""

import sys
import re
import os
from pathlib import Path


class ThemeComplianceChecker:
    """Validates theme compliance in web server pages."""

    # Forbidden color combinations by page type.
    # Each regex supports optional leading '#' and is case-insensitive.
    FORBIDDEN_COLOR_PATTERNS = {
        # Receiver green must never be hardcoded in transmitter pages.
        'transmitter': [
            r'(?<![0-9A-Fa-f])#?4CAF50(?![0-9A-Fa-f])',
            r'(?<![0-9A-Fa-f])#?33A35F(?![0-9A-Fa-f])',
        ],
        # Transmitter blue must never be hardcoded in receiver pages.
        'receiver': [
            r'(?<![0-9A-Fa-f])#?2196F3(?![0-9A-Fa-f])',
            r'(?<![0-9A-Fa-f])#?2D7DFF(?![0-9A-Fa-f])',
        ],
    }

    # Files to check
    PAGE_PATTERNS = {
        'transmitter': [
            'lib/webserver/pages/settings_page*.cpp',
            'lib/webserver/pages/transmitter_hub_page*.cpp',
            'lib/webserver/pages/hardware_config_page*.cpp',
            'lib/webserver/pages/battery_settings_page*.cpp',
            'lib/webserver/pages/inverter_settings_page*.cpp',
            'lib/webserver/pages/monitor_page*.cpp',
            'lib/webserver/pages/monitor2_page*.cpp',
            'lib/webserver/pages/debug_page*.cpp',
            'lib/webserver/pages/reboot_page*.cpp',
            'lib/webserver/pages/dashboard_page*.cpp',
            'lib/webserver_lcd/pages/settings_page*.cpp',
            'lib/webserver_lcd/pages/transmitter_hub_page*.cpp',
            'lib/webserver_lcd/pages/hardware_config_page*.cpp',
            'lib/webserver_lcd/pages/battery_settings_page*.cpp',
            'lib/webserver_lcd/pages/inverter_settings_page*.cpp',
            'lib/webserver_lcd/pages/monitor_page*.cpp',
            'lib/webserver_lcd/pages/monitor2_page*.cpp',
            'lib/webserver_lcd/pages/debug_page*.cpp',
            'lib/webserver_lcd/pages/reboot_page*.cpp',
            'lib/webserver_lcd/pages/dashboard_page*.cpp',
        ],
        'receiver': [
            'lib/webserver/pages/receiver_page*.cpp',
            'lib/webserver/pages/systeminfo_page*.cpp',
            'lib/webserver/pages/cellmonitor_page*.cpp',
            'lib/webserver/pages/network_config_page*.cpp',
            'lib/webserver/pages/memoryhealth_page*.cpp',
            'lib/webserver_lcd/pages/receiver_page*.cpp',
            'lib/webserver_lcd/pages/systeminfo_page*.cpp',
            'lib/webserver_lcd/pages/cellmonitor_page*.cpp',
            'lib/webserver_lcd/pages/network_page*.cpp',
            'lib/webserver_lcd/pages/memoryhealth_page*.cpp',
        ],
    }

    def __init__(self, project_path):
        """Initialize checker with project path."""
        self.project_path = Path(project_path)
        self.violations = []
        self.checked_files = 0

    def check_compliance(self):
        """Run all compliance checks."""
        print("🔍 Theme Compliance Checker")
        print("=" * 60)

        for page_type, patterns in self.PAGE_PATTERNS.items():
            print(f"\n📋 Checking {page_type.upper()} pages...")
            forbidden = self.FORBIDDEN_COLOR_PATTERNS[page_type]
            
            for pattern in patterns:
                for file_path in self.project_path.glob(pattern):
                    if file_path.is_file():
                        self.checked_files += 1
                        self._check_file(file_path, page_type, forbidden)

        return self._report_results()

    def _check_file(self, file_path, page_type, forbidden_patterns):
        """Check a single file for violations."""
        try:
            with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
                lines = f.read().splitlines()

            seen = set()

            for pattern_text in forbidden_patterns:
                pattern = re.compile(pattern_text, re.IGNORECASE)
                for line_num, line in enumerate(lines, 1):
                    stripped = line.strip()

                    # Skip comments and CSS variable definitions.
                    if '--primary-color' in line or stripped.startswith('//'):
                        continue
                    
                    match = pattern.search(line)
                    if match and ('"' in line or "'" in line):
                        key = (line_num, match.group(0).lower())
                        if key not in seen:
                            seen.add(key)
                            self.violations.append({
                                'file': str(file_path.relative_to(self.project_path)),
                                'line': line_num,
                                'page_type': page_type,
                                'color': match.group(0),
                                'content': line.strip()[:80],
                            })
        except Exception as e:
            print(f"⚠️  Error reading {file_path}: {e}")

    def _report_results(self):
        """Print compliance report."""
        print("\n" + "=" * 60)
        
        if not self.violations:
            print("✅ PASS: All pages comply with theme contract")
            print(f"\nScanned files: {self.checked_files}")
            print("\nTheme Contract Summary:")
            print("  • Transmitter pages: Use CSS variable --primary-color (blue #2196F3)")
            print("  • Receiver pages: Use CSS variable --primary-color (green #4CAF50)")
            print("  • No hardcoded theme colors (#2196F3, #4CAF50) allowed in page code")
            print("  • All theme styling must use semantic classes or CSS variables")
            return 0
        else:
            print(f"❌ FAIL: Found {len(self.violations)} compliance violations\n")
            print(f"Scanned files: {self.checked_files}\n")
            
            # Group by file
            by_file = {}
            for v in self.violations:
                if v['file'] not in by_file:
                    by_file[v['file']] = []
                by_file[v['file']].append(v)
            
            for file_path, violations in sorted(by_file.items()):
                print(f"\n📄 {file_path}")
                for v in violations:
                    print(f"   Line {v['line']:4d}: Found {v['color']} in {v['page_type']} page")
                    print(f"              {v['content']}...")
            
            print("\n⚠️  Action required:")
            print("  1. Replace hardcoded colors with CSS variable reads: getPrimaryColor()")
            print("  2. Use semantic classes: .theme-panel, .theme-primary-btn, etc.")
            print("  3. Re-run this checker to verify compliance")
            return 1


def main():
    """Entry point."""
    if len(sys.argv) < 2:
        print("Usage: python theme_compliance_checker.py <project_path>")
        print("\nExample:")
        print("  python theme_compliance_checker.py c:\\Users\\GrahamWillsher\\ESP32Projects\\espnowreceiver_2")
        sys.exit(1)

    project_path = sys.argv[1]
    if not os.path.isdir(project_path):
        print(f"❌ Error: Project path not found: {project_path}")
        sys.exit(1)

    checker = ThemeComplianceChecker(project_path)
    exit_code = checker.check_compliance()
    sys.exit(exit_code)


if __name__ == '__main__':
    main()
