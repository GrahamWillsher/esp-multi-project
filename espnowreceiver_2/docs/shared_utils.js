/**
 * @file shared_utils.js
 * @brief Shared JavaScript utilities for web interface pages
 *
 * This module contains reusable functions used across multiple pages to avoid duplication
 * and ensure consistent behavior throughout the web interface.
 */

/**
 * Format a number to fixed decimal places
 * @param {number} value - The value to format
 * @param {number} decimals - Number of decimal places (default 1)
 * @returns {string} Formatted number as string
 */
function formatNumber(value, decimals = 1) {
    if (value === null || value === undefined) return '--';
    const num = parseFloat(value);
    if (isNaN(num)) return '--';
    return num.toFixed(decimals);
}

/**
 * Format a number as currency
 * @param {number} value - The value to format
 * @returns {string} Formatted currency string
 */
function formatCurrency(value) {
    if (value === null || value === undefined) return '$0.00';
    const num = parseFloat(value);
    if (isNaN(num)) return '$0.00';
    return '$' + num.toFixed(2);
}

/**
 * Format a number as percentage
 * @param {number} value - The percentage value (0-100)
 * @returns {string} Formatted percentage string
 */
function formatPercent(value) {
    if (value === null || value === undefined) return '0%';
    const num = parseFloat(value);
    if (isNaN(num)) return '0%';
    return num.toFixed(1) + '%';
}

/**
 * Format a value with a unit suffix
 * @param {number} value - The numeric value
 * @param {string} unit - The unit to append (e.g., 'V', 'A', 'W')
 * @param {number} decimals - Number of decimal places (default 1)
 * @returns {string} Formatted value with unit
 */
function formatWithUnit(value, unit, decimals = 1) {
    if (value === null || value === undefined) return '-- ' + unit;
    const num = parseFloat(value);
    if (isNaN(num)) return '-- ' + unit;
    return num.toFixed(decimals) + ' ' + unit;
}

/**
 * Create a table row with given cell contents
 * @param {Array<string>} cells - Array of cell contents
 * @param {string} className - Optional CSS class for the row
 * @returns {HTMLTableRowElement} Created table row
 */
function createTableRow(cells, className = '') {
    const tr = document.createElement('tr');
    if (className) tr.className = className;
    
    cells.forEach(cell => {
        const td = document.createElement('td');
        td.textContent = cell;
        tr.appendChild(td);
    });
    
    return tr;
}

/**
 * Create a table header cell
 * @param {string} text - Header text
 * @param {string} className - Optional CSS class
 * @returns {HTMLTableCellElement} Created header cell
 */
function createTableHeader(text, className = '') {
    const th = document.createElement('th');
    th.textContent = text;
    if (className) th.className = className;
    return th;
}

/**
 * Update element text content with error handling
 * @param {string} elementId - ID of the element to update
 * @param {string} text - Text content to set
 */
function updateElementText(elementId, text) {
    const elem = document.getElementById(elementId);
    if (elem) {
        elem.textContent = text;
    }
}

/**
 * Update element HTML content with error handling
 * @param {string} elementId - ID of the element to update
 * @param {string} html - HTML content to set
 */
function updateElementHTML(elementId, html) {
    const elem = document.getElementById(elementId);
    if (elem) {
        elem.innerHTML = html;
    }
}

/**
 * Add or remove CSS class from element
 * @param {string} elementId - ID of the element
 * @param {string} className - CSS class name
 * @param {boolean} add - True to add class, false to remove
 */
function toggleClass(elementId, className, add) {
    const elem = document.getElementById(elementId);
    if (elem) {
        if (add) {
            elem.classList.add(className);
        } else {
            elem.classList.remove(className);
        }
    }
}

/**
 * Parse JSON with error handling
 * @param {string} jsonText - JSON text to parse
 * @param {*} defaultValue - Value to return if parsing fails
 * @returns {*} Parsed object or defaultValue
 */
function safeParseJSON(jsonText, defaultValue = null) {
    try {
        return JSON.parse(jsonText);
    } catch (err) {
        console.error('JSON parse error:', err);
        return defaultValue;
    }
}

/**
 * Fetch URL with error handling
 * @param {string} url - URL to fetch
 * @param {function} callback - Function to call with response
 * @param {function} errorCallback - Function to call on error
 */
function safeFetch(url, callback, errorCallback = null) {
    fetch(url)
        .then(response => {
            if (!response.ok) {
                throw new Error(`HTTP error! status: ${response.status}`);
            }
            return response.json();
        })
        .then(data => {
            if (callback) callback(data);
        })
        .catch(error => {
            console.error('Fetch error:', error);
            if (errorCallback) {
                errorCallback(error);
            } else {
                alert('Failed to fetch data: ' + error.message);
            }
        });
}

