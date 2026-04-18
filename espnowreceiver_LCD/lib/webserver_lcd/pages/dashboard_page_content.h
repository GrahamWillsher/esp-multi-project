#ifndef DASHBOARD_PAGE_CONTENT_H
#define DASHBOARD_PAGE_CONTENT_H

#include <Arduino.h>

String get_dashboard_page_content(const String& tx_status,
                                  const String& tx_status_color,
                                  const String& tx_ip,
                                  const String& tx_ip_mode,
                                  const String& tx_version,
                                  const String& tx_device_name,
                                  const String& tx_mac,
                                  const String& rx_ip,
                                  const String& rx_ip_mode,
                                  const String& rx_version,
                                  const String& rx_device_name,
                                  const String& rx_mac);

#endif // DASHBOARD_PAGE_CONTENT_H