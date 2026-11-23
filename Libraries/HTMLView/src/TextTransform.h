/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

#ifndef HTMLVIEW_TEXTTRANSFORM_H
#define HTMLVIEW_TEXTTRANSFORM_H

#include <litehtml.h>

namespace htmlview
{

// CSS text-transform on UTF-8 text, with Unicode case mapping
// (libunistring): uppercase, lowercase, capitalize (first letter)
void TransformText(std::string& text, litehtml::text_transform transform);

}	// namespace htmlview

#endif
