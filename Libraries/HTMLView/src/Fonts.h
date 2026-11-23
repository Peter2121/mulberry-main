/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

// Fonts and text for the HTML viewer (stack S1): fontconfig chooses the
// fonts and the fallback fonts per character, HarfBuzz shapes, Cairo draws
// the glyphs.  No Pango, no GLib.

#ifndef HTMLVIEW_FONTS_H
#define HTMLVIEW_FONTS_H

#include <cairo.h>
#include <fontconfig/fontconfig.h>
#include <hb.h>
#include <litehtml.h>

#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace htmlview
{

// A font file (one face of it) as chosen by fontconfig, with its HarfBuzz
// and Cairo faces.  Shared by all documents for the life of the process.
struct Face
{
	FcPattern*			pattern = nullptr;		// render-prepared pattern (owned)
	FcCharSet*			charset = nullptr;		// coverage (belongs to pattern)
	bool				color = false;			// colour font (emoji)
	bool				embolden = false;		// synthetic bold
	unsigned			named_instance = 0;		// variable font instance + 1, 0: none
	std::vector<hb_variation_t> variations;		// variable font axes
	hb_face_t*			hb_face = nullptr;
	cairo_font_face_t*	cairo_face = nullptr;

	~Face();
};

// A face at one size
struct SizedFace
{
	Face*					face = nullptr;
	double					size = 0;
	hb_font_t*				hb_font = nullptr;
	cairo_scaled_font_t*	scaled_font = nullptr;

	~SizedFace();
};

// The fonts fontconfig proposes for a family list, weight and style, best
// first: the first one is the font itself, the others are its fallbacks.
// Shared by the fonts of all sizes with that description.
struct FontSet
{
	FcPattern*			request = nullptr;		// owned
	FcFontSet*			set = nullptr;			// owned
	std::vector<Face*>	faces;					// resolved lazily, nullptr: not yet / unusable
	std::vector<char>	resolved;

	~FontSet();
};

// A run of text drawn with one face: glyph positions relative to the start
// of the run (x) and to the baseline (y)
struct GlyphRun
{
	SizedFace*					sized = nullptr;
	std::vector<cairo_glyph_t>	glyphs;
	double						width = 0;
};

// What litehtml's create_font returns
struct Font
{
	FontSet*	fontset = nullptr;
	double		size = 0;

	// Metrics in pixels
	double		ascent = 0;
	double		descent = 0;
	double		height = 0;
	double		x_height = 0;
	double		ch_width = 0;

	// Text decoration
	bool		underline = false;
	bool		overline = false;
	bool		strikeout = false;
	litehtml::text_decoration_style decoration_style = litehtml::text_decoration_style_solid;
	litehtml::web_color decoration_color = litehtml::web_color::current_color;
	double		underline_position = 0;			// below the baseline
	double		underline_thickness = 1;
	double		strikeout_position = 0;			// above the baseline
	double		strikeout_thickness = 1;
	double		overline_thickness = 1;

	// text_width() cache: litehtml asks for the same words many times
	std::unordered_map<std::string, double> widths;
};

class FontManager
{
public:
	static FontManager& Get();

	Font*	CreateFont(const litehtml::font_description& descr, const std::string& default_family,
					   const litehtml::document* doc, litehtml::font_metrics* fm);
	void	DeleteFont(Font* font);

	double	TextWidth(Font* font, const char* text);

	// Splits text into runs of one face each (fallback per character) and
	// shapes them
	void	Shape(Font* font, const char* text, std::vector<GlyphRun>& runs);

private:
	FontManager();
	~FontManager();

	FontSet*	GetFontSet(const litehtml::font_description& descr, const std::string& default_family);
	Face*		GetFace(FontSet* fontset, int index);
	Face*		CreateFace(FcPattern* render_pattern);
	SizedFace*	GetSized(Face* face, double size);
	void		AddColorFallbacks(FontSet* fontset);
	int			ChooseFace(FontSet* fontset, hb_codepoint_t cp, bool prefer_color, bool prefer_text);

	FcConfig*				mConfig = nullptr;
	cairo_font_options_t*	mFontOptions = nullptr;
	hb_buffer_t*			mBuffer = nullptr;
	hb_unicode_funcs_t*		mUnicode = nullptr;

	std::map<std::string, std::unique_ptr<FontSet>>		mFontSets;
	std::map<std::string, std::unique_ptr<Face>>		mFaces;
	std::map<std::pair<Face*, long>, std::unique_ptr<SizedFace>> mSized;

	FcFontSet*	mColorFallbacks = nullptr;		// colour (emoji) fonts, best first
	bool		mColorFallbacksLoaded = false;
};

}	// namespace htmlview

#endif
