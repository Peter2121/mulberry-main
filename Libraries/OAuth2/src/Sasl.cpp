/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

#include "oauth2/Sasl.h"

#include "oauth2/Encoding.h"
#include "TokenInternal.h"

#include <algorithm>
#include <cctype>

namespace oauth2
{

using namespace internal;

const char* const cSaslXOAuth2 = "XOAUTH2";
const char* const cSaslOAuthBearer = "OAUTHBEARER";

namespace
{

const char cKvSep = '\x01';

bool HasControl(const std::string& text)
{
	return std::any_of(text.begin(), text.end(),
		[](char c) { return (unsigned char) c < 0x20 || c == 0x7f; });
}

bool EqualsNoCase(const std::string& a, const char* b)
{
	const std::string other = b;
	return a.size() == other.size() && std::equal(a.begin(), a.end(), other.begin(),
		[](char x, char y) { return std::toupper((unsigned char) x) == std::toupper((unsigned char) y); });
}

} // namespace

std::string XOAuth2Response(const std::string& user, const std::string& access_token)
{
	if (user.empty() || access_token.empty() || HasControl(user) || HasControl(access_token))
		return std::string();
	return "user=" + user + cKvSep + "auth=Bearer " + access_token + cKvSep + cKvSep;
}

std::string Gs2SaslName(const std::string& user)
{
	std::string result;
	for (char c : user)
	{
		if (c == ',')
			result += "=2C";
		else if (c == '=')
			result += "=3D";
		else
			result += c;
	}
	return result;
}

std::string OAuthBearerResponse(const std::string& user, const std::string& host, int port,
	const std::string& access_token)
{
	if (user.empty() || access_token.empty() || HasControl(user) || HasControl(host) ||
		HasControl(access_token))
		return std::string();
	std::string response = "n,a=" + Gs2SaslName(user) + "," + cKvSep;
	if (!host.empty())
		response += "host=" + host + cKvSep;
	if (port > 0)
		response += "port=" + std::to_string(port) + cKvSep;
	response += "auth=Bearer " + access_token + cKvSep + cKvSep;
	return response;
}

std::string SaslErrorReply(const std::string& mechanism)
{
	if (EqualsNoCase(mechanism, cSaslOAuthBearer))
		return std::string(1, cKvSep);
	return std::string();
}

std::string SaslError::Describe() const
{
	std::string text = "status " + (status.empty() ? std::string("not given") : status);
	if (status == "invalid_token" || status == "401")
		text += " (the access token is invalid or expired)";
	else if (status == "insufficient_scope" || status == "403")
		text += " (the access token lacks a scope)";
	if (!scope.empty())
		text += ", scope " + scope;
	if (!schemes.empty())
		text += ", schemes " + schemes;
	if (!openid_configuration.empty())
		text += ", provider " + openid_configuration;
	return text;
}

bool ParseSaslErrorChallenge(const std::string& challenge, SaslError& error, std::string& message)
{
	std::string text = challenge;
	// Outer spaces and the line end, if the caller left them
	const size_t first = text.find_first_not_of(" \t\r\n");
	text = first == std::string::npos ? std::string() :
		text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
	std::string decoded;
	if (text.empty() || !Base64Decode(text, decoded))
	{
		message = "the server's challenge is not base64";
		return false;
	}
	Json json;
	try
	{
		json = Json::parse(decoded, nullptr, false);
	}
	catch (const std::exception&)
	{
		json = Json(Json::value_t::discarded);
	}
	if (json.is_discarded() || !json.is_object())
	{
		message = "the server's challenge is not a JSON object (" + std::to_string(decoded.size()) + " bytes)";
		return false;
	}
	SaslError parsed;
	parsed.status = CleanText(StringField(json, "status"));
	parsed.schemes = CleanText(StringField(json, "schemes"));
	parsed.scope = CleanText(StringField(json, "scope"));
	parsed.openid_configuration = CleanText(StringField(json, "openid-configuration"));
	error = parsed;
	return true;
}

} // namespace oauth2
