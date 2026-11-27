/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

#include "oauth2/Encoding.h"

namespace oauth2
{

namespace
{

const char cBase64Url[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
const char cHexDigits[] = "0123456789ABCDEF";

// Value of a base64url character, or -1
int Base64UrlValue(char c)
{
	if (c >= 'A' && c <= 'Z')
		return c - 'A';
	if (c >= 'a' && c <= 'z')
		return c - 'a' + 26;
	if (c >= '0' && c <= '9')
		return c - '0' + 52;
	if (c == '-')
		return 62;
	if (c == '_')
		return 63;
	return -1;
}

int HexValue(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

bool IsUnreserved(unsigned char c)
{
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
		c == '-' || c == '.' || c == '_' || c == '~';
}

std::string Encode(const std::string& data, bool space_as_plus)
{
	std::string result;
	result.reserve(data.size() * 3);
	for (unsigned char c : data)
	{
		if (IsUnreserved(c))
			result += static_cast<char>(c);
		else if (c == ' ' && space_as_plus)
			result += '+';
		else
		{
			result += '%';
			result += cHexDigits[c >> 4];
			result += cHexDigits[c & 0x0F];
		}
	}
	return result;
}

std::string Join(const Params& params, bool form)
{
	std::string result;
	for (const auto& param : params)
	{
		if (!result.empty())
			result += '&';
		result += Encode(param.first, form);
		result += '=';
		result += Encode(param.second, form);
	}
	return result;
}

} // namespace

std::string Base64UrlEncode(const std::string& data)
{
	std::string result;
	result.reserve((data.size() * 4 + 2) / 3);
	size_t i = 0;
	for (; i + 3 <= data.size(); i += 3)
	{
		const unsigned long n = (static_cast<unsigned char>(data[i]) << 16) |
			(static_cast<unsigned char>(data[i + 1]) << 8) |
			static_cast<unsigned char>(data[i + 2]);
		result += cBase64Url[(n >> 18) & 0x3F];
		result += cBase64Url[(n >> 12) & 0x3F];
		result += cBase64Url[(n >> 6) & 0x3F];
		result += cBase64Url[n & 0x3F];
	}
	const size_t rest = data.size() - i;
	if (rest == 1)
	{
		const unsigned long n = static_cast<unsigned char>(data[i]) << 16;
		result += cBase64Url[(n >> 18) & 0x3F];
		result += cBase64Url[(n >> 12) & 0x3F];
	}
	else if (rest == 2)
	{
		const unsigned long n = (static_cast<unsigned char>(data[i]) << 16) |
			(static_cast<unsigned char>(data[i + 1]) << 8);
		result += cBase64Url[(n >> 18) & 0x3F];
		result += cBase64Url[(n >> 12) & 0x3F];
		result += cBase64Url[(n >> 6) & 0x3F];
	}
	return result;
}

bool Base64UrlDecode(const std::string& text, std::string& out)
{
	// A last group of one character cannot encode a whole byte
	if (text.size() % 4 == 1)
		return false;

	std::string result;
	result.reserve(text.size() * 3 / 4);
	unsigned long buffer = 0;
	int bits = 0;
	for (char c : text)
	{
		const int value = Base64UrlValue(c);
		if (value < 0)
			return false;
		buffer = (buffer << 6) | static_cast<unsigned long>(value);
		bits += 6;
		if (bits >= 8)
		{
			bits -= 8;
			result += static_cast<char>((buffer >> bits) & 0xFF);
		}
	}
	// The bits left over (2 or 4) must be zero: one encoding per value
	if (bits > 0 && (buffer & ((1UL << bits) - 1)) != 0)
		return false;

	out.swap(result);
	return true;
}

std::string Base64Encode(const std::string& data)
{
	std::string result = Base64UrlEncode(data);
	for (char& c : result)
	{
		if (c == '-')
			c = '+';
		else if (c == '_')
			c = '/';
	}
	result.append((4 - result.size() % 4) % 4, '=');
	return result;
}

bool Base64Decode(const std::string& text, std::string& out)
{
	std::string body = text;
	if (body.size() % 4 == 0 && !body.empty())
	{
		// At most two '=', only at the end
		size_t padding = 0;
		while (padding < 2 && body[body.size() - 1 - padding] == '=')
			++padding;
		body.resize(body.size() - padding);
	}
	for (char& c : body)
	{
		if (c == '-' || c == '_')
			return false;
		if (c == '+')
			c = '-';
		else if (c == '/')
			c = '_';
	}
	// '=' left in the middle is refused here
	return Base64UrlDecode(body, out);
}

std::string PercentEncode(const std::string& data)
{
	return Encode(data, false);
}

std::string FormEncode(const std::string& data)
{
	return Encode(data, true);
}

bool PercentDecode(const std::string& text, std::string& out, bool plus_as_space)
{
	std::string result;
	result.reserve(text.size());
	for (size_t i = 0; i < text.size(); ++i)
	{
		const char c = text[i];
		if (c == '%')
		{
			// Two characters must follow
			if (i + 2 >= text.size())
				return false;
			const int high = HexValue(text[i + 1]);
			const int low = HexValue(text[i + 2]);
			if (high < 0 || low < 0)
				return false;
			result += static_cast<char>((high << 4) | low);
			i += 2;
		}
		else if (c == '+' && plus_as_space)
			result += ' ';
		else
			result += c;
	}
	out.swap(result);
	return true;
}

std::string BuildQuery(const Params& params)
{
	return Join(params, false);
}

std::string BuildForm(const Params& params)
{
	return Join(params, true);
}

bool ParseQuery(const std::string& query, Params& out)
{
	Params result;
	size_t start = 0;
	while (start <= query.size())
	{
		size_t end = query.find('&', start);
		if (end == std::string::npos)
			end = query.size();
		const std::string part = query.substr(start, end - start);
		if (!part.empty())
		{
			const size_t equals = part.find('=');
			std::string name;
			std::string value;
			if (!PercentDecode(part.substr(0, equals), name, true))
				return false;
			if (equals != std::string::npos &&
				!PercentDecode(part.substr(equals + 1), value, true))
				return false;
			result.emplace_back(name, value);
		}
		start = end + 1;
	}
	out.swap(result);
	return true;
}

} // namespace oauth2
