/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

#include "oauth2/TokenClient.h"

#include "oauth2/Pkce.h"
#include "oauth2/Provider.h"
#include "oauth2/Redact.h"
#include "HttpClient.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>

namespace oauth2
{

namespace
{

typedef nlohmann::ordered_json Json;

// Longest error description kept in messages, in bytes
const size_t cMaxDescription = 300;

// The code of ProbeClient(): no provider issues codes of this form
const char* const cProbeCode = "invalid-code-for-the-client-check";

bool EqualsNoCase(const std::string& a, const std::string& b)
{
	if (a.size() != b.size())
		return false;
	for (size_t i = 0; i < a.size(); ++i)
		if (std::tolower((unsigned char) a[i]) != std::tolower((unsigned char) b[i]))
			return false;
	return true;
}

// The first line of a server text, without control characters or outer
// spaces, at most cMaxDescription bytes (cut on a UTF-8 boundary)
std::string CleanText(const std::string& text)
{
	std::string line = text.substr(0, text.find_first_of("\r\n"));
	for (char& c : line)
		if ((unsigned char) c < 0x20 || c == 0x7f)
			c = ' ';
	const size_t first = line.find_first_not_of(' ');
	if (first == std::string::npos)
		return std::string();
	line = line.substr(first, line.find_last_not_of(' ') - first + 1);
	if (line.size() > cMaxDescription)
	{
		size_t cut = cMaxDescription;
		while (cut > 0 && ((unsigned char) line[cut] & 0xC0) == 0x80)
			--cut;
		line = line.substr(0, cut) + "...";
	}
	return line;
}

std::string StringField(const Json& object, const char* name)
{
	const auto it = object.find(name);
	return (it != object.end() && it->is_string()) ? it->get<std::string>() : std::string();
}

std::string JoinWords(const std::vector<std::string>& words)
{
	std::string result;
	for (const std::string& word : words)
		result += (result.empty() ? "" : " ") + word;
	return result;
}

std::vector<std::string> SplitWords(const std::string& text)
{
	std::vector<std::string> words;
	size_t start = 0;
	while (start < text.size())
	{
		const size_t end = std::min(text.find(' ', start), text.size());
		if (end > start)
			words.push_back(text.substr(start, end - start));
		start = end + 1;
	}
	return words;
}

// "123" or 123 as a count of seconds; false if neither, or negative
bool ParseSeconds(const Json& value, long& seconds)
{
	if (value.is_number_unsigned())
	{
		const auto n = value.get<unsigned long long>();
		if (n > 0x7fffffffULL)
			return false;
		seconds = (long) n;
		return true;
	}
	if (value.is_number_integer())
	{
		const auto n = value.get<long long>();
		if (n < 0 || n > 0x7fffffffLL)
			return false;
		seconds = (long) n;
		return true;
	}
	if (value.is_number_float())
	{
		const double n = value.get<double>();
		if (!std::isfinite(n) || n < 0 || n > 0x7fffffff)
			return false;
		seconds = (long) n;
		return true;
	}
	if (value.is_string())
	{
		// Some servers send the number as a string
		const std::string text = value.get<std::string>();
		if (text.empty() || text.size() > 10 ||
			!std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; }))
			return false;
		const long long n = std::stoll(text);
		if (n > 0x7fffffffLL)
			return false;
		seconds = (long) n;
		return true;
	}
	return false;
}

// Microsoft: the first of "error_codes", else "AADSTS<digits>" in the
// description
long FindAadsts(const Json& object, const std::string& description)
{
	const auto codes = object.find("error_codes");
	if (codes != object.end() && codes->is_array() && !codes->empty() &&
		codes->front().is_number_integer() && codes->front().get<long long>() > 0)
		return (long) codes->front().get<long long>();
	const size_t at = description.find("AADSTS");
	if (at == std::string::npos)
		return 0;
	long code = 0;
	size_t i = at + 6;
	for (; i < description.size() && i < at + 15 && std::isdigit((unsigned char) description[i]); ++i)
		code = code * 10 + (description[i] - '0');
	return code;
}

void SetError(TokenError& error, TokenError::Kind kind, const std::string& message)
{
	error.kind = kind;
	error.message = message;
}

// A POST that brought no usable answer; what: "token endpoint"...,
// request: "token request"...
void SetTransportError(TokenError& error, const HttpResult& result, const std::string& what,
	const std::string& request)
{
	switch (result.outcome)
	{
	case HttpResult::Outcome::RedirectRefused:
		SetError(error, TokenError::Kind::Http, what + ": " + result.error);
		error.http_status = result.status;
		break;
	case HttpResult::Outcome::BadUrl:
		SetError(error, TokenError::Kind::Config, what + ": " + result.error);
		break;
	case HttpResult::Outcome::Timeout:
		SetError(error, TokenError::Kind::Timeout, what + ": " + result.error);
		break;
	case HttpResult::Outcome::TooLarge:
		SetError(error, TokenError::Kind::Protocol, what + ": " + result.error);
		break;
	case HttpResult::Outcome::Cancelled:
		SetError(error, TokenError::Kind::Cancelled, request + " cancelled");
		break;
	default:
		SetError(error, TokenError::Kind::Network, what + ": " + result.error);
		break;
	}
}

