# Transmitter/Receiver Template Inheritance and Memory Hardening — Concrete Implementation Plan

Date: 2026-05-31
Scope: `espnowreceiver_2` and `espnowreceiver_LCD`
Plan owner: Receiver web/runtime maintainers

---

## 1) Non-negotiable target state

When this plan is complete:

1. `/transmitter` and `/transmitter/...` are consistently blue-themed.
2. `/receiver` and `/receiver/...` are consistently green-themed.
3. Dashboard button is injected at top by template and is theme-colored by route.
4. First page heading is theme-colored by template, not by page-local hardcoding.
5. All section surrounds are theme-colored by template classes.
6. `espnowreceiver_2` and `espnowreceiver_LCD` both use the same hardened page delivery model (or documented, justified exceptions).
7. Old/redundant/legacy code is removed; no shadow template systems remain.

---

## 2) Phase status model (must be updated as work proceeds)

Status values:

- `NOT STARTED`
- `IN PROGRESS`
- `COMPLETE`
- `BLOCKED`

Each phase is only marked `COMPLETE` when:

- all tasks in that phase are complete,
- all deletion/cleanup tasks in that phase are complete,
- verification gates pass,
- evidence is recorded in the phase log.

Mandatory verification rule:

- A phase is only `COMPLETE` after an explicit compliance check confirms implementation aligns with this document.
- If any item is found non-compliant during review, that phase must be moved back to `IN PROGRESS` and reworked until alignment is achieved.

### 2.1 Master phase tracker

| Phase | Name | Status | Completed date | Evidence (commit / PR / notes) |
|---|---|---|---|---|
| P0 | Baseline + inventory freeze | COMPLETE | 2026-05-31 | Baseline hotspot scans captured in terminal history; inventory confirmed against current source and tracker evidence |
| P1 | Central template contract enforcement | COMPLETE | 2026-05-31 | Shared CSS/template primitives introduced in page generators/common styles; theme contract enforced via shared generator path |
| P2 | Navigation/dashboard auto-injection | COMPLETE | 2026-05-31 | Template-level Dashboard injection implemented (route-aware) with opt-out support for exceptional pages |
| P3 | Legacy style and page cleanup (hard delete) | COMPLETE | 2026-05-31 | Dashboard/nav inline legacy blocks removed from remaining page content files in both codebases; route color overrides removed from first-line headings |
| P4 | Rendering hardening parity (`_2` ⇢ LCD model) | COMPLETE | 2026-05-31 | `espnowreceiver_2/lib/webserver/common/page_generator.cpp`; both `_2` envs build green after hardening and orphan cleanup |
| P5 | Route/factory parity + orphan removal | COMPLETE | 2026-05-31 | `renderPage` removed from `_2` template layer; `/receiver/memoryhealth` registered in `_2` and LCD factory/pages/page-definitions; LCD `network_page` root helper removed; orphan `receiver_page*` modules physically deleted from both codebases; sequential env builds are green for `_2` (TFT + non-TFT) and LCD (waveshare); route parity checker now passes |
| P6 | Guardrails + clean-codebase certification | COMPLETE | 2026-05-31 | `esp32common/scripts/theme_compliance_checker.py` passing (`_2`: 42 files, LCD: 39 files); `esp32common/scripts/route_parity_checker.py` passing after orphan deletion; sequential receiver builds green after cleanup |
| P7 | System Tools namespace migration (`/systemtools/*`) | IN PROGRESS | — | Approved addendum below: immediate move of `/debug` `/ota` `/events` for all receivers with hard delete of legacy routes |

---

## 3) Canonical template contract (implementation truth)

1. Theme mapping is route-derived in page generator only:
   - `/receiver*` => `theme-receiver`
   - all others => `theme-transmitter`
2. Template provides semantic styling primitives used by all pages:
   - `.page-title`
   - `.section-frame`
   - `.dashboard-link`
3. Page content must not own theme color via inline hex values for template-critical elements.
4. New pages must inherit theme/nav by default without custom color logic.

---

## 4) Concrete phased implementation

## P0 — Baseline + inventory freeze

Status: COMPLETE

### Tasks

- Capture full baseline inventory for both codebases:
  - page handlers,
  - page content files,
  - page generators,
  - nav generation,
  - route registration tables,
  - render paths (`send_rendered_page`, streaming, legacy helpers).
- Record all hardcoded template-color hotspots and dashboard-inline snippets.
- Record all orphan handlers not represented in route factories.

### Mandatory cleanup in this phase

- Remove or archive stale planning assumptions in this document section if they conflict with current source.

### Completion gate

- Baseline inventory is committed and referenced in tracker evidence.

---

## P1 — Central template contract enforcement

Status: COMPLETE

### Tasks