/**
 * Send a POST request with JSON data
 * @param {string} url - URL endpoint
 * @param {*} data - Data to send (will be JSON stringified)
 * @param {function} callback - Success callback
 * @param {function} errorCallback - Error callback
 */
function safeFetchPost(url, data, callback, errorCallback = null) {
    fetch(url, {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json'
        },
        body: JSON.stringify(data)
    })
        .then(response => {
            if (!response.ok) {
                throw new Error(`HTTP error! status: ${response.status}`);
            }
            return response.json();
        })
        .then(responseData => {
            if (callback) callback(responseData);
        })
        .catch(error => {
            console.error('POST error:', error);
            if (errorCallback) {
                errorCallback(error);
            } else {
                alert('Request failed: ' + error.message);
            }
        });
}

/**
 * Debounce a function call
 * @param {function} func - Function to debounce
 * @param {number} delay - Delay in milliseconds
 * @returns {function} Debounced function
 */
function debounce(func, delay = 300) {
    let timeoutId = null;
    return function debounced(...args) {
        clearTimeout(timeoutId);
        timeoutId = setTimeout(() => {
            func.apply(this, args);
        }, delay);
    };
}

/**
 * Convert time in milliseconds to readable format
 * @param {number} ms - Time in milliseconds
 * @returns {string} Readable time format (e.g., "1h 23m 45s")
 */
function formatTime(ms) {
    if (ms < 0) ms = 0;
    
    const seconds = Math.floor((ms / 1000) % 60);
    const minutes = Math.floor((ms / (1000 * 60)) % 60);
    const hours = Math.floor((ms / (1000 * 60 * 60)) % 24);
    const days = Math.floor(ms / (1000 * 60 * 60 * 24));
    
    let result = '';
    if (days > 0) result += days + 'd ';
    if (hours > 0 || days > 0) result += hours + 'h ';
    if (minutes > 0 || hours > 0 || days > 0) result += minutes + 'm ';
    result += seconds + 's';
    
    return result;
}

/**
 * Check if a value is within acceptable range
 * @param {number} value - The value to check
 * @param {number} min - Minimum value (inclusive)
 * @param {number} max - Maximum value (inclusive)
 * @returns {boolean} True if value is in range
 */
function isInRange(value, min, max) {
    const num = parseFloat(value);
    return !isNaN(num) && num >= min && num <= max;
}

/**
 * Clamp a value between min and max
 * @param {number} value - Value to clamp
 * @param {number} min - Minimum value
 * @param {number} max - Maximum value
 * @returns {number} Clamped value
 */
function clamp(value, min, max) {
    const num = parseFloat(value);
    if (isNaN(num)) return min;
    if (num < min) return min;
    if (num > max) return max;
    return num;
}

/**
 * Map a value from one range to another
 * @param {number} value - Value to map
 * @param {number} fromMin - Source minimum
 * @param {number} fromMax - Source maximum
 * @param {number} toMin - Target minimum
 * @param {number} toMax - Target maximum
 * @returns {number} Mapped value
 */
function mapValue(value, fromMin, fromMax, toMin, toMax) {
    return (value - fromMin) * (toMax - toMin) / (fromMax - fromMin) + toMin;
}

/**
 * Hide element
 * @param {string} elementId - ID of element to hide
 */
function hideElement(elementId) {
    const elem = document.getElementById(elementId);
    if (elem) elem.style.display = 'none';
}

/**
 * Show element
 * @param {string} elementId - ID of element to show
 * @param {string} display - CSS display value (default 'block')
 */
function showElement(elementId, display = 'block') {
    const elem = document.getElementById(elementId);
    if (elem) elem.style.display = display;
}

/**
 * Check if element is visible
 * @param {string} elementId - ID of element to check
 * @returns {boolean} True if element is visible
 */
function isElementVisible(elementId) {
    const elem = document.getElementById(elementId);
    if (!elem) return false;
    return elem.style.display !== 'none' && window.getComputedStyle(elem).display !== 'none';
}

/**
 * Log message with timestamp
 * @param {string} message - Message to log
 * @param {string} level - Log level ('info', 'warn', 'error')
 */
function logMessage(message, level = 'info') {
    const timestamp = new Date().toLocaleTimeString();
    const prefix = `[${timestamp}] [${level.toUpperCase()}]`;
    
    switch (level) {
        case 'warn':
            console.warn(prefix, message);
            break;
        case 'error':
            console.error(prefix, message);
            break;
        default:
            console.log(prefix, message);
    }
}

// Export for use (if using modules)
// export { formatNumber, formatCurrency, createTableRow, safeFetch, ... };
