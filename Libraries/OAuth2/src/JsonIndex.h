/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

// Private: positions of the keys and values of a JSON text, and duplicate
// keys.  nlohmann/json reports positions only for syntax errors and keeps
// the last of two identical keys silently; this second pass over a text it
// has already accepted gives every key and value its byte offset, by JSON
// pointer (RFC 6901), so that schema errors can name a line and column.

#ifndef OAUTH2_JSONINDEX_H
#define OAUTH2_JSONINDEX_H

#include <cstddef>
#include <map>
#include <string>

namespace oauth2
{

struct JsonIndex
{
	// Byte offset of each value ("" is the root, "/providers/google/hosts/0"
	// an array element), and of each object member's key (its opening quote)
	std::map<std::string, size_t> value_pos;
	std::map<std::string, size_t> key_pos;

	// The first key that appears twice in the same object, in text order
	bool has_duplicate = false;
	std::string duplicate_key;
	std::string duplicate_object;	// pointer of the object holding it
	size_t duplicate_pos = 0;		// offset of the second occurrence
};

// Indexes a text that nlohmann/json has accepted; false if the text turns
// out not to be valid JSON after all
bool IndexJson(const std::string& text, JsonIndex& index);

// "/" + key with '~' and '/' escaped (RFC 6901)
std::string JsonPointerAppend(const std::string& pointer, const std::string& key);

} // namespace oauth2

#endif