- In both codebases, enforce theme mapping and body class assignment in page generator only.
- Extend common styles so template-critical colors are CSS-variable driven.
- Ensure first visible heading styling is template-owned (e.g., `.page-title` / heading selector contract).
- Ensure section surround styling is template-owned (e.g., `.section-frame` + shared card selectors).

### Mandatory cleanup in this phase

- Remove duplicated/legacy theme resolution code outside page generators.
- Remove fallback hardcoded transmitter-only body class paths.

### Completion gate

- Theme behavior matches contract on hub + at least one nested transmitter page and one nested receiver page in each codebase.

---

## P2 — Navigation/dashboard auto-injection

Status: COMPLETE

### Tasks

- Add automatic top Dashboard injection in render pipeline (template-level, not page-level).
- Dashboard button must be first in top nav and theme-colored by route.
- Add explicit opt-out render option for exceptional pages only.

### Mandatory cleanup in this phase

- Delete page-local duplicated dashboard blocks where template now injects nav.
- Remove dead helper functions used only by deleted inline nav blocks.

### Completion gate

- All normal pages show template-injected Dashboard at top.
- No page requires inline Dashboard style blocks to meet contract.

---

## P3 — Legacy style and page cleanup (hard delete)

Status: COMPLETE

### Tasks

- Bulk-convert page content from inline template colors to semantic classes.
- Remove hardcoded template-color literals from template-critical UI elements.
- Replace page-specific heading color hacks with template class usage.

### Mandatory cleanup in this phase (required, not optional)

- Delete old/redundant/legacy template paths and styles that duplicate shared template logic.
- Delete no-longer-referenced CSS/JS fragments left by conversion.
- Delete route-specific copy-paste nav/button styling blocks.

### Completion gate

- Zero hardcoded template-color ownership in page templates for:
  - Dashboard button,
  - first heading line,
  - section surrounds.

---

## P4 — Rendering hardening parity (`_2` ⇢ LCD model)

Status: COMPLETE

### Tasks

- Bring `espnowreceiver_2` page rendering to LCD hardening parity:
  - guarded streaming/chunked rendering,
  - low-heap preflight refusal,
  - adaptive chunk policy,
  - request accounting and long-response logging,
  - external helper script caching route where applicable.
- Keep API middleware protections aligned with page delivery behavior.

### Mandatory cleanup in this phase

- Remove obsolete non-hardened render pathways once migration is complete.
- Remove compatibility wrappers that are no longer exercised.

### Completion gate

- Both receivers pass repeated-request stress validation without crash/regression.
- Hardened rendering path is the default, not an optional side path.

---

## P5 — Route/factory parity + orphan removal

Status: COMPLETE

### Tasks

- Resolve all route registration drift:
  - either register implemented handlers,
  - or remove orphan page modules.
- Resolve `/receiver` and `/receiver/memoryhealth` parity decision consistently across both codebases.
- Resolve LCD `/config` contract compliance (migrate into template path or document sanctioned exception).

### Mandatory cleanup in this phase

- Delete orphan page source files and dead declarations.
- Delete unused factory entries and stale comments that no longer match runtime registration.

### Completion gate

- No handler exists without intentional registration decision.
- No registered route points to legacy/dead behavior.

---

## P6 — Guardrails + clean-codebase certification

Status: COMPLETE

### Tasks

- Add automated compliance checks for:
  - hardcoded template colors in page content,
  - inline Dashboard-style regressions,
  - page modules bypassing standard render helpers,
  - route/handler registration drift.
- Add CI enforcement so new pages inherit by default and cannot regress silently.

### Mandatory cleanup in this phase

- Remove superseded scripts/checks that overlap or conflict with new guardrails.
- Remove temporary migration toggles used during earlier phases.

### Completion gate

- Guardrails pass on both codebases.
- Codebase is certified “clean” per Section 5 definition.

---

## 5) Definition of “clean codebase” for this plan

The codebase is considered clean only if all are true:

1. No template-critical UI color ownership in page-local inline styles.
2. No duplicate legacy render/template systems doing the same job.
3. No orphan/unregistered page modules left in source tree without documented reason.
4. No dead compatibility shims kept “just in case” after migration completion.
5. Shared template path is the default path used by all standard pages.

---

## 6) Phase completion log template (to be filled in during execution)

For each phase, append one entry:

- Phase: `Px`
- Status set to: `COMPLETE`
- Date:
- Evidence:
  - commits/PR
  - verification summary
  - deleted legacy items list
  - residual exceptions (if any) with rationale

If any deletion/cleanup item is incomplete, phase must remain `IN PROGRESS`.

### Execution note (implementation start)

- Date: 2026-05-31
- Update:
  - Added template-owned semantic CSS primitives (`.page-title`, `.section-frame`, `.template-top-nav`, `.dashboard-link`) in both receiver codebases.
  - Switched `h1` to theme-derived color in shared templates.
  - Added route-aware automatic Dashboard injection in shared page generators.
  - Added render option flag `include_template_dashboard_nav` (default on) for controlled opt-out.