std::string DescribeBody(const std::string& content_type, const std::string& body)
{
	std::string text = content_type.empty() ? "no content type" : CleanText(content_type);
	return text + ", " + std::to_string(body.size()) + (body.size() == 1 ? " byte" : " bytes");
}

// The OAuth2 error of an answer (RFC 6749 section 5.2, and the providers'
// additions)
void SetOAuthError(const Json& object, long http_status, TokenError& error)
{
	error = TokenError();
	error.kind = TokenError::Kind::OAuth;
	error.http_status = http_status;

	const Json& value = object["error"];
	if (value.is_string())
	{
		error.error = CleanText(value.get<std::string>());
		error.description = CleanText(StringField(object, "error_description"));
	}
	else if (value.is_object())
	{
		// Non-standard form reported for Yahoo (unverified):
		// {"error": {"errorId": "...", "message": "..."}}
		error.error = CleanText(StringField(value, "errorId"));
		error.description = CleanText(StringField(value, "message"));
		if (error.description.empty())
			error.description = CleanText(StringField(value, "localizedMessage"));
	}
	if (error.error.empty())
		error.error = "unknown_error";

	error.uri = CleanText(StringField(object, "error_uri"));
	error.subtype = CleanText(StringField(object, "error_subtype"));	// Google
	if (error.subtype.empty())
		error.subtype = CleanText(StringField(object, "suberror"));		// Microsoft
	error.aadsts = FindAadsts(object, error.description);
	error.correlation_id = CleanText(StringField(object, "correlation_id"));

	error.message = error.error;
	if (!error.subtype.empty())
		error.message += " (" + error.subtype + ")";
	if (!error.description.empty())
		error.message += ": " + error.description;
}

} // namespace

bool TokenError::RequiresSignIn() const
{
	return kind == Kind::OAuth && error == "invalid_grant";
}

bool TokenError::IsClientProblem() const
{
	return kind == Kind::OAuth && (error == "invalid_client" || error == "unauthorized_client");
}

const char* KindName(TokenError::Kind kind)
{
	switch (kind)
	{
	case TokenError::Kind::None:		return "none";
	case TokenError::Kind::Config:		return "config";
	case TokenError::Kind::Network:		return "network";
	case TokenError::Kind::Timeout:		return "timeout";
	case TokenError::Kind::Cancelled:	return "cancelled";
	case TokenError::Kind::Http:		return "http";
	case TokenError::Kind::Protocol:	return "protocol";
	case TokenError::Kind::OAuth:		return "oauth";
	}
	return "unknown";
}

TokenEndpoint TokenEndpointOf(const Provider& provider)
{
	TokenEndpoint endpoint;
	endpoint.url = provider.ResolveEndpoint(provider.token_endpoint);
	endpoint.client_id = provider.client_id;
	endpoint.client_secret = provider.client_secret;
	endpoint.redirect_domains = provider.redirect_domains;
	return endpoint;
}

