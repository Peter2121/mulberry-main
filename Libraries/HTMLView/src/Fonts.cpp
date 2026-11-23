/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

// Fonts and text for the HTML viewer (stack S1), see Fonts.h.
//
// - Fonts: a CSS font description (family list, weight, style) becomes a
//   fontconfig pattern; FcFontSort gives the font and its fallback fonts,
//   best first.  Each font file is loaded once, for HarfBuzz (shaping, from
//   the file's tables) and for Cairo (drawing, through its FreeType back end,
//   which also applies fontconfig's rendering settings and synthetic bold or
//   oblique).
// - Fallback per character: text is split into runs, each drawn with the
//   first font covering its characters; marks, joiners and variation
//   selectors stay with the character before them.  Emoji prefer colour
//   fonts.
// - Shaping: each run is shaped by HarfBuzz with its script and direction
//   (litehtml gives one word at a time and has no bidi, so right-to-left
//   runs are reordered within the run only).
// - Positions are not hinted (Cairo's metric hinting is off): widths do not
//   depend on the hinting settings, and match what is drawn.

#include "Fonts.h"

#include <cairo-ft.h>
#include <hb-ot.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace htmlview
{

namespace
{

// HarfBuzz positions are in 26.6 fixed point (64 units per pixel)
const double cHBScale = 64.0;

// Synthetic bold widens glyphs by 1/24 em, as FreeType's
// FT_GlyphSlot_Embolden (which Cairo uses to draw them) does
const float cSyntheticBold = 1.0f / 24.0f;

// litehtml asks for the width of the same words again and again
const size_t cMaxWidthCache = 8192;

// Decodes one UTF-8 sequence at s[i] and moves i past it.  Invalid or
// truncated sequences give U+FFFD and move by one byte.
hb_codepoint_t DecodeUTF8(const unsigned char* s, size_t len, size_t& i)
{
	unsigned char c = s[i];
	if (c < 0x80)
	{
		i++;
		return c;
	}

	int more;
	hb_codepoint_t cp;
	if ((c & 0xE0) == 0xC0)
	{
		more = 1;
		cp = c & 0x1F;
	}
	else if ((c & 0xF0) == 0xE0)
	{
		more = 2;
		cp = c & 0x0F;
	}
	else if ((c & 0xF8) == 0xF0)
	{
		more = 3;
		cp = c & 0x07;
	}
	else
	{
		i++;
		return 0xFFFD;
	}

	if (i + more >= len)
	{
		i++;
		return 0xFFFD;
	}
	for (int k = 1; k <= more; k++)
	{
		unsigned char cc = s[i + k];
		if ((cc & 0xC0) != 0x80)
		{
			i++;
			return 0xFFFD;
		}
		cp = (cp << 6) | (cc & 0x3F);
	}
	i += more + 1;
	return cp;
}

// Invisible characters that belong to the character before them, whatever
// font covers them (HarfBuzz hides them when the font lacks them): zero
// width joiner, variation selectors, tags (flags of regions)
bool IsJoiner(hb_codepoint_t cp)
{
	return (cp == 0x200D) ||
		   ((cp >= 0xFE00) && (cp <= 0xFE0F)) ||
		   ((cp >= 0xE0000) && (cp <= 0xE01EF));
}

// Combining marks, emoji skin tone modifiers and the combining keycap: stay
// with their base character when its font has them
bool IsMark(hb_unicode_funcs_t* ufuncs, hb_codepoint_t cp)
{
	if (((cp >= 0x1F3FB) && (cp <= 0x1F3FF)) || (cp == 0x20E3))
		return true;
	switch (hb_unicode_general_category(ufuncs, cp))
	{
	case HB_UNICODE_GENERAL_CATEGORY_NON_SPACING_MARK:
	case HB_UNICODE_GENERAL_CATEGORY_SPACING_MARK:
	case HB_UNICODE_GENERAL_CATEGORY_ENCLOSING_MARK:
		return true;
	default:
		return false;
	}
}

// Characters shown as emoji (in colour) by default, without U+FE0F
// (Emoji_Presentation in Unicode's emoji-data.txt, simplified outside the
// Basic Multilingual Plane)
bool IsEmojiPresentation(hb_codepoint_t cp)
{
	if (cp >= 0x1F000)
	{
		if (cp <= 0x1F0FF)
			return (cp == 0x1F004) || (cp == 0x1F0CF);
		if (cp <= 0x1F1E5)
			return (cp == 0x1F18E) || ((cp >= 0x1F191) && (cp <= 0x1F19A));
		return cp <= 0x1FAFF;
	}

	static const hb_codepoint_t ranges[][2] = {
		{ 0x231A, 0x231B }, { 0x23E9, 0x23EC }, { 0x23F0, 0x23F0 }, { 0x23F3, 0x23F3 },
		{ 0x25FD, 0x25FE }, { 0x2614, 0x2615 }, { 0x2648, 0x2653 }, { 0x267F, 0x267F },
		{ 0x2693, 0x2693 }, { 0x26A1, 0x26A1 }, { 0x26AA, 0x26AB }, { 0x26BD, 0x26BE },
		{ 0x26C4, 0x26C5 }, { 0x26CE, 0x26CE }, { 0x26D4, 0x26D4 }, { 0x26EA, 0x26EA },
		{ 0x26F2, 0x26F3 }, { 0x26F5, 0x26F5 }, { 0x26FA, 0x26FA }, { 0x26FD, 0x26FD },
		{ 0x2705, 0x2705 }, { 0x270A, 0x270B }, { 0x2728, 0x2728 }, { 0x274C, 0x274C },
		{ 0x274E, 0x274E }, { 0x2753, 0x2755 }, { 0x2757, 0x2757 }, { 0x2795, 0x2797 },
		{ 0x27B0, 0x27B0 }, { 0x27BF, 0x27BF }, { 0x2B1B, 0x2B1C }, { 0x2B50, 0x2B50 },
		{ 0x2B55, 0x2B55 },
	};
	if ((cp < 0x231A) || (cp > 0x2B55))
		return false;
	for (const auto& r : ranges)
	{
		if (cp < r[0])
			return false;
		if (cp <= r[1])
			return true;
	}
	return false;
}

bool IsWeakScript(hb_script_t script)
{
	return (script == HB_SCRIPT_COMMON) || (script == HB_SCRIPT_INHERITED) ||
		   (script == HB_SCRIPT_UNKNOWN) || (script == HB_SCRIPT_INVALID);
}

bool PatternBool(FcPattern* pattern, const char* object)
{
	FcBool value = FcFalse;
	return (FcPatternGetBool(pattern, object, 0, &value) == FcResultMatch) && value;
}

bool Covers(FcPattern* pattern, hb_codepoint_t cp)
{
	FcCharSet* charset = nullptr;
	return (FcPatternGetCharSet(pattern, FC_CHARSET, 0, &charset) == FcResultMatch) &&
		   FcCharSetHasChar(charset, cp);
}

std::string Lower(std::string s)
{
	for (auto& c : s)
		c = (char) std::tolower((unsigned char) c);
	return s;
}

// CSS generic family names and the fontconfig names for them.  An empty
// result: not a generic family.
std::string GenericFamily(const std::string& name)
{
	std::string lower = Lower(name);
	if ((lower == "serif") || (lower == "sans-serif") || (lower == "monospace") ||
		(lower == "cursive") || (lower == "fantasy") || (lower == "emoji") || (lower == "math"))
		return lower;
	if ((lower == "system-ui") || (lower == "-apple-system") || (lower == "blinkmacsystemfont") ||
		(lower == "ui-sans-serif") || (lower == "ui-rounded"))
		return "sans-serif";
	if (lower == "ui-serif")
		return "serif";
	if (lower == "ui-monospace")
		return "monospace";
	return std::string();
}

// The identity of a face for the cache: file, face index and what
// fontconfig asks to synthesise or vary
std::string FaceKey(FcPattern* pattern)
{
	std::string key;
	FcChar8* file = nullptr;
	if (FcPatternGetString(pattern, FC_FILE, 0, &file) == FcResultMatch)
		key = (const char*) file;
	int index = 0;
	FcPatternGetInteger(pattern, FC_INDEX, 0, &index);
	key += "|" + std::to_string(index);
	key += PatternBool(pattern, FC_EMBOLDEN) ? "|b" : "|";
	FcMatrix* matrix = nullptr;
	if (FcPatternGetMatrix(pattern, FC_MATRIX, 0, &matrix) == FcResultMatch)
		key += "|m" + std::to_string(matrix->xx) + "," + std::to_string(matrix->xy) + "," +
			   std::to_string(matrix->yx) + "," + std::to_string(matrix->yy);
	FcChar8* variations = nullptr;
	if (FcPatternGetString(pattern, FC_FONT_VARIATIONS, 0, &variations) == FcResultMatch)
		key += std::string("|v") + (const char*) variations;
	return key;
}

}	// namespace

// ---------------------------------------------------------------------------
// Objects
// ---------------------------------------------------------------------------

Face::~Face()
{
	if (cairo_face)
		cairo_font_face_destroy(cairo_face);
	if (hb_face)
		hb_face_destroy(hb_face);
	if (pattern)
		FcPatternDestroy(pattern);
}

SizedFace::~SizedFace()
{
	if (scaled_font)
		cairo_scaled_font_destroy(scaled_font);
	if (hb_font)
		hb_font_destroy(hb_font);
}

FontSet::~FontSet()
{
	if (set)
		FcFontSetDestroy(set);
	if (request)
		FcPatternDestroy(request);
}

// ---------------------------------------------------------------------------
// FontManager
// ---------------------------------------------------------------------------

FontManager& FontManager::Get()
{
	// Shared by all documents and never destroyed: the fonts stay loaded
	// for the next message
	static FontManager* sManager = new FontManager;
	return *sManager;
}

FontManager::FontManager()
{
	FcInit();
	mConfig = FcConfigReference(nullptr);

	mFontOptions = cairo_font_options_create();
	cairo_font_options_set_hint_metrics(mFontOptions, CAIRO_HINT_METRICS_OFF);

	mBuffer = hb_buffer_create();
	mUnicode = hb_unicode_funcs_get_default();
}

FontManager::~FontManager()
{
	mSized.clear();
	mFontSets.clear();
	mFaces.clear();
	if (mColorFallbacks)
		FcFontSetDestroy(mColorFallbacks);
	hb_buffer_destroy(mBuffer);
	cairo_font_options_destroy(mFontOptions);
	FcConfigDestroy(mConfig);
}

FontSet* FontManager::GetFontSet(const litehtml::font_description& descr, const std::string& default_family)
{
	std::string key = descr.family + "|" + std::to_string(descr.weight) + "|" +
					  std::to_string(descr.style) + "|" + default_family;
	auto found = mFontSets.find(key);
	if (found != mFontSets.end())
		return found->second.get();

	FcPattern* request = FcPatternCreate();

	// Families in CSS order, up to the first generic one (which always
	// matches something), then the default family
	bool generic = false;
	litehtml::string_vector tokens;
	litehtml::split_string(descr.family, tokens, ",");
	for (auto& token : tokens)
	{
		litehtml::trim(token, " \t\r\n\f\v\"\'");
		if (token.empty())
			continue;
		std::string name = GenericFamily(token);
		generic = !name.empty();
		if (!generic)
			name = token;
		FcPatternAddString(request, FC_FAMILY, (const FcChar8*) name.c_str());
		if (generic)
			break;
	}
	if (!generic)
	{
		std::string name = GenericFamily(default_family);
		FcPatternAddString(request, FC_FAMILY,
						   (const FcChar8*) (name.empty() ? default_family : name).c_str());
	}

	int weight = std::clamp(descr.weight, 1, 1000);
	FcPatternAddInteger(request, FC_WEIGHT, FcWeightFromOpenType(weight));
	FcPatternAddInteger(request, FC_SLANT,
						(descr.style == litehtml::font_style_italic) ? FC_SLANT_ITALIC : FC_SLANT_ROMAN);

	FcConfigSubstitute(mConfig, request, FcMatchPattern);
	FcDefaultSubstitute(request);

	FcResult result;
	FcFontSet* set = FcFontSort(mConfig, request, FcTrue, nullptr, &result);
	if (!set)
		set = FcFontSetCreate();

	auto fontset = std::make_unique<FontSet>();
	fontset->request = request;
	fontset->set = set;
	AddColorFallbacks(fontset.get());
	fontset->faces.assign(fontset->set->nfont, nullptr);
	fontset->resolved.assign(fontset->set->nfont, 0);

	FontSet* ret = fontset.get();
	mFontSets.emplace(key, std::move(fontset));
	return ret;
}

// Makes sure a colour (emoji) font is among the fallbacks
void FontManager::AddColorFallbacks(FontSet* fontset)
{
	for (int i = 0; i < fontset->set->nfont; i++)
	{
		if (PatternBool(fontset->set->fonts[i], FC_COLOR))
			return;
	}

	if (!mColorFallbacksLoaded)
	{
		mColorFallbacksLoaded = true;
		mColorFallbacks = FcFontSetCreate();

		FcPattern* pattern = FcPatternCreate();
		FcPatternAddString(pattern, FC_FAMILY, (const FcChar8*) "emoji");
		FcPatternAddBool(pattern, FC_COLOR, FcTrue);
		FcConfigSubstitute(mConfig, pattern, FcMatchPattern);
		FcDefaultSubstitute(pattern);
		FcResult result;
		FcFontSet* all = FcFontSort(mConfig, pattern, FcFalse, nullptr, &result);
		if (all)
		{
			for (int i = 0; (i < all->nfont) && (mColorFallbacks->nfont < 2); i++)
			{
				if (PatternBool(all->fonts[i], FC_COLOR))
					FcFontSetAdd(mColorFallbacks, FcPatternDuplicate(all->fonts[i]));
			}
			FcFontSetDestroy(all);
		}
		FcPatternDestroy(pattern);
	}

	for (int i = 0; i < mColorFallbacks->nfont; i++)
		FcFontSetAdd(fontset->set, FcPatternDuplicate(mColorFallbacks->fonts[i]));
}

Face* FontManager::GetFace(FontSet* fontset, int index)
{
	if ((index < 0) || (index >= fontset->set->nfont))
		return nullptr;
	if (fontset->resolved[index])
		return fontset->faces[index];

	fontset->resolved[index] = 1;
	FcPattern* render = FcFontRenderPrepare(mConfig, fontset->request, fontset->set->fonts[index]);
	if (!render)
		return nullptr;

	std::string key = FaceKey(render);
	auto found = mFaces.find(key);
	if (found != mFaces.end())
	{
		FcPatternDestroy(render);
		fontset->faces[index] = found->second.get();
	}
	else
	{
		Face* face = CreateFace(render);	// takes the pattern
		mFaces.emplace(key, std::unique_ptr<Face>(face));
		fontset->faces[index] = face;
	}
	return fontset->faces[index];
}

// Returns a face, unusable (no HarfBuzz or Cairo face) when the file cannot
// be loaded; it is cached anyway, so that the file is tried only once
Face* FontManager::CreateFace(FcPattern* render)
{
	auto face = new Face;
	face->pattern = render;
	FcPatternGetCharSet(render, FC_CHARSET, 0, &face->charset);
	face->color = PatternBool(render, FC_COLOR);
	face->embolden = PatternBool(render, FC_EMBOLDEN);

	FcChar8* file = nullptr;
	if (FcPatternGetString(render, FC_FILE, 0, &file) != FcResultMatch)
		return face;
	int index = 0;
	FcPatternGetInteger(render, FC_INDEX, 0, &index);

	hb_blob_t* blob = hb_blob_create_from_file_or_fail((const char*) file);
	if (!blob)
		return face;
	face->hb_face = hb_face_create(blob, index & 0xFFFF);
	hb_blob_destroy(blob);
	face->named_instance = (unsigned) index >> 16;

	FcChar8* variations = nullptr;
	if (FcPatternGetString(render, FC_FONT_VARIATIONS, 0, &variations) == FcResultMatch)
	{
		litehtml::string_vector items;
		litehtml::split_string((const char*) variations, items, ",");
		for (const auto& item : items)
		{
			hb_variation_t variation;
			if (hb_variation_from_string(item.c_str(), (int) item.size(), &variation))
				face->variations.push_back(variation);
		}
	}

	// Cairo opens the file itself and applies the pattern's rendering
	// options (antialiasing, hinting, synthetic bold or oblique)
	face->cairo_face = cairo_ft_font_face_create_for_pattern(render);
	if (cairo_font_face_status(face->cairo_face) != CAIRO_STATUS_SUCCESS)
	{
		cairo_font_face_destroy(face->cairo_face);
		face->cairo_face = nullptr;
	}

	if (!face->cairo_face)
	{
		hb_face_destroy(face->hb_face);
		face->hb_face = nullptr;
	}
	return face;
}

SizedFace* FontManager::GetSized(Face* face, double size)
{
	auto key = std::make_pair(face, std::lround(size * cHBScale));
	auto found = mSized.find(key);
	if (found != mSized.end())
		return found->second.get();

	auto sized = std::make_unique<SizedFace>();
	sized->face = face;
	sized->size = size;

	int scale = (int) std::lround(size * cHBScale);
	sized->hb_font = hb_font_create(face->hb_face);
	hb_font_set_scale(sized->hb_font, scale, scale);
	if (face->named_instance)
		hb_font_set_var_named_instance(sized->hb_font, face->named_instance - 1);
	if (!face->variations.empty())
		hb_font_set_variations(sized->hb_font, face->variations.data(), (unsigned) face->variations.size());
	if (face->embolden)
		hb_font_set_synthetic_bold(sized->hb_font, cSyntheticBold, cSyntheticBold, false);

	cairo_matrix_t font_matrix;
	cairo_matrix_t ctm;
	cairo_matrix_init_scale(&font_matrix, size, size);
	cairo_matrix_init_identity(&ctm);
	sized->scaled_font = cairo_scaled_font_create(face->cairo_face, &font_matrix, &ctm, mFontOptions);
	if (cairo_scaled_font_status(sized->scaled_font) != CAIRO_STATUS_SUCCESS)
	{
		cairo_scaled_font_destroy(sized->scaled_font);
		sized->scaled_font = nullptr;
	}

	SizedFace* ret = sized.get();
	mSized.emplace(key, std::move(sized));
	return ret;
}

// The first usable font covering cp; -1 if there is none
int FontManager::ChooseFace(FontSet* fontset, hb_codepoint_t cp, bool prefer_color, bool prefer_text)
{
	FcFontSet* set = fontset->set;

	if (prefer_color)
	{
		for (int i = 0; i < set->nfont; i++)
		{
			if (PatternBool(set->fonts[i], FC_COLOR) && Covers(set->fonts[i], cp) && GetFace(fontset, i) &&
				fontset->faces[i]->hb_face)
				return i;
		}
	}

	for (int pass = 0; pass < 2; pass++)
	{
		for (int i = 0; i < set->nfont; i++)
		{
			// Text presentation asked for (U+FE0E): colour fonts last
			if (prefer_text && ((pass == 0) == PatternBool(set->fonts[i], FC_COLOR)))
				continue;
			if (Covers(set->fonts[i], cp) && GetFace(fontset, i) && fontset->faces[i]->hb_face)
				return i;
		}
		if (!prefer_text)
			break;
	}
	return -1;
}

Font* FontManager::CreateFont(const litehtml::font_description& descr, const std::string& default_family,
							  const litehtml::document* doc, litehtml::font_metrics* fm)
{
	auto font = new Font;
	font->fontset = GetFontSet(descr, default_family);
	double requested = (float) descr.size;
	font->size = (requested > 0) ? requested : 16.0;
	double size = font->size;

	// Defaults, for when no font can be loaded at all
	font->ascent = std::round(size * 0.8);
	font->descent = std::round(size * 0.2);
	font->height = font->ascent + font->descent;
	font->x_height = std::round(size * 0.5);
	font->ch_width = std::round(size * 0.5);
	font->underline_position = std::max(1.0, std::round(size / 10.0));
	font->underline_thickness = std::max(1.0, std::round(size / 14.0));
	font->strikeout_position = std::round(size * 0.3);
	font->strikeout_thickness = font->underline_thickness;

	Face* primary = nullptr;
	for (int i = 0; (i < font->fontset->set->nfont) && !primary; i++)
	{
		Face* face = GetFace(font->fontset, i);
		if (face && face->hb_face)
			primary = face;
	}

	if (primary)
	{
		hb_font_t* hb_font = GetSized(primary, size)->hb_font;

		hb_font_extents_t extents;
		if (hb_font_get_h_extents(hb_font, &extents) && (extents.ascender - extents.descender > 0))
		{
			double ascent = extents.ascender / cHBScale;
			double descent = -extents.descender / cHBScale;
			double gap = std::max(0.0, extents.line_gap / cHBScale);
			font->ascent = std::round(ascent);
			font->descent = std::round(descent);
			font->height = std::max(font->ascent + font->descent, std::round(ascent + descent + gap));
		}
		else
			font->height = font->ascent + font->descent;

		hb_position_t value = 0;
		if (hb_ot_metrics_get_position(hb_font, HB_OT_METRICS_TAG_X_HEIGHT, &value) && (value > 0))
			font->x_height = std::round(value / cHBScale);
		else
		{
			hb_codepoint_t glyph;
			hb_glyph_extents_t glyph_extents;
			if (hb_font_get_nominal_glyph(hb_font, 'x', &glyph) &&
				hb_font_get_glyph_extents(hb_font, glyph, &glyph_extents) && (glyph_extents.y_bearing > 0))
				font->x_height = std::round(glyph_extents.y_bearing / cHBScale);
		}

		hb_codepoint_t zero;
		if (hb_font_get_nominal_glyph(hb_font, '0', &zero))
			font->ch_width = hb_font_get_glyph_h_advance(hb_font, zero) / cHBScale;

		hb_ot_metrics_get_position_with_fallback(hb_font, HB_OT_METRICS_TAG_UNDERLINE_OFFSET, &value);
		font->underline_position = std::max(1.0, std::round(-value / cHBScale));
		hb_ot_metrics_get_position_with_fallback(hb_font, HB_OT_METRICS_TAG_UNDERLINE_SIZE, &value);
		font->underline_thickness = std::max(1.0, std::round(value / cHBScale));
		hb_ot_metrics_get_position_with_fallback(hb_font, HB_OT_METRICS_TAG_STRIKEOUT_OFFSET, &value);
		font->strikeout_position = std::round(value / cHBScale);
		hb_ot_metrics_get_position_with_fallback(hb_font, HB_OT_METRICS_TAG_STRIKEOUT_SIZE, &value);
		font->strikeout_thickness = std::max(1.0, std::round(value / cHBScale));
	}
	font->overline_thickness = font->underline_thickness;

	font->underline = (descr.decoration_line & litehtml::text_decoration_line_underline) != 0;
	font->overline = (descr.decoration_line & litehtml::text_decoration_line_overline) != 0;
	font->strikeout = (descr.decoration_line & litehtml::text_decoration_line_line_through) != 0;
	font->decoration_style = descr.decoration_style;
	font->decoration_color = descr.decoration_color;

	if (fm)
	{
		// (float): litehtml's pixel_t is a float in 0.10, a class later
		fm->font_size = (float) size;
		fm->ascent = (float) font->ascent;
		fm->descent = (float) font->descent;
		fm->height = (float) font->height;
		fm->x_height = (float) font->x_height;
		fm->ch_width = (float) font->ch_width;
		fm->draw_spaces = (descr.decoration_line != litehtml::text_decoration_line_none);
		fm->sub_shift = (float) (size / 5);
		fm->super_shift = (float) (size / 3);

		// text-decoration-thickness given in CSS (else the font's own)
		if (doc && !descr.decoration_thickness.is_predefined())
		{
			litehtml::css_length thickness = descr.decoration_thickness;
			litehtml::css_length one_em(1.0, litehtml::css_units_em);
			doc->cvt_units(one_em, *fm, 0);
			doc->cvt_units(thickness, *fm, one_em.val());
			double value = std::max(1.0, (double) std::round(thickness.val()));
			font->underline_thickness = font->strikeout_thickness = font->overline_thickness = value;
		}
	}

	return font;
}

void FontManager::DeleteFont(Font* font)
{
	delete font;
}

void FontManager::Shape(Font* font, const char* text, std::vector<GlyphRun>& runs)
{
	runs.clear();
	size_t len = text ? std::strlen(text) : 0;
	if (!len)
		return;

	FontSet* fontset = font->fontset;
	const unsigned char* s = (const unsigned char*) text;

	// Code points and their byte offsets (with one more entry for the end)
	std::vector<std::pair<hb_codepoint_t, size_t>> cps;
	for (size_t i = 0; i < len; )
	{
		size_t at = i;
		hb_codepoint_t cp = DecodeUTF8(s, len, i);
		cps.emplace_back(cp, at);
	}
	cps.emplace_back(0, len);

	// Runs of one face and one script
	struct Item
	{
		size_t		start;
		size_t		end;
		int			face;
		hb_script_t	script;		// HB_SCRIPT_INVALID: none decided yet
	};
	std::vector<Item> items;

	int fallback = -1;	// face for characters no font has
	for (size_t k = 0; k + 1 < cps.size(); k++)
	{
		hb_codepoint_t cp = cps[k].first;
		hb_codepoint_t next = cps[k + 1].first;
		hb_script_t script = hb_unicode_script(mUnicode, cp);
		if (IsWeakScript(script))
			script = HB_SCRIPT_INVALID;

		int face = -1;
		if (!items.empty())
		{
			int current = items.back().face;
			if (IsJoiner(cp))
				face = current;
			else if (IsMark(mUnicode, cp) && Covers(fontset->set->fonts[current], cp))
				face = current;
		}
		if (face < 0)
			face = ChooseFace(fontset, cp, (next == 0xFE0F) || IsEmojiPresentation(cp), next == 0xFE0E);
		if (face < 0)
		{
			// Nothing covers it: shown as the font's "missing glyph"
			if (!items.empty())
				face = items.back().face;
			else
			{
				if (fallback < 0)
				{
					for (int i = 0; i < fontset->set->nfont; i++)
					{
						Face* f = GetFace(fontset, i);
						if (f && f->hb_face)
						{
							fallback = i;
							break;
						}
					}
				}
				face = fallback;
			}
		}
		if (face < 0)
			return;		// no usable font at all

		if (!items.empty() && (items.back().face == face) &&
			((script == HB_SCRIPT_INVALID) || (items.back().script == HB_SCRIPT_INVALID) ||
			 (items.back().script == script)))
		{
			items.back().end = cps[k + 1].second;
			if (items.back().script == HB_SCRIPT_INVALID)
				items.back().script = script;
		}
		else
			items.push_back(Item{ cps[k].second, cps[k + 1].second, face, script });
	}

	for (const auto& item : items)
	{
		SizedFace* sized = GetSized(fontset->faces[item.face], font->size);

		hb_buffer_clear_contents(mBuffer);
		hb_buffer_add_utf8(mBuffer, text, (int) len, (unsigned) item.start, (int) (item.end - item.start));
		if (item.script != HB_SCRIPT_INVALID)
		{
			hb_direction_t direction = hb_script_get_horizontal_direction(item.script);
			hb_buffer_set_script(mBuffer, item.script);
			hb_buffer_set_direction(mBuffer, (direction == HB_DIRECTION_INVALID) ? HB_DIRECTION_LTR : direction);
		}
		hb_buffer_guess_segment_properties(mBuffer);
		hb_shape(sized->hb_font, mBuffer, nullptr, 0);

		unsigned count = 0;
		hb_glyph_info_t* info = hb_buffer_get_glyph_infos(mBuffer, &count);
		hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(mBuffer, &count);

		GlyphRun run;
		run.sized = sized;
		run.glyphs.resize(count);
		double x = 0;
		for (unsigned i = 0; i < count; i++)
		{
			run.glyphs[i].index = info[i].codepoint;
			run.glyphs[i].x = x + pos[i].x_offset / cHBScale;
			run.glyphs[i].y = -pos[i].y_offset / cHBScale;
			x += pos[i].x_advance / cHBScale;
		}
		run.width = x;
		runs.push_back(std::move(run));
	}
}

double FontManager::TextWidth(Font* font, const char* text)
{
	auto found = font->widths.find(text);
	if (found != font->widths.end())
		return found->second;

	std::vector<GlyphRun> runs;
	Shape(font, text, runs);
	double width = 0;
	for (const auto& run : runs)
		width += run.width;

	if (font->widths.size() >= cMaxWidthCache)
		font->widths.clear();
	font->widths.emplace(text, width);
	return width;
}

}	// namespace htmlview