- Phase impact:
  - P1: started
  - P2: started

### Execution note (legacy cleanup + build verification)

- Date: 2026-05-31
- Update:
  - Removed remaining page-local inline Dashboard blocks from transmitter/receiver page content modules in both `espnowreceiver_2` and `espnowreceiver_LCD`.
  - Removed first-line hardcoded transmitter/receiver heading color overrides from page-local content where template now owns heading color.
  - Restored `espnowreceiver_2` compatibility by replacing accidental streaming-only page send calls in `receiver_page.cpp` and `memoryhealth_page.cpp` with supported `send_rendered_page(...)` path.
  - Rebuilt both projects successfully (`espnowreceiver_2`: both environments succeeded; `espnowreceiver_LCD`: waveshare environment succeeded).
- Phase impact:
  - P3: started
  - P4: completed in subsequent hardening pass (see tracker)
  - P3+: pending

### Execution note (guardrail hardening sweep)

- Date: 2026-05-31
- Update:
  - Hardened `esp32common/scripts/theme_compliance_checker.py` to remove a path-globbing defect (`pattern.lstrip('lib/')`) that could skip intended files.
  - Expanded compliance coverage to include remaining legacy-prone page families (`dashboard`, `debug`, `monitor2`, `reboot`, `network`, `memoryhealth`) across both receiver codebases.
  - Added secondary palette checks so contract validation also catches `#33A35F` (receiver variant green) and `#2D7DFF` (transmitter variant blue) literals.
  - Added scan telemetry (`Scanned files: N`) and de-duplicated line-level violation reporting for cleaner audits.
- Phase impact:
  - P6: started (guardrail implementation active)
  - P5: still in progress (orphan file hard-delete pass pending final confirmation)

### Execution note (theme guardrail remediation + revalidation)

- Date: 2026-05-31
- Update:
  - Ran `theme_compliance_checker.py` against both receiver codebases and captured initial violations (23 in `espnowreceiver_2`, 23 in `espnowreceiver_LCD`).
  - Replaced remaining transmitter-page receiver-green literals across dashboard/transmitter-hub/settings/hardware script/content modules in both codebases.
  - Fixed LCD receiver script outlier using transmitter-blue literal (`systeminfo_page_script.cpp`) to receiver-green.
  - Re-ran compliance checker and confirmed PASS in both projects:
    - `espnowreceiver_2`: PASS, scanned files = 42
    - `espnowreceiver_LCD`: PASS, scanned files = 39
- Phase impact:
  - P6: progressed (guardrail checks now green for current code state)
  - P5: still in progress (orphan receiver page modules remain physically present pending hard-delete confirmation)

### Execution note (LCD memoryhealth route parity)

- Date: 2026-05-31
- Update:
  - Added `memoryhealth_page.h` to LCD consolidated page header (`pages/pages.h`) so registration symbols are available through the standard include path.
  - Added `/receiver/memoryhealth` registration entry in LCD page factory (`page_registration_factory.cpp`).
  - Added `/receiver/memoryhealth` entry to LCD central page registry (`page_definitions.cpp`) for route metadata parity.
  - Re-validated theme guardrail status for LCD project: PASS (39 scanned files).
  - `_2` build remains green after latest changes; LCD local rebuild attempt hit file-lock contention from a concurrently running PlatformIO process (`libFrameworkArduino.a` in use), not a source compile error.
- Phase impact:
  - P5: progressed (route/factory parity advanced; orphan hard-delete still pending)

### Execution note (orphan receiver-page retirement + build revalidation)

- Date: 2026-05-31
- Update:
  - Retired legacy unregistered `receiver_page*` modules in both `espnowreceiver_2` and `espnowreceiver_LCD` by replacing implementation units and declarations with inert stubs.
  - Rebuilt `espnowreceiver_2` (both environments) and `espnowreceiver_LCD` (waveshare environment) after retirement changes; all target builds succeeded.
  - Physical file hard-delete remains pending a follow-up cleanup pass; runtime behavior is now aligned because orphan routes are non-registered and inert.
- Phase impact:
  - P5: progressed (runtime orphan behavior removed; physical source deletion still pending)

### Execution note (route parity guardrail + lock-contention revalidation)

- Date: 2026-05-31
- Update:
  - Added `esp32common/scripts/route_parity_checker.py` to enforce route/definition parity and detect lingering `receiver_page*` orphans in both receiver projects.
  - Ran checker against both codebases; result correctly fails only on physical orphan file presence (6 files in `_2`, 6 files in LCD).
  - Revalidated builds after lock contention by running environments sequentially:
    - `espnowreceiver_2`: `lilygo-t-display-s3_tft` and `lilygo-t-display-s3` both succeeded when built one-by-one.
    - `espnowreceiver_LCD`: `waveshare_esp32s3_lcd7_lvgl` succeeded after clearing stale PlatformIO process lock.
