/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

// Encodings used by OAuth2: base64url without padding (RFC 4648 section 5,
// as RFC 7636 and JWTs use it), percent-encoding of URL query parameters
// (RFC 3986) and application/x-www-form-urlencoded bodies (RFC 6749
// appendix B).  Strings are byte strings; text is UTF-8.

#ifndef OAUTH2_ENCODING_H
#define OAUTH2_ENCODING_H

#include <string>
#include <utility>
#include <vector>

namespace oauth2
{

// Ordered name/value pairs of a query string or a form body
typedef std::vector<std::pair<std::string, std::string>> Params;

// base64url without '=' padding
std::string Base64UrlEncode(const std::string& data);

// Decodes base64url without padding.  False, with out unchanged, on any
// character outside A-Z a-z 0-9 '-' '_' (padding '=' and the standard
// alphabet's '+' '/' included), on an impossible length, or when the unused
// bits of the last character are not zero.
bool Base64UrlDecode(const std::string& text, std::string& out);

// Standard base64 (RFC 4648 section 4) with '=' padding, as SASL uses it
std::string Base64Encode(const std::string& data);

// Decodes standard base64, with or without its '=' padding.  False, with
// out unchanged, on any character outside A-Z a-z 0-9 '+' '/' (spaces and
// line breaks included), on padding anywhere but at the end, on an
// impossible length, or when the unused bits are not zero.
bool Base64Decode(const std::string& text, std::string& out);

// Percent-encoding for URL query components: every byte except the RFC 3986
// unreserved characters (A-Z a-z 0-9 '-' '.' '_' '~') becomes %XX (upper
// case hexadecimal); a space becomes %20
std::string PercentEncode(const std::string& data);

// The same for form bodies: a space becomes '+'
std::string FormEncode(const std::string& data);

// Decodes %XX sequences (either case); with plus_as_space, '+' becomes a
// space (form bodies and redirect queries).  False, with out unchanged, on a
// '%' not followed by two hexadecimal digits.
bool PercentDecode(const std::string& text, std::string& out, bool plus_as_space);

// "name=value&name=value", with PercentEncode (URL query) or FormEncode
// (form body)
std::string BuildQuery(const Params& params);
std::string BuildForm(const Params& params);

// Parses "a=1&b=2" (no leading '?'): '+' is a space, empty parts are
// skipped, a part without '=' has an empty value.  False, with out
// unchanged, if a name or value has a bad % sequence.
bool ParseQuery(const std::string& query, Params& out);

} // namespace oauth2

#endif
