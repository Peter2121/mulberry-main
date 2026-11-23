/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

// htmlview::Document: Mulberry's HTML viewer core.
//
// Lays out and draws an HTML document with litehtml, Cairo and HarfBuzz
// (stack S1 in LITEHTML-VIEWER-PLAN.md).  It knows nothing about the GUI
// toolkit: the host gives it HTML, a width and a Cairo context to draw into,
// and receives callbacks through htmlview::Host.  No litehtml, Cairo or
// HarfBuzz header is needed to use this one.

#ifndef HTMLVIEW_DOCUMENT_H
#define HTMLVIEW_DOCUMENT_H

#include <cstddef>
#include <memory>
#include <string>

typedef struct _cairo cairo_t;

namespace htmlview
{

// Settings of a document, filled by the host (from its preferences)
struct Settings
{
	std::string	default_font_family = "sans-serif";	// when CSS names no installed family
	double		default_font_size = 16.0;			// pixels
	double		dpi = 96.0;
	int			screen_width = 1920;				// for CSS media queries
	int			screen_height = 1080;
	std::string	language = "en";

	// Limits against hostile input: larger or deeper documents are rejected
	// (Load() returns an error status) and the host shows them as text
	size_t		max_html_size = 16 * 1024 * 1024;	// bytes
	int			max_nesting_depth = 512;			// elements (as Chromium's parser)
	size_t		max_elements = 20000;				// about 12 KB and 30-60 us each
};

// A rectangle in pixels
struct Rect
{
	int x = 0;
	int y = 0;
	int width = 0;
	int height = 0;
};

// Callbacks to the host.  The defaults do nothing.
class Host
{
public:
	virtual ~Host() = default;

	virtual void OnAnchorClick(const std::string& url) {}
	virtual void SetCursor(const std::string& cursor) {}
};

class Document
{
public:
	enum class Result		// not "Status": Xlib defines it as a macro
	{
		Ok,
		Empty,			// no HTML given
		TooLarge,		// more than Settings::max_html_size bytes
		TooDeep,		// elements nested deeper than Settings::max_nesting_depth
		TooManyElements,	// more than Settings::max_elements
		Error			// litehtml could not create the document
	};

	explicit Document(const Settings& settings = Settings(), Host* host = nullptr);
	~Document();

	Document(const Document&) = delete;
	Document& operator=(const Document&) = delete;

	// Parses the HTML.  charset: the encoding of html; Mulberry passes
	// "utf-8" (parts are decoded already).  An empty charset means: detect it
	// (byte order mark or <meta charset>, else UTF-8), as a browser does.
	Result Load(const std::string& html, const std::string& charset = "utf-8");

	// Lays the document out for a width in pixels; returns its height
	int Layout(int width);

	// Size of the laid out document (the width can be larger than the
	// layout width, e.g. with wide tables)
	int GetWidth() const;
	int GetHeight() const;

	// Draws the part of the document inside clip.  (x, y): position of the
	// document's top left corner in the Cairo context; clip is in the
	// context's coordinates too.
	void Draw(cairo_t* cr, int x, int y, const Rect& clip);

	static const char* ResultText(Result result);

private:
	struct Impl;
	std::unique_ptr<Impl> mImpl;
};

}	// namespace htmlview

#endif
