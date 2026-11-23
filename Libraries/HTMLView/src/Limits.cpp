/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

#include "Limits.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <unordered_set>
#include <vector>

namespace htmlview
{

namespace
{

// Elements the parser adds around everything: html, body, and tbody in
// tables
const int cImplicitDepth = 3;

typedef std::unordered_set<std::string> NameSet;

const NameSet cVoid = {
	"area", "base", "basefont", "bgsound", "br", "col", "embed", "frame", "hr", "image",
	"img", "input", "keygen", "link", "menuitem", "meta", "param", "source", "track", "wbr",
};

// Content up to the matching end tag is text, not tags
const NameSet cRawText = {
	"iframe", "noembed", "noframes", "noscript", "plaintext", "script", "style", "textarea",
	"title", "xmp",
};

// Start tags that close an open <p>
const NameSet cClosesP = {
	"address", "article", "aside", "blockquote", "center", "details", "dialog", "dir", "div",
	"dl", "dd", "dt", "fieldset", "figcaption", "figure", "footer", "form", "h1", "h2", "h3",
	"h4", "h5", "h6", "header", "hgroup", "hr", "li", "listing", "main", "menu", "nav", "ol",
	"p", "plaintext", "pre", "section", "summary", "table", "ul", "xmp",
};

// Where the search for an element to close stops
const NameSet cScope = {
	"applet", "button", "caption", "html", "marquee", "object", "table", "td", "template", "th",
};

// Where the searches for <li>, <dd>/<dt>, cells, rows and row groups stop
const NameSet cListScope = { "ol", "ul", "table", "td", "th", "button", "html" };
const NameSet cDefinitionScope = { "dl", "table", "td", "th", "html" };
const NameSet cRowScope = { "tr", "table", "html" };
const NameSet cSectionScope = { "table", "tbody", "thead", "tfoot", "html" };
const NameSet cTableScope = { "table", "html" };
const NameSet cNoScope;

bool IsHeading(const std::string& name)
{
	return (name.size() == 2) && (name[0] == 'h') && (name[1] >= '1') && (name[1] <= '6');
}

class Scanner
{
public:
	Scanner(const std::string& html, int max_depth)
		: mHtml(html), mMaxDepth(max_depth)
	{
	}

	ScanResult Run();

private:
	void	StartTag(const std::string& name, bool self_closing);
	void	EndTag(const std::string& name);
	void	SkipRawText(const std::string& name);

	// Closes the nearest open element accepted by match, unless an element
	// in stop is reached first
	template <typename Match>
	void	CloseNearest(Match match, const NameSet& stop);

	bool	InForeignContent() const { return mForeign > 0; }

