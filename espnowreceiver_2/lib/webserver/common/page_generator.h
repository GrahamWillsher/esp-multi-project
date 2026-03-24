#ifndef PAGE_GENERATOR_H
#define PAGE_GENERATOR_H

#include <Arduino.h>
#include <esp_http_server.h>

struct PageRenderOptions {
	String extra_styles;
	String script;
	bool include_common_script_helpers;

	PageRenderOptions(const String& styles = String(),
					 const String& script_content = String(),
					 bool include_helpers = true)
		: extra_styles(styles), script(script_content), include_common_script_helpers(include_helpers) {}
};

// Generate standard HTML page with common template using typed options.
String renderPage(const String& title, const String& content, const PageRenderOptions& options = PageRenderOptions());

// Render and send a standard HTML page with consistent content-type handling.
esp_err_t send_rendered_page(httpd_req_t* req,
							 const String& title,
							 const String& content,
							 const PageRenderOptions& options = PageRenderOptions(),
							 const char* content_type = "text/html");

#endif
