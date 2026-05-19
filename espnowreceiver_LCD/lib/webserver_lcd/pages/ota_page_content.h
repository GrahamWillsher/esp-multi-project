#ifndef OTA_PAGE_CONTENT_H
#define OTA_PAGE_CONTENT_H

// Returns a pointer to static flash-resident HTML content for the OTA page.
// No heap allocation; safe to call on every request.
const char* get_ota_page_content();

#endif // OTA_PAGE_CONTENT_H