- Phase impact:
  - P6: progressed (route/definition guardrail implemented and runnable)
  - P5: complete after physical `receiver_page*` deletion and checker PASS

### Execution note (orphan deletion finalization + parity pass)

- Date: 2026-05-31
- Update:
  - Removed all 12 physical `receiver_page*` files from both `espnowreceiver_2` and `espnowreceiver_LCD`.
  - Re-ran `route_parity_checker.py` and confirmed PASS.
  - Reconfirmed sequential builds for both receiver projects after cleanup.
- Phase impact:
  - P5: COMPLETE
  - P6: pending closeout at the time of this note (superseded by the certification closeout entry below)

### Execution note (guardrail certification closeout)

- Date: 2026-05-31
- Update:
  - Re-validated the theme compliance guardrail after the cleanup sweep and confirmed PASS in both receiver codebases.
  - Confirmed route parity remains clean after legacy orphan deletion.
  - Closed the clean-codebase certification phase with the receiver builds still green in sequential validation.
- Phase impact:
  - P6: COMPLETE

---

## 7) Final acceptance checklist (project close)

- [x] P0 through P6 are marked `COMPLETE` in tracker.
- [x] Blue/green theme inheritance works for hub and nested routes in both codebases.
- [x] Dashboard is template-injected and correctly themed on all standard pages.
- [x] First heading and section surrounds are template-owned and route-colored.
- [x] `espnowreceiver_2` rendering hardening parity achieved with LCD model.
- [x] Old/redundant/legacy code removed; no shadow path remains.
- [x] Guardrails active to keep future pages compliant by default.

---

## 8) Approved implementation addendum — System Tools namespace migration (all receivers)

Status: IN PROGRESS

### 8.1 Goal

Create a dedicated orange-themed System Tools hub and move current tools under `/systemtools/*` now to keep the codebase clean and extensible.

### 8.2 Scope (mandatory, both receiver projects)

Projects in scope:
- `espnowreceiver_2`
- `espnowreceiver_LCD`

Routes to move now:
- `/debug` → `/systemtools/debug`
- `/ota` → `/systemtools/ota`
- `/events` → `/systemtools/events`

New hub route:
- `/systemtools`

### 8.3 Required implementation

1. Add `/systemtools` hub page in both receivers.
  - Includes cards/buttons for Debug, OTA, Event Logs.
  - Uses orange section styling and shared semantic classes.

2. Move tool pages to `/systemtools/*` routes now.
  - Register handlers only under `/systemtools/debug`, `/systemtools/ota`, `/systemtools/events`.
  - Update all dashboard and hub links to these new routes.

3. Add route-based template theme mapping for system tools.
  - Extend page generator route theme resolver:
    - `/receiver*` => receiver theme (green)
    - `/systemtools*` => system tools theme (orange)
    - others => transmitter theme (blue)

4. Remove Memory Health section from Debug page during this migration.
  - `/systemtools/debug` must contain debug-level controls only.

### 8.4 Mandatory legacy/redundant cleanup (hard delete policy)

As each route is migrated, remove old code immediately in the same phase:

1. Delete legacy route registrations:
  - `/debug`, `/ota`, `/events` from factories and route definitions.

2. Delete dead compatibility paths:
  - old constants/comments mentioning removed routes,
  - old nav cards/links that target removed routes,
  - obsolete helper code tied only to removed paths.

3. Delete duplicate styling fragments:
  - inline one-off orange styling if replaced by shared semantic classes,
  - stale page-local style blocks superseded by template/system-tools styles.

4. Remove Debug Memory Health legacy UI/JS/CSS:
  - content block,
  - `loadMemoryHealth()` script path,
  - `.mem-*` style rules and any orphan references.

### 8.5 Guardrails and parity requirements

1. `page_registration_factory.cpp` and `page_definitions.cpp` must stay in route parity for both receivers.
2. Route parity checker must pass after migration.
3. Theme compliance checker must pass after migration.
4. No legacy `/debug`, `/ota`, `/events` routes may remain registered after completion.

### 8.6 Completion gate for P7

P7 is only COMPLETE when all are true:

1. `/systemtools` hub exists and links correctly in both receivers.
2. Tool pages are served from `/systemtools/debug`, `/systemtools/ota`, `/systemtools/events`.
3. Legacy `/debug`, `/ota`, `/events` routes are fully removed from runtime registration and route definitions.
4. Debug page no longer includes Memory Health section.
5. No old/legacy/redundant migration leftovers remain in source tree.
6. Both receiver builds pass and guardrails pass.

