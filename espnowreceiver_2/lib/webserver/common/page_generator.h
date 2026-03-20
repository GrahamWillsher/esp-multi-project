#ifndef PAGE_GENERATOR_H
#define PAGE_GENERATOR_H

#include <Arduino.h>

struct PageRenderOptions {
	String extra_styles;
	String script;

	PageRenderOptions(const String& styles = String(), const String& script_content = String())
		: extra_styles(styles), script(script_content) {}
};

// Generate standard HTML page with common template using typed options.
String renderPage(const String& title, const String& content, const PageRenderOptions& options = PageRenderOptions());

#endif
