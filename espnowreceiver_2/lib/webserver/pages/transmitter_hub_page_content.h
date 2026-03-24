#ifndef TRANSMITTER_HUB_PAGE_CONTENT_H
#define TRANSMITTER_HUB_PAGE_CONTENT_H

#include <Arduino.h>

String get_transmitter_hub_page_content(
    const String& device_subtitle,
    const String& status_color,
    const String& status_text,
    const String& ip_text,
    const String& version_text,
    const String& build_date
);

#endif