bool ParseTokenResponse(long http_status, const std::string& content_type,
	const std::string& body, std::time_t received_at, TokenResponse& response,
	TokenError& error)
{
	response = TokenResponse();
	error = TokenError();
	error.http_status = http_status;

	// Filled here, copied to response only when everything is valid
	TokenResponse parsed;
	Json json;
	try
	{
		json = Json::parse(body, nullptr, false);
	}
	catch (const std::exception&)
	{
		json = Json(Json::value_t::discarded);
	}
	const bool object = !json.is_discarded() && json.is_object();

	// An OAuth2 error, whatever the status (normally 400 or 401)
	if (object && json.contains("error"))
	{
		SetOAuthError(json, http_status, error);
		return false;
	}
	if (http_status >= 300 && http_status < 400)
	{
		SetError(error, TokenError::Kind::Http, "HTTP " + std::to_string(http_status) +
			" from the token endpoint: a redirect that was not followed");
		return false;
	}
	if (http_status != 200)
	{
		SetError(error, TokenError::Kind::Http, "HTTP " + std::to_string(http_status) +
			" from the token endpoint (" + DescribeBody(content_type, body) + ")");
		return false;
	}
	// Never the parser's message: it quotes the text, which may hold a token
	if (!object)
	{
		SetError(error, TokenError::Kind::Protocol, "the token endpoint's answer is not a JSON object (" +
			DescribeBody(content_type, body) + ")");
		return false;
	}

	try
	{
		parsed.received_at = received_at;
		parsed.access_token = StringField(json, "access_token");
		if (parsed.access_token.empty())
		{
			SetError(error, TokenError::Kind::Protocol, "the token endpoint's answer has no access_token");
			return false;
		}

		const auto type = json.find("token_type");
		if (type == json.end() ||
			(type->is_string() && EqualsNoCase(type->get<std::string>(), "Bearer")))
			parsed.token_type = "Bearer";
		else
		{
			SetError(error, TokenError::Kind::Protocol, "unsupported token_type " +
				(type->is_string() ? "\"" + CleanText(type->get<std::string>()) + "\"" : type->type_name()) +
				" (only Bearer is supported)");
			return false;
		}

		const auto expires = json.find("expires_in");
		if (expires != json.end() && !expires->is_null() && !ParseSeconds(*expires, parsed.expires_in))
		{
			SetError(error, TokenError::Kind::Protocol, "the token endpoint's expires_in is not a number of seconds");
			return false;
		}
		const auto refresh_expires = json.find("refresh_token_expires_in");
		if (refresh_expires != json.end() && !ParseSeconds(*refresh_expires, parsed.refresh_token_expires_in))
			parsed.refresh_token_expires_in = -1;

		parsed.refresh_token = StringField(json, "refresh_token");
		parsed.id_token = StringField(json, "id_token");
		const auto scope = json.find("scope");
		if (scope != json.end() && scope->is_string())
		{
			parsed.scope_given = true;
			parsed.scopes = SplitWords(scope->get<std::string>());
		}
	}
	catch (const std::exception&)
	{
		// get<>() on an unexpected type: should not happen after the checks
		SetError(error, TokenError::Kind::Protocol, "unexpected value in the token endpoint's answer");
		return false;
	}
	response = std::move(parsed);
	return true;
}

std::string RedactJsonBody(const std::string& body)
{
	try
	{
		Json json = Json::parse(body, nullptr, false);
		if (json.is_discarded() || !json.is_object())
			return "(" + std::to_string(body.size()) + " bytes, not JSON)";
		for (const std::string& name : SecretFieldNames())
		{
			auto it = json.find(name);
			if (it != json.end())
				*it = it->is_string() ? Redact(it->get<std::string>()) : std::string("<redacted>");
		}
		return json.dump();
	}
	catch (const std::exception&)
	{
		return "(" + std::to_string(body.size()) + " bytes, not shown)";
	}
}

bool RequestToken(const TokenEndpoint& endpoint, const Params& grant,
	const HttpSettings& settings, TokenResponse& response, TokenError& error)
{
	response = TokenResponse();
	error = TokenError();
	const auto log = [&settings](const std::string& line) { if (settings.log) settings.log(line); };

	if (endpoint.client_id.empty())
	{
		SetError(error, TokenError::Kind::Config, "no client ID for this provider");
		log("* token request refused: " + error.message);
		return false;
	}

	Params form = grant;
	form.emplace_back("client_id", endpoint.client_id);
	if (!endpoint.client_secret.empty())
		form.emplace_back("client_secret", endpoint.client_secret);

	const std::time_t sent_at = std::time(nullptr);
	HttpResult result;
	if (!PostForm(endpoint.url, form, endpoint.redirect_domains, settings, result))
	{
		SetTransportError(error, result, "token endpoint", "token request");
		return false;
	}

	if (!result.body.empty())
		log("< " + RedactJsonBody(result.body));
	if (!ParseTokenResponse(result.status, result.content_type, result.body, sent_at, response, error))
	{
		log("* token request failed: " + error.message);
		return false;
	}

	std::string summary = "* token received: " + response.token_type;
	summary += response.expires_in >= 0 ? ", expires in " + std::to_string(response.expires_in) + " s" :
		", expiry not given";
	summary += response.refresh_token.empty() ? ", no refresh token" : ", refresh token " +
		Redact(response.refresh_token);
	if (response.scope_given)
		summary += ", scope \"" + JoinWords(response.scopes) + "\"";
	log(summary);
	return true;
}

bool ExchangeCode(const TokenEndpoint& endpoint, const std::string& code,
	const std::string& code_verifier, const std::string& redirect_uri,
	const HttpSettings& settings, TokenResponse& response, TokenError& error)
{
	response = TokenResponse();
	error = TokenError();
	if (code.empty())
		SetError(error, TokenError::Kind::Config, "no authorization code");
	else if (redirect_uri.empty())
		SetError(error, TokenError::Kind::Config, "no redirect URI");
	else if (!code_verifier.empty() && !IsValidPkceVerifier(code_verifier))
		SetError(error, TokenError::Kind::Config, "invalid PKCE code verifier");
	if (error.kind != TokenError::Kind::None)
		return false;

	Params grant = {
		{"grant_type", "authorization_code"},
		{"code", code},
		{"redirect_uri", redirect_uri}
	};
	if (!code_verifier.empty())
		grant.emplace_back("code_verifier", code_verifier);
	return RequestToken(endpoint, grant, settings, response, error);
}

