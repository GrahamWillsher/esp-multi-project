#ifndef PAGE_GENERATOR_H
#define PAGE_GENERATOR_H

#include <Arduino.h>
#include <esp_http_server.h>

struct PageRenderOptions {
	String extra_styles;
	String script;
	const char* extra_styles_static;
	const char* script_static;
	bool include_common_script_helpers;
	bool include_template_dashboard_nav;

	PageRenderOptions(const String& styles = String(),
					 const String& script_content = String(),
					 bool include_helpers = true,
					 bool include_dashboard_nav = true)
		: extra_styles(styles),
		  script(script_content),
		  extra_styles_static(nullptr),
		  script_static(nullptr),
		  include_common_script_helpers(include_helpers),
		  include_template_dashboard_nav(include_dashboard_nav) {}

	PageRenderOptions(const char* styles,
					 const char* script_content,
					 bool include_helpers = true,
					 bool include_dashboard_nav = true)
		: extra_styles(),
		  script(),
		  extra_styles_static(styles),
		  script_static(script_content),
		  include_common_script_helpers(include_helpers),
		  include_template_dashboard_nav(include_dashboard_nav) {}
};

// Streaming render: callback-based content generation
// Handler provides a callback that emits page content incrementally without building it all in a String.
// Callback receives an httpd_req_t and should return ESP_OK on success, ESP_FAIL/ESP_ERR_NO_MEM on error or abort.
typedef esp_err_t (*page_content_generator_t)(httpd_req_t* req);

// Sends page body content through the same guarded/adaptive chunk pipeline used
// by send_rendered_page_streaming().
//
// Use this inside page content generators instead of calling
// httpd_resp_send_chunk() directly so pressure checks, adaptive chunk sizing,
// and request accounting stay consistent.
esp_err_t send_page_content_chunk(httpd_req_t* req,
								  const char* stage,
								  const char* data,
								  size_t len);

// Render and send a page with streaming content generation
// The callback is invoked to emit the page body after the HTML head is sent.
// This path avoids building the full page body in a temporary String.
esp_err_t send_rendered_page_streaming(httpd_req_t* req,
										const char* title,
										page_content_generator_t content_generator,
										const PageRenderOptions& options = PageRenderOptions(),
										const char* content_type = "text/html");

// Phase 2: Register /static/helpers.js endpoint (cacheable, served from flash)
esp_err_t register_static_helpers_js(httpd_handle_t server);

#endif
