#ifndef PAGE_REGISTRATION_FACTORY_H
#define PAGE_REGISTRATION_FACTORY_H

#include <esp_http_server.h>
#include "page_definitions.h"

/**
 * Page registration factory - consolidates page handler registration boilerplate.
 * 
 * Usage in webserver initialization:
 *   PageRegistrationFactory::register_all_pages(server);
 */

namespace PageRegistrationFactory {

/**
 * Register all page handlers in a single call.
 * Returns the number of successfully registered handlers.
 */
int register_all_pages(httpd_handle_t server);

/**
 * Get expected page handler count (useful for verification).
 */
int get_expected_page_handler_count();

} // namespace PageRegistrationFactory

#endif // PAGE_REGISTRATION_FACTORY_H