namespace
{

// client_check "token"
bool ProbeWithCode(const Provider& provider, const std::string& redirect_uri,
	const HttpSettings& settings, TokenError& error)
{
	if (settings.log)
		settings.log("* checking the client registration with an invalid code");
	TokenResponse response;
	if (ExchangeCode(TokenEndpointOf(provider), cProbeCode, MakePkce().verifier, redirect_uri, settings,
		response, error))
	{
		// Accepted: impossible with a real server, harmless anyway
		error = TokenError();
		return true;
	}
	switch (error.kind)
	{
	case TokenError::Kind::OAuth:
		return !error.IsClientProblem();
	case TokenError::Kind::Http:
	case TokenError::Kind::Protocol:
		return true;
	default:
		return false;
	}
}

// client_check "device_authorization" (RFC 8628 section 3.1).  Microsoft
// answers a client it does not know with unauthorized_client AADSTS700016
// ("application not found": /consumers, a tenant) or with invalid_request
// AADSTS50059 ("no tenant-identifying information": /common and
// /organizations, where only the application would tell the tenant).  The
// secret is not sent: the request only has to name the client.
bool ProbeWithDeviceCode(const Provider& provider, const std::vector<std::string>& scopes,
	const HttpSettings& settings, TokenError& error)
{
	error = TokenError();
	const auto log = [&settings](const std::string& line) { if (settings.log) settings.log(line); };
	log("* checking the client registration with a device code request (the code is not used)");
	if (provider.client_id.empty())
	{
		SetError(error, TokenError::Kind::Config, "no client ID for this provider");
		return false;
	}

	Params form = { {"client_id", provider.client_id} };
	if (!scopes.empty())
		form.emplace_back("scope", JoinWords(scopes));
	HttpResult result;
	if (!PostForm(provider.ResolveEndpoint(provider.device_authorization_endpoint), form,
		provider.redirect_domains, settings, result))
	{
		SetTransportError(error, result, "device authorization endpoint", "device code request");
		return false;
	}
	if (!result.body.empty())
		log("< " + RedactJsonBody(result.body));

	// A device code: the provider knows the client
	if (result.status == 200)
		return true;

	TokenResponse response;
	if (ParseTokenResponse(result.status, result.content_type, result.body, std::time(nullptr), response,
		error))
	{
		error = TokenError();
		return true;
	}
	if (error.kind == TokenError::Kind::OAuth &&
		(error.IsClientProblem() || error.aadsts == 50059 || error.aadsts == 700016))
	{
		error.message = "the provider does not know this client ID (" + error.message + ")";
		return false;
	}
	// Another refusal (a scope, a disabled device flow...): not the client's
	return true;
}

} // namespace

bool ProbeClient(const Provider& provider, const std::vector<std::string>& scopes,
	const std::string& redirect_uri, const HttpSettings& settings, TokenError& error)
{
	if (provider.client_check == cClientCheckDevice && provider.SupportsDeviceCode())
		return ProbeWithDeviceCode(provider, scopes, settings, error);
	return ProbeWithCode(provider, redirect_uri, settings, error);
}

bool RefreshToken(const TokenEndpoint& endpoint, const std::string& refresh_token,
	const std::vector<std::string>& scopes, const HttpSettings& settings,
	TokenResponse& response, TokenError& error)
{
	response = TokenResponse();
	error = TokenError();
	if (refresh_token.empty())
	{
		SetError(error, TokenError::Kind::Config, "no refresh token");
		return false;
	}

	Params grant = {
		{"grant_type", "refresh_token"},
		{"refresh_token", refresh_token}
	};
	if (!scopes.empty())
		grant.emplace_back("scope", JoinWords(scopes));
	return RequestToken(endpoint, grant, settings, response, error);
}

bool Tokens::NeedsRefresh(std::time_t now, long margin) const
{
	return access_token.empty() || expires_at == 0 || expires_at - margin <= now;
}

bool UpdateTokens(Tokens& tokens, const TokenResponse& response)
{
	tokens.access_token = response.access_token;
	tokens.expires_at = response.expires_in >= 0 ? response.received_at + response.expires_in : 0;
	if (response.scope_given)
		tokens.scopes = response.scopes;
	if (!response.id_token.empty())
		tokens.id_token = response.id_token;
	if (response.refresh_token.empty() || response.refresh_token == tokens.refresh_token)
		return false;
	tokens.refresh_token = response.refresh_token;
	return true;
}

} // namespace oauth2
