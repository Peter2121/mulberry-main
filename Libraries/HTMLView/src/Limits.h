/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

#ifndef HTMLVIEW_LIMITS_H
#define HTMLVIEW_LIMITS_H

#include <cstddef>
#include <string>

namespace htmlview
{

// What a quick scan of the HTML found, before litehtml parses it
struct ScanResult
{
	bool	too_deep = false;	// nesting deeper than the limit (scan stopped there)
	int		max_depth = 0;		// estimated element nesting depth
	size_t	elements = 0;		// start tags
};

// Estimates how deeply the elements of an HTML document nest, and counts
// them, without parsing it for real:
// - litehtml builds, styles and lays out its element tree recursively, so
//   a very deep tree (easy to write: "<div>" repeated) overflows the stack;
// - Gumbo, litehtml's HTML parser, takes quadratic time in the nesting
//   depth, so it cannot be used to measure it.
// The scan follows the tags with the implicit closing rules of HTML that
// matter for depth (<p>, <li>, <dd>/<dt>, table cells and rows, <option>,
// headings, nested <a>), so ordinary sloppy HTML is not overestimated.  It
// stops as soon as the depth exceeds max_depth: its cost is at most the
// input length times max_depth.
ScanResult ScanHTML(const std::string& html, int max_depth);

}	// namespace htmlview

#endif
