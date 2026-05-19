#pragma once

#include <Arduino.h>

/**
 * @brief Generate the JavaScript for the Cell Monitor page.
 *
 * Returns the full script block (without <script> tags) for the cell
 * monitor: 5-second snapshot polling from /api/cell_data (with backoff),
 * cell grid rendering, voltage distribution bar chart, and bi-directional
 * hover highlighting.
 *
 * @return Pointer to static JavaScript payload.
 */
const char* get_cellmonitor_page_script();
