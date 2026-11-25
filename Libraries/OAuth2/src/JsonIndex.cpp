/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

#include "JsonIndex.h"

#include <nlohmann/json.hpp>

#include <set>

namespace oauth2
{

namespace
{

// Nesting limit: the configuration files are shallow
const int cMaxDepth = 64;

class Indexer
{
public:
	Indexer(const std::string& text, JsonIndex& index) : mText(text), mIndex(index) {}

	bool Run()
	{
		if (!Value("", 0))
			return false;
		SkipSpace();
		return mPos == mText.size();
	}

private:
	const std::string& mText;
	JsonIndex& mIndex;
	size_t mPos = 0;

	bool AtEnd() const { return mPos >= mText.size(); }
	char Peek() const { return mText[mPos]; }

	void SkipSpace()
	{
		while (!AtEnd() && (Peek() == ' ' || Peek() == '\t' || Peek() == '\n' || Peek() == '\r'))
			++mPos;
	}

	// A string token, quotes included; mPos is on the opening quote
	bool StringToken(std::string& token)
	{
		const size_t start = mPos++;
		while (!AtEnd())
		{
			const char c = mText[mPos];
			if (c == '\\')
				mPos += 2;
			else if (c == '"')
			{
				++mPos;
				token = mText.substr(start, mPos - start);
				return true;
			}
			else
				++mPos;
		}
		return false;
	}

	// Decodes a string token (escapes, \uXXXX) with nlohmann/json itself
	static bool Decode(const std::string& token, std::string& value)
	{
		try
		{
			value = nlohmann::json::parse(token).get<std::string>();
			return true;
		}
		catch (const nlohmann::json::exception&)
		{
			return false;
		}
	}

	bool Value(const std::string& pointer, int depth)
	{
		if (depth > cMaxDepth)
			return false;
		SkipSpace();
		if (AtEnd())
			return false;
		mIndex.value_pos[pointer] = mPos;

		const char c = Peek();
		if (c == '{')
			return Object(pointer, depth);
		if (c == '[')
			return Array(pointer, depth);
		if (c == '"')
		{
			std::string token;
			return StringToken(token);
		}
		// Number or literal: up to the next delimiter
		const size_t start = mPos;
		while (!AtEnd() && Peek() != ',' && Peek() != ']' && Peek() != '}' && Peek() != ' ' &&
			Peek() != '\t' && Peek() != '\n' && Peek() != '\r')
			++mPos;
		return mPos > start;
	}

	bool Object(const std::string& pointer, int depth)
	{
		++mPos;
		SkipSpace();
		if (!AtEnd() && Peek() == '}')
		{
			++mPos;
			return true;
		}
		std::set<std::string> keys;
		for (;;)
		{
			SkipSpace();
			if (AtEnd() || Peek() != '"')
				return false;
			const size_t keyPos = mPos;
			std::string token;
			std::string key;
			if (!StringToken(token) || !Decode(token, key))
				return false;
			if (!keys.insert(key).second && !mIndex.has_duplicate)
			{
				mIndex.has_duplicate = true;
				mIndex.duplicate_key = key;
				mIndex.duplicate_object = pointer;
				mIndex.duplicate_pos = keyPos;
			}
			const std::string child = JsonPointerAppend(pointer, key);
			mIndex.key_pos[child] = keyPos;

			SkipSpace();
			if (AtEnd() || Peek() != ':')
				return false;
			++mPos;
			if (!Value(child, depth + 1))
				return false;
			SkipSpace();
			if (AtEnd())
				return false;
			if (Peek() == ',')
			{
				++mPos;
				continue;
			}
			if (Peek() == '}')
			{
				++mPos;
				return true;
			}
			return false;
		}
	}

	bool Array(const std::string& pointer, int depth)
	{
		++mPos;
		SkipSpace();
		if (!AtEnd() && Peek() == ']')
		{
			++mPos;
			return true;
		}
		for (size_t n = 0;; ++n)
		{
			if (!Value(pointer + "/" + std::to_string(n), depth + 1))
				return false;
			SkipSpace();
			if (AtEnd())
				return false;
			if (Peek() == ',')
			{
				++mPos;
				continue;
			}
			if (Peek() == ']')
			{
				++mPos;
				return true;
			}
			return false;
		}
	}
};

} // namespace

std::string JsonPointerAppend(const std::string& pointer, const std::string& key)
{
	std::string result = pointer + "/";
	for (char c : key)
	{
		if (c == '~')
			result += "~0";
		else if (c == '/')
			result += "~1";
		else
			result += c;
	}
	return result;
}

bool IndexJson(const std::string& text, JsonIndex& index)
{
	index = JsonIndex();
	return Indexer(text, index).Run();
}

} // namespace oauth2
