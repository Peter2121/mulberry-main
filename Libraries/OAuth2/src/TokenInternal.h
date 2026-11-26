/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

// Helpers of the token client shared with the device code (not installed)

#ifndef OAUTH2_TOKENINTERNAL_H
#define OAUTH2_TOKENINTERNAL_H

#include "oauth2/TokenClient.h"
#include "HttpClient.h"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace oauth2
{
namespace internal
{

typedef nlohmann::ordered_json Json;

// The first line of a server text, without control characters or outer
// spaces, at most 300 bytes (cut on a UTF-8 boundary)
std::string CleanText(const std::string& text);

// The string member name of object; empty if absent or not a string
std::string StringField(const Json& object, const char* name);

std::string JoinWords(const std::vector<std::string>& words);

// "123" or 123 as a count of seconds; false if neither, or negative
bool ParseSeconds(const Json& value, long& seconds);

void SetError(TokenError& error, TokenError::Kind kind, const std::string& message);

// A POST that brought no usable answer; what: "token endpoint"...,
// request: "token request"...
void SetTransportError(TokenError& error, const HttpResult& result, const std::string& what,
	const std::string& request);

// "application/json, 123 bytes"
std::string DescribeBody(const std::string& content_type, const std::string& body);

// The OAuth2 error of an answer (RFC 6749 section 5.2, and the providers'
// additions)
void SetOAuthError(const Json& object, long http_status, TokenError& error);

} // namespace internal
} // namespace oauth2

#endif
