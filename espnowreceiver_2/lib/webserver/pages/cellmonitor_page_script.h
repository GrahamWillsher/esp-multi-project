#pragma once

#include <Arduino.h>

/**
 * @brief Generate the JavaScript for the Cell Monitor page.
 *
 * Returns the full script block (without <script> tags) for the cell
 * monitor: SSE connection to /api/cell_stream, cell grid rendering,
 * voltage distribution bar chart, and bi-directional hover highlighting.
 *
 * @return String containing the JavaScript.
 */
String get_cellmonitor_page_script();
