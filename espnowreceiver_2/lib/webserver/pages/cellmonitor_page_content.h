#pragma once

/**
 * @brief Return the static HTML body content for the Cell Monitor page.
 *
 * Returns a pointer to a string literal in flash — no heap allocation.
 * @return const char* pointing to the static HTML body (no <script> block).
 */
const char* get_cellmonitor_page_content();