	const std::string&			mHtml;
	size_t						mPos = 0;
	int							mMaxDepth;
	std::vector<std::string>	mStack;
	int							mForeign = 0;	// open svg/math elements
	ScanResult					mResult;
};

template <typename Match>
void Scanner::CloseNearest(Match match, const NameSet& stop)
{
	for (size_t i = mStack.size(); i-- > 0; )
	{
		if (match(mStack[i]))
		{
			for (size_t k = i; k < mStack.size(); k++)
			{
				if ((mStack[k] == "svg") || (mStack[k] == "math"))
					mForeign--;
			}
			mStack.resize(i);
			return;
		}
		if (stop.count(mStack[i]))
			return;
	}
}

void Scanner::StartTag(const std::string& name, bool self_closing)
{
	mResult.elements++;

	// In SVG and MathML, "<x/>" is an empty element; in HTML "/>" is ignored
	if (InForeignContent() && self_closing)
		return;

	if (!InForeignContent())
	{
		if (cClosesP.count(name))
			CloseNearest([](const std::string& n) { return n == "p"; }, cScope);

		if (name == "li")
			CloseNearest([](const std::string& n) { return n == "li"; },
						 cListScope);
		else if ((name == "dd") || (name == "dt"))
			CloseNearest([](const std::string& n) { return (n == "dd") || (n == "dt"); },
						 cDefinitionScope);
		else if ((name == "td") || (name == "th"))
			CloseNearest([](const std::string& n) { return (n == "td") || (n == "th"); },
						 cRowScope);
		else if (name == "tr")
			CloseNearest([](const std::string& n) { return n == "tr"; },
						 cSectionScope);
		else if ((name == "tbody") || (name == "thead") || (name == "tfoot"))
			CloseNearest([](const std::string& n) { return (n == "tbody") || (n == "thead") || (n == "tfoot"); },
						 cTableScope);
		else if ((name == "option") || (name == "optgroup"))
		{
			if (!mStack.empty() && (mStack.back() == "option"))
				mStack.pop_back();
			if ((name == "optgroup") && !mStack.empty() && (mStack.back() == "optgroup"))
				mStack.pop_back();
		}
		else if (IsHeading(name) && !mStack.empty() && IsHeading(mStack.back()))
			mStack.pop_back();
		else if ((name == "a") || (name == "nobr") || (name == "button") || (name == "form"))
			CloseNearest([&name](const std::string& n) { return n == name; }, cScope);

		if (cVoid.count(name))
			return;
	}

	mStack.push_back(name);
	if ((name == "svg") || (name == "math"))
		mForeign++;

	int depth = (int) mStack.size() + cImplicitDepth;
	mResult.max_depth = std::max(mResult.max_depth, depth);
	if (depth > mMaxDepth)
		mResult.too_deep = true;

	if (!InForeignContent() && cRawText.count(name))
		SkipRawText(name);
}

void Scanner::EndTag(const std::string& name)
{
	CloseNearest([&name](const std::string& n) { return n == name; }, cNoScope);
}

// Moves past "</name"
void Scanner::SkipRawText(const std::string& name)
{
	while (true)
	{
		size_t found = mHtml.find("</", mPos);
		if (found == std::string::npos)
		{
			mPos = mHtml.size();
			return;
		}
		mPos = found + 2;
		if ((mHtml.size() - mPos >= name.size()) &&
			std::equal(name.begin(), name.end(), mHtml.begin() + mPos,
					   [](char a, char b) { return a == (char) std::tolower((unsigned char) b); }))
		{
			mPos -= 2;
			return;
		}
	}
}

ScanResult Scanner::Run()
{
	const size_t len = mHtml.size();
	while (!mResult.too_deep && (mPos < len))
	{
		size_t lt = mHtml.find('<', mPos);
		if ((lt == std::string::npos) || (lt + 1 >= len))
			break;
		mPos = lt + 1;
		unsigned char next = (unsigned char) mHtml[mPos];

		// Comment, doctype, processing instruction
		if (mHtml.compare(lt, 4, "<!--") == 0)
		{
			size_t end = mHtml.find("-->", lt + 4);
			mPos = (end == std::string::npos) ? len : end + 3;
			continue;
		}
		if ((next == '!') || (next == '?'))
		{
			size_t end = mHtml.find('>', mPos);
			mPos = (end == std::string::npos) ? len : end + 1;
			continue;
		}

		bool end_tag = false;
		if (next == '/')
		{
			end_tag = true;
			mPos++;
			if (mPos >= len)
				break;
			next = (unsigned char) mHtml[mPos];
		}
		if (!std::isalpha(next))
			continue;		// "<" as text

		// Tag name
		std::string name;
		while ((mPos < len) && !std::isspace((unsigned char) mHtml[mPos]) && (mHtml[mPos] != '/') &&
			   (mHtml[mPos] != '>'))
			name += (char) std::tolower((unsigned char) mHtml[mPos++]);

		// Attributes, up to the closing ">" (quotes may contain ">")
		bool self_closing = false;
		while (mPos < len)
		{
			char c = mHtml[mPos];
			if (c == '>')
			{
				mPos++;
				break;
			}
			if ((c == '"') || (c == '\''))
			{
				size_t close = mHtml.find(c, mPos + 1);
				mPos = (close == std::string::npos) ? len : close + 1;
				self_closing = false;
				continue;
			}
			if (!std::isspace((unsigned char) c))
				self_closing = (c == '/');
			mPos++;
		}

		if (end_tag)
			EndTag(name);
		else
			StartTag(name, self_closing);
	}
	return mResult;
}

}	// namespace

ScanResult ScanHTML(const std::string& html, int max_depth)
{
	return Scanner(html, max_depth).Run();
}

}	// namespace htmlview
