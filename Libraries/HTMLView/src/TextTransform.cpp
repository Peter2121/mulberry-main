/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

#include "TextTransform.h"

#include <unicase.h>
#include <unistr.h>

#include <cstdlib>

namespace htmlview
{

void TransformText(std::string& text, litehtml::text_transform transform)
{
	if (text.empty())
		return;

	const uint8_t* s = (const uint8_t*) text.data();
	size_t n = text.size();
	size_t length = 0;
	uint8_t* result = nullptr;

	switch (transform)
	{
	case litehtml::text_transform_uppercase:
		result = u8_toupper(s, n, uc_locale_language(), nullptr, nullptr, &length);
		break;

	case litehtml::text_transform_lowercase:
		result = u8_tolower(s, n, uc_locale_language(), nullptr, nullptr, &length);
		break;

	case litehtml::text_transform_capitalize:
		{
			// litehtml calls this for each word: change its first letter only
			ucs4_t first;
			int first_length = u8_mbtouc(&first, s, n);
			if ((first_length <= 0) || (first == 0xFFFD))
				return;
			ucs4_t title = uc_totitle(first);
			if (title == first)
				return;
			uint8_t buffer[6];
			int title_length = u8_uctomb(buffer, title, sizeof(buffer));
			if (title_length > 0)
				text.replace(0, first_length, (const char*) buffer, title_length);
		}
		return;

	default:
		return;
	}

	if (result)
	{
		text.assign((const char*) result, length);
		std::free(result);
	}
}

}	// namespace htmlview
