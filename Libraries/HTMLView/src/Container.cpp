/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

#include "Container.h"
#include "Images.h"
#include "TextTransform.h"

#include <algorithm>
#include <cmath>

namespace htmlview
{

namespace
{

// litehtml's pixel_t as a number: a float in litehtml 0.10, a class with
// conversions to float and int in later versions
inline double Px(litehtml::pixel_t value)
{
	return (double) (float) value;
}

// Which line of a text decoration: decides where a double line's second
// line goes
enum class LineKind
{
	Underline,		// second line below
	Overline,		// second line above
	Strikeout		// lines on both sides of the position
};

// Draws a text decoration line centred on y, in the current source colour
void DrawDecoration(cairo_t* cr, double x, double y, double width, double thickness,
					litehtml::text_decoration_style style, LineKind kind)
{
	if (width <= 0)
		return;

	double t = std::max(1.0, std::round(thickness));
	// Centre the line on the pixel grid, so that it stays sharp
	double yc = std::floor(y) + ((((int) t) % 2) ? 0.5 : 0.0);

	cairo_save(cr);
	cairo_new_path(cr);
	cairo_set_line_width(cr, t);

	switch (style)
	{
	case litehtml::text_decoration_style_double:
		{
			double y1 = yc;
			double y2 = yc;
			switch (kind)
			{
			case LineKind::Underline:
				y2 = yc + 2 * t;
				break;
			case LineKind::Overline:
				y2 = yc - 2 * t;
				break;
			case LineKind::Strikeout:
				y1 = yc - t;
				y2 = yc + t;
				break;
			}
			cairo_move_to(cr, x, y1);
			cairo_line_to(cr, x + width, y1);
			cairo_move_to(cr, x, y2);
			cairo_line_to(cr, x + width, y2);
		}
		break;

	case litehtml::text_decoration_style_dotted:
		{
			double dashes[] = { 0, 2 * t };
			cairo_set_dash(cr, dashes, 2, 0);
			cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
			cairo_move_to(cr, x + t / 2, yc);
			cairo_line_to(cr, x + width, yc);
		}
		break;

	case litehtml::text_decoration_style_dashed:
		{
			double dashes[] = { 3 * t, 2 * t };
			cairo_set_dash(cr, dashes, 2, 0);
			cairo_move_to(cr, x, yc);
			cairo_line_to(cr, x + width, yc);
		}
		break;

	case litehtml::text_decoration_style_wavy:
		{
			// Quarter waves of length 2t and height 1.5t, cut at the width
			double amplitude = 1.5 * t;
			double step = 2 * t;
			cairo_rectangle(cr, x, yc - amplitude - t, width, 2 * (amplitude + t));
			cairo_clip(cr);
			cairo_move_to(cr, x, yc);
			bool up = true;
			for (double px = x; px < x + width; px += step)
			{
				double peak = up ? yc - amplitude : yc + amplitude;
				cairo_curve_to(cr, px + step / 2, peak, px + step / 2, peak, px + step, yc);
				up = !up;
			}
		}
		break;

	default:
		cairo_move_to(cr, x, yc);
		cairo_line_to(cr, x + width, yc);
		break;
	}

	cairo_stroke(cr);
	cairo_restore(cr);
}

}	// namespace

Container::Container(const Settings& settings, Host* host)
	: mSettings(settings), mHost(host)
{
}

Container::~Container()
{
	clear_images();
	for (auto& image : mImages)
	{
		if (image.second)
			cairo_surface_destroy(image.second);
	}
}

// ---------------------------------------------------------------------------
// Fonts and text
// ---------------------------------------------------------------------------

litehtml::uint_ptr Container::create_font(const litehtml::font_description& descr, const litehtml::document* doc,
										  litehtml::font_metrics* fm)
{
	return (litehtml::uint_ptr) FontManager::Get().CreateFont(descr, mSettings.default_font_family, doc, fm);
}

void Container::delete_font(litehtml::uint_ptr hFont)
{
	FontManager::Get().DeleteFont((Font*) hFont);
}

litehtml::pixel_t Container::text_width(const char* text, litehtml::uint_ptr hFont)
{
	auto* font = (Font*) hFont;
	return font ? (float) FontManager::Get().TextWidth(font, text) : 0.0f;
}

void Container::draw_text(litehtml::uint_ptr hdc, const char* text, litehtml::uint_ptr hFont,
						  litehtml::web_color color, const litehtml::position& pos)
{
	auto* cr = (cairo_t*) hdc;
	auto* font = (Font*) hFont;
	if (!cr || !font || !text)
		return;

	FontManager::Get().Shape(font, text, mRuns);

	cairo_save(cr);
	apply_clip(cr);
	set_color(cr, color);

	// The text box's height is the font's: the baseline is its descent above
	// the bottom; on the pixel grid, for sharp text
	double left = Px(pos.x);
	double baseline = std::round(Px(pos.y) + Px(pos.height) - font->descent);
	double x = left;
	for (const auto& run : mRuns)
	{
		if (run.sized && run.sized->scaled_font && !run.glyphs.empty())
		{
			mGlyphs.assign(run.glyphs.begin(), run.glyphs.end());
			for (auto& glyph : mGlyphs)
			{
				glyph.x += x;
				glyph.y += baseline;
			}
			cairo_set_scaled_font(cr, run.sized->scaled_font);
			cairo_show_glyphs(cr, mGlyphs.data(), (int) mGlyphs.size());
		}
		x += run.width;
	}

	if (font->underline || font->overline || font->strikeout)
	{
		double width = x - left;
		set_color(cr, font->decoration_color.is_current_color ? color : font->decoration_color);
		if (font->underline)
			DrawDecoration(cr, left, baseline + font->underline_position, width, font->underline_thickness,
						   font->decoration_style, LineKind::Underline);
		if (font->strikeout)
			DrawDecoration(cr, left, baseline - font->strikeout_position, width, font->strikeout_thickness,
						   font->decoration_style, LineKind::Strikeout);
		if (font->overline)
			DrawDecoration(cr, left, baseline - font->ascent, width, font->overline_thickness,
						   font->decoration_style, LineKind::Overline);
	}

	cairo_restore(cr);
}

litehtml::pixel_t Container::get_default_font_size() const
{
	return (float) mSettings.default_font_size;
}

const char* Container::get_default_font_name() const
{
	return mSettings.default_font_family.c_str();
}

void Container::transform_text(std::string& text, litehtml::text_transform tt)
{
	TransformText(text, tt);
}

// ---------------------------------------------------------------------------
// Images
// ---------------------------------------------------------------------------

// The image for a URL, loaded and decoded the first time (the cache keeps
// the reference)
namespace
{

// litehtml passes attribute values as they are
std::string TrimURL(const std::string& url)
{
	size_t first = url.find_first_not_of(" \t\r\n\f");
	size_t last = url.find_last_not_of(" \t\r\n\f");
	return (first == std::string::npos) ? std::string() : url.substr(first, last - first + 1);
}

}	// namespace

cairo_surface_t* Container::LoadImage(const std::string& url, ImageUse use)
{
	std::string key = TrimURL(url);
	if (key.empty())
		return nullptr;

	auto found = mImages.find(key);
	if (found != mImages.end())
	{
		auto remote = mRemoteAffectsLayout.find(key);
		if ((remote != mRemoteAffectsLayout.end()) && (use == ImageUse::Layout))
			remote->second = true;
		return found->second;
	}

	std::string data;
	bool have_data = false;
	switch (ClassifyURL(key))
	{
	case URLKind::Data:
		have_data = DecodeDataURL(key, mSettings.max_image_bytes, data);
		break;
	case URLKind::Local:
		have_data = mHost && mHost->LoadImageData(key, data) && (data.size() <= mSettings.max_image_bytes);
		break;
	case URLKind::Remote:
		// Pending: the host may give it later (SetRemoteImage())
		mRemoteURLs.push_back(key);
		// Not known (first needed while drawing): assume the layout depends on it
		mRemoteAffectsLayout[key] = (use != ImageUse::Redraw);
		break;
	}

	cairo_surface_t* image = have_data ? DecodeImage(data, mSettings.max_image_pixels) : nullptr;
	mImages.emplace(key, image);
	return image;
}

cairo_surface_t* Container::get_image(const std::string& url)
{
	// The caller releases its reference
	cairo_surface_t* image = LoadImage(url, ImageUse::Lookup);
	return image ? cairo_surface_reference(image) : nullptr;
}

void Container::load_image(const char* src, const char* baseurl, bool redraw_on_ready)
{
	if (!src)
		return;
	std::string url;
	make_url(src, baseurl, url);
	LoadImage(url, redraw_on_ready ? ImageUse::Redraw : ImageUse::Layout);
}

int Container::SetRemoteImage(const std::string& url, const std::string& data)
{
	using Change = Document::ImageChange;

	std::string key = TrimURL(url);
	auto remote = mRemoteAffectsLayout.find(key);
	if ((remote == mRemoteAffectsLayout.end()) || (data.size() > mSettings.max_image_bytes))
		return (int) Change::NoChange;

	cairo_surface_t* image = DecodeImage(data, mSettings.max_image_pixels);
	if (!image)
		return (int) Change::NoChange;

	cairo_surface_t*& cached = mImages[key];
	if (cached)
		cairo_surface_destroy(cached);
	else
		mLoadedRemote++;
	cached = image;
	return (int) (remote->second ? Change::Relayout : Change::Redraw);
}

// ---------------------------------------------------------------------------
// Screen
// ---------------------------------------------------------------------------

double Container::get_screen_dpi() const
{
	return mSettings.dpi;
}

int Container::get_screen_width() const
{
	return mSettings.screen_width;
}

int Container::get_screen_height() const
{
	return mSettings.screen_height;
}

void Container::get_viewport(litehtml::position& viewport) const
{
	viewport.x = 0;
	viewport.y = 0;
	viewport.width = mViewportWidth;
	viewport.height = mSettings.screen_height;
}

void Container::get_language(std::string& language, std::string& culture) const
{
	language = mSettings.language;
	culture.clear();
}

// ---------------------------------------------------------------------------
// Document and links
// ---------------------------------------------------------------------------

void Container::set_caption(const char* caption)
{
}

void Container::set_base_url(const char* base_url)
{
}

void Container::on_anchor_click(const char* url, const litehtml::element::ptr& el)
{
	if (mHost && url)
		mHost->OnAnchorClick(url);
}

void Container::on_mouse_event(const litehtml::element::ptr& el, litehtml::mouse_event event)
{
}

void Container::set_cursor(const char* cursor)
{
	if (mHost && cursor)
		mHost->SetCursor(cursor);
}

void Container::import_css(std::string& text, const std::string& url, std::string& baseurl)
{
	text.clear();
}

}	// namespace htmlview
