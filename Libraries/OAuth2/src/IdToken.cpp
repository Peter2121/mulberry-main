/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

#include "oauth2/IdToken.h"

#include "oauth2/Encoding.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace oauth2
{

namespace
{

typedef nlohmann::json Json;

// A string claim; false if present with another type (null counts as absent)
bool StringClaim(const Json& json, const char* name, std::string& out)
{
	const auto it = json.find(name);
	if (it == json.end() || it->is_null())
		return true;
	if (!it->is_string())
		return false;
	out = it->get<std::string>();
	return true;
}

// A NumericDate claim (seconds since 1970, possibly with a fraction)
bool TimeClaim(const Json& json, const char* name, std::time_t& out)
{
	const auto it = json.find(name);
	if (it == json.end() || it->is_null())
		return true;
	if (!it->is_number())
		return false;
	const double value = it->get<double>();
	if (!std::isfinite(value) || value < 0 || value > 1e12)
		return false;
	out = static_cast<std::time_t>(value);
	return true;
}

} // namespace

std::string IdTokenClaims::Address() const
{
	if (!email.empty())
		return email;
	if (preferred_username.find('@') != std::string::npos)
		return preferred_username;
	return std::string();
}

bool IdTokenClaims::HasAudience(const std::string& client_id) const
{
	return std::find(audience.begin(), audience.end(), client_id) != audience.end();
}

bool ParseIdToken(const std::string& token, IdTokenClaims& claims, std::string& error)
{
	// Messages never quote the token
	const size_t first = token.find('.');
	const size_t second = first == std::string::npos ? first : token.find('.', first + 1);
	if (second == std::string::npos || token.find('.', second + 1) != std::string::npos)
	{
		error = "the ID token is not a JWT (three parts separated by dots)";
		return false;
	}
	std::string payload;
	if (!Base64UrlDecode(token.substr(first + 1, second - first - 1), payload))
	{
		error = "the ID token's payload is not base64url";
		return false;
	}

	Json json;
	try
	{
		json = Json::parse(payload, nullptr, false);
	}
	catch (const std::exception&)
	{
		json = Json(Json::value_t::discarded);
	}
	if (json.is_discarded() || !json.is_object())
	{
		error = "the ID token's payload is not a JSON object";
		return false;
	}

	IdTokenClaims parsed;
	const char* wrong = nullptr;
	try
	{
		if (!StringClaim(json, "iss", parsed.issuer))
			wrong = "iss";
		else if (!StringClaim(json, "sub", parsed.subject))
			wrong = "sub";
		else if (!StringClaim(json, "email", parsed.email))
			wrong = "email";
		else if (!StringClaim(json, "preferred_username", parsed.preferred_username))
			wrong = "preferred_username";
		else if (!StringClaim(json, "name", parsed.name))
			wrong = "name";
		else if (!TimeClaim(json, "exp", parsed.expires_at))
			wrong = "exp";
		else if (!TimeClaim(json, "iat", parsed.issued_at))
			wrong = "iat";

		const auto aud = json.find("aud");
		if (!wrong && aud != json.end() && !aud->is_null())
		{
			if (aud->is_string())
				parsed.audience.push_back(aud->get<std::string>());
			else if (aud->is_array() && std::all_of(aud->begin(), aud->end(),
					[](const Json& item) { return item.is_string(); }))
			{
				for (const Json& item : *aud)
					parsed.audience.push_back(item.get<std::string>());
			}
			else
				wrong = "aud";
		}

		// A boolean, or the string "true" (some providers send strings)
		const auto verified = json.find("email_verified");
		if (!wrong && verified != json.end() && !verified->is_null())
		{
			if (verified->is_boolean())
				parsed.email_verified = verified->get<bool>();
			else if (verified->is_string())
				parsed.email_verified = verified->get<std::string>() == "true";
			else
				wrong = "email_verified";
			parsed.email_verified_given = true;
		}
	}
	catch (const std::exception&)
	{
		wrong = "a claim";
	}
	if (wrong)
	{
		error = std::string("the ID token's ") + wrong + " claim has the wrong type";
		return false;
	}
	claims = parsed;
	return true;
}

} // namespace oauth2
