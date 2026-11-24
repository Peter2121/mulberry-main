/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

#include "htmlview/Document.h"

#include "Container.h"
#include "Images.h"
#include "Limits.h"

#include <cmath>

namespace htmlview
{

namespace
{

// Added to litehtml's master style sheet: elements a mail viewer does not
// show (embedded content and form controls; there is no scripting, and
// nothing is loaded from the network).  The content of <form> stays
// visible: some newsletters wrap everything in one.
//
// Also HTML attributes litehtml does not map to CSS itself: the list types
// of <ol type> and <ul type>; align="center"/"right" on cells, <div>, <p>
// and <center>, which browsers apply to child tables and block images too
// (as -webkit-center: newsletters centre their columns this way).  Rules of the master style sheet are applied
// before the document's, so any author CSS overrides them, as it overrides
// presentational attributes in browsers.
const char* const cMailCSS = R"(
iframe, frame, frameset, object, embed, applet, audio, video, canvas,
input, button, select, textarea, keygen, portal {
	display: none;
}
ol[type="1"] { list-style-type: decimal; }
ol[type="a" s] { list-style-type: lower-alpha; }
ol[type="A" s] { list-style-type: upper-alpha; }
ol[type="i" s] { list-style-type: lower-roman; }
ol[type="I" s] { list-style-type: upper-roman; }
ul[type="disc"] { list-style-type: disc; }
ul[type="circle"] { list-style-type: circle; }
ul[type="square"] { list-style-type: square; }
td[align="center"] > table, th[align="center"] > table, div[align="center"] > table,
p[align="center"] > table, center > table,
td[align="center"] > img, th[align="center"] > img, div[align="center"] > img,
td[align="center"] > a > img, th[align="center"] > a > img, div[align="center"] > a > img {
	margin-left: auto;
	margin-right: auto;
}
td[align="right"] > table, th[align="right"] > table, div[align="right"] > table,
td[align="right"] > img, th[align="right"] > img, div[align="right"] > img {
	margin-left: auto;
	margin-right: 0;
}
)";

// <table cellpadding="N">, heavily used by mail newsletters, is not mapped
// by litehtml: one rule per common value, for the table's own cells only
// (not those of nested tables)
std::string CellPaddingCSS()
{
	std::string css;
	for (int n = 0; n <= 40; n++)
	{
		std::string table = "table[cellpadding=\"" + std::to_string(n) + "\"]";
		css += table + " > tr > td, " + table + " > tr > th, " +
			   table + " > * > tr > td, " + table + " > * > tr > th { padding: " +
			   std::to_string(n) + "px; }\n";
	}
	return css;
}

const std::string& MasterCSS()
{
	static const std::string css = std::string(litehtml::master_css) + cMailCSS + CellPaddingCSS();
	return css;
}

}	// namespace

struct Document::Impl
{
	Impl(const Settings& settings, Host* host)
		: mSettings(settings), mContainer(settings, host)
	{
	}

	Settings				mSettings;
	Container				mContainer;
	litehtml::document::ptr	mDocument;
};

Document::Document(const Settings& settings, Host* host)
	: mImpl(new Impl(settings, host))
{
}

Document::~Document()
{
	// The document's fonts are deleted through the container: release the
	// document first
	mImpl->mDocument.reset();
}

Document::Result Document::Load(const std::string& html, const std::string& charset)
{
	mImpl->mDocument.reset();

	if (html.empty())
		return Result::Empty;
	if (html.size() > mImpl->mSettings.max_html_size)
		return Result::TooLarge;
	ScanResult scan = ScanHTML(html, mImpl->mSettings.max_nesting_depth);
	if (scan.too_deep)
		return Result::TooDeep;
	if (scan.elements > mImpl->mSettings.max_elements)
		return Result::TooManyElements;

	// Known charset: used as it is (a <meta charset> is ignored); unknown or
	// none: litehtml detects it (byte order mark, <meta>, else UTF-8)
	litehtml::encoding encoding = charset.empty() ? litehtml::encoding::null : litehtml::get_encoding(charset);
	litehtml::estring input(html, encoding,
							(encoding == litehtml::encoding::null) ? litehtml::confidence::tentative
																	: litehtml::confidence::certain);

	mImpl->mDocument = litehtml::document::createFromString(input, &mImpl->mContainer, MasterCSS());
	return mImpl->mDocument ? Result::Ok : Result::Error;
}

int Document::Layout(int width)
{
	mImpl->mContainer.SetViewportWidth(width);
	if (!mImpl->mDocument)
		return 0;
	mImpl->mDocument->render((litehtml::pixel_t) width);
	return GetHeight();
}

int Document::GetWidth() const
{
	return mImpl->mDocument ? (int) std::ceil((float) mImpl->mDocument->width()) : 0;
}

int Document::GetHeight() const
{
	return mImpl->mDocument ? (int) std::ceil((float) mImpl->mDocument->height()) : 0;
}

void Document::Draw(cairo_t* cr, int x, int y, const Rect& clip)
{
	if (!mImpl->mDocument || !cr)
		return;

	litehtml::position clip_pos(clip.x, clip.y, clip.width, clip.height);
	mImpl->mDocument->draw((litehtml::uint_ptr) cr, x, y, &clip_pos);
}

bool IsDisplayableImage(const std::string& data, const Settings& settings)
{
	return (data.size() <= settings.max_image_bytes) && ProbeImage(data, settings.max_image_pixels);
}

int Document::GetBlockedImages() const
{
	return mImpl->mContainer.GetBlockedImages();
}

const char* Document::ResultText(Result result)
{
	switch (result)
	{
	case Result::Ok:
		return "ok";
	case Result::Empty:
		return "empty document";
	case Result::TooLarge:
		return "document too large";
	case Result::TooDeep:
		return "elements nested too deeply";
	case Result::TooManyElements:
		return "too many elements";
	case Result::Error:
	default:
		return "document could not be created";
	}
}

}	// namespace htmlview
