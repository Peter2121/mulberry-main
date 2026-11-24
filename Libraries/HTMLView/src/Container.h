/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

// Mulberry's litehtml container.  Backgrounds, borders, gradients, list
// markers and images are drawn by litehtml's own Cairo container
// (containers/cairo/container_cairo.cpp, unchanged); fonts and text are ours
// (Fonts.h: fontconfig + HarfBuzz + Cairo).

#ifndef HTMLVIEW_CONTAINER_H
#define HTMLVIEW_CONTAINER_H

#include "htmlview/Document.h"
#include "Fonts.h"

#include "container_cairo.h"

#include <map>
#include <string>
#include <vector>

namespace htmlview
{

class Container : public container_cairo
{
public:
	Container(const Settings& settings, Host* host);
	~Container() override;

	void SetViewportWidth(int width) { mViewportWidth = width; }

	// Distinct remote image URLs (http, https, ...) the document refers to:
	// not loaded
	int  GetBlockedImages() const { return mBlockedImages; }

	// Fonts and text
	litehtml::uint_ptr	create_font(const litehtml::font_description& descr, const litehtml::document* doc,
									litehtml::font_metrics* fm) override;
	void				delete_font(litehtml::uint_ptr hFont) override;
	litehtml::pixel_t	text_width(const char* text, litehtml::uint_ptr hFont) override;
	void				draw_text(litehtml::uint_ptr hdc, const char* text, litehtml::uint_ptr hFont,
								  litehtml::web_color color, const litehtml::position& pos) override;
	litehtml::pixel_t	get_default_font_size() const override;
	const char*			get_default_font_name() const override;
	void				transform_text(std::string& text, litehtml::text_transform tt) override;

	// Images: data: URLs, and those the host finds (in the mail: cid:, names);
	// remote ones are never loaded
	cairo_surface_t*	get_image(const std::string& url) override;
	void				load_image(const char* src, const char* baseurl, bool redraw_on_ready) override;

	// Screen
	double				get_screen_dpi() const override;
	int					get_screen_width() const override;
	int					get_screen_height() const override;
	void				get_viewport(litehtml::position& viewport) const override;
	void				get_language(std::string& language, std::string& culture) const override;

	// Document and links
	void				set_caption(const char* caption) override;
	void				set_base_url(const char* base_url) override;
	void				on_anchor_click(const char* url, const litehtml::element::ptr& el) override;
	void				on_mouse_event(const litehtml::element::ptr& el, litehtml::mouse_event event) override;
	void				set_cursor(const char* cursor) override;

	// Remote content is never loaded: external style sheets are ignored
	void				import_css(std::string& text, const std::string& url,
								   std::string& baseurl) override;

private:
	Settings				mSettings;
	Host*					mHost;
	int						mViewportWidth = 800;
	std::vector<GlyphRun>	mRuns;			// draw_text's scratch space
	std::vector<cairo_glyph_t> mGlyphs;

	// Decoded images by URL; nullptr: not available (cached too, so it is
	// looked for once)
	std::map<std::string, cairo_surface_t*> mImages;
	int						mBlockedImages = 0;

	cairo_surface_t*	LoadImage(const std::string& url);
};

}	// namespace htmlview

#endif
