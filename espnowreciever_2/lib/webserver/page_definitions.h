#ifndef PAGE_DEFINITIONS_H
#define PAGE_DEFINITIONS_H

#include <espnow_common.h>

// ═══════════════════════════════════════════════════════════════════════
// PAGE-TO-SUBTYPE MAPPING STRUCTURE
// ═══════════════════════════════════════════════════════════════════════
// Centralized page definition structure - enforces relationship between
// pages, buttons, and ESP-NOW subtypes. All pages MUST be defined here.
// 
// ─────────────────────────────────────────────────────────────────────
// HOW TO ADD A NEW PAGE WITH BUTTON:
// ─────────────────────────────────────────────────────────────────────
// 
// STEP 1: Add to PAGE_DEFINITIONS[] array in page_definitions.cpp
//   { "/mypage", "My Page Name", subtype_events, true }
//          ^            ^              ^          ^
//          |            |              |          |
//          URI       Button text    Subtype   Needs SSE?
//
// STEP 2: Create handler class in pages/mypage_page.h/cpp
// STEP 3: Register in init_webserver() (webserver.cpp)
// STEP 4: Handle subtype on transmitter side (main.cpp)
//
// ─────────────────────────────────────────────────────────────────────
// AUTOMATIC FEATURES:
// ─────────────────────────────────────────────────────────────────────
// ✓ Button automatically appears on ALL other pages
// ✓ Navigation is consistent everywhere
// ✓ If needs_sse=true, REQUEST_DATA/ABORT_DATA auto-use correct subtype
// ✓ Subtype lookup happens via get_subtype_for_uri()
// ✓ No hardcoded subtype values in SSE handlers

struct PageInfo {
    const char* uri;           // Page URI (e.g., "/", "/monitor")
    const char* name;          // Display name for button
    msg_subtype subtype;       // ESP-NOW subtype for REQUEST_DATA/ABORT_DATA
    bool needs_sse;            // True if page uses Server-Sent Events
};

// Central page registry - all pages defined in one place
extern const PageInfo PAGE_DEFINITIONS[];
extern const int PAGE_COUNT;

// Helper functions
msg_subtype get_subtype_for_uri(const char* uri);
bool uri_needs_sse(const char* uri);
const PageInfo* get_page_info(const char* uri);

#endif
