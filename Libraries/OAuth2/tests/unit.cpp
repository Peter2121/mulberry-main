/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

// oauth2-unit: offline unit tests of the OAuth2 core, run by "gmake check".
// No network and no account are needed.  Each task of stage 1 of
// OAUTH2-PLAN.md adds its tests here.

#include "oauth2/Authorization.h"
#include "oauth2/DeviceCode.h"
#include "oauth2/Encoding.h"
#include "oauth2/IdToken.h"
#include "oauth2/Identity.h"
#include "oauth2/Listener.h"
#include "oauth2/Paths.h"
#include "oauth2/Permissions.h"
#include "oauth2/Pkce.h"
#include "oauth2/ProviderConfig.h"
#include "oauth2/Random.h"
#include "oauth2/Redact.h"
#include "oauth2/TokenClient.h"
#include "oauth2/Version.h"

#include "HttpClient.h"

#include <nlohmann/json.hpp>

#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

namespace
{

int sChecks = 0;
int sFailures = 0;

void Check(bool ok, const char* what, const char* file, int line)
{
	++sChecks;
	if (!ok)
	{
		++sFailures;
		std::fprintf(stderr, "%s:%d: FAILED: %s\n", file, line, what);
	}
}

#define CHECK(expr) Check((expr), #expr, __FILE__, __LINE__)

void TestVersion()
{
	CHECK(std::string(oauth2::LibraryVersion()) == "0.1.0");
	CHECK(!oauth2::CurlVersion().empty());
}

void TestIdentityId()
{
	CHECK(oauth2::IsValidIdentityId("mulberry"));
	CHECK(oauth2::IsValidIdentityId("my-mail.client_2"));
	CHECK(oauth2::IsValidIdentityId("2mail"));
	CHECK(!oauth2::IsValidIdentityId(""));
	CHECK(!oauth2::IsValidIdentityId("Mulberry"));
	CHECK(!oauth2::IsValidIdentityId("-mail"));
	CHECK(!oauth2::IsValidIdentityId(".mulberry"));
	CHECK(!oauth2::IsValidIdentityId("mul/berry"));
	CHECK(!oauth2::IsValidIdentityId("mul berry"));
	CHECK(!oauth2::IsValidIdentityId(".."));
}

std::string Bytes(std::initializer_list<int> values)
{
	std::string result;
	for (int v : values)
		result += static_cast<char>(v);
	return result;
}

// Encode, check the expected text, decode back
void CheckBase64Url(const std::string& data, const std::string& expected)
{
	CHECK(oauth2::Base64UrlEncode(data) == expected);
	std::string decoded = "unchanged";
	CHECK(oauth2::Base64UrlDecode(expected, decoded));
	CHECK(decoded == data);
}

void TestBase64Url()
{
	// RFC 4648 section 10, without the padding
	CheckBase64Url("", "");
	CheckBase64Url("f", "Zg");
	CheckBase64Url("fo", "Zm8");
	CheckBase64Url("foo", "Zm9v");
	CheckBase64Url("foob", "Zm9vYg");
	CheckBase64Url("fooba", "Zm9vYmE");
	CheckBase64Url("foobar", "Zm9vYmFy");
	// The URL-safe alphabet: standard "+/8=" is "-_8"
	CheckBase64Url(Bytes({0xFB, 0xFF}), "-_8");
	CheckBase64Url(Bytes({0x00, 0xFF, 0x80, 0x7F}), "AP-Afw");

	// Round trips of every length and byte value
	for (size_t length = 0; length <= 64; ++length)
	{
		std::string data;
		for (size_t i = 0; i < length; ++i)
			data += static_cast<char>((i * 37 + length * 11) & 0xFF);
		std::string decoded;
		CHECK(oauth2::Base64UrlDecode(oauth2::Base64UrlEncode(data), decoded) && decoded == data);
	}
	std::string all;
	for (int i = 0; i < 256; ++i)
		all += static_cast<char>(i);
	std::string decoded;
	CHECK(oauth2::Base64UrlDecode(oauth2::Base64UrlEncode(all), decoded) && decoded == all);

	// Refused: padding, the standard alphabet, other characters, a length
	// of 4n+1, non-zero unused bits; the output is left unchanged
	const char* const bad[] = { "Zm9v=", "Zg==", "Zm+v", "Zm/v", "Zm9 v", "Zm9v\n", "Z",
		"Zm9vY", "Zh", "Zm9" };
	for (const char* text : bad)
	{
		std::string out = "unchanged";
		CHECK(!oauth2::Base64UrlDecode(text, out) && out == "unchanged");
	}
}

void TestPercentEncoding()
{
	// Unreserved characters are kept, everything else is %XX in upper case
	CHECK(oauth2::PercentEncode("AZaz09-._~") == "AZaz09-._~");
	CHECK(oauth2::PercentEncode("a b") == "a%20b");
	CHECK(oauth2::FormEncode("a b") == "a+b");
	// Reserved characters of RFC 3986 and the form syntax
	CHECK(oauth2::FormEncode(":/?#[]@!$&'()*+,;=%") ==
		"%3A%2F%3F%23%5B%5D%40%21%24%26%27%28%29%2A%2B%2C%3B%3D%25");
	CHECK(oauth2::PercentEncode(":/?#[]@!$&'()*+,;=%") ==
		"%3A%2F%3F%23%5B%5D%40%21%24%26%27%28%29%2A%2B%2C%3B%3D%25");
	// UTF-8 bytes, control characters
	CHECK(oauth2::FormEncode("\xC3\xA9\t\n") == "%C3%A9%09%0A");
	// A typical scope list and redirect URI
	CHECK(oauth2::FormEncode("https://mail.google.com/ openid") ==
		"https%3A%2F%2Fmail.google.com%2F+openid");
	CHECK(oauth2::PercentEncode("http://127.0.0.1:53682/") == "http%3A%2F%2F127.0.0.1%3A53682%2F");

	std::string out;
	CHECK(oauth2::PercentDecode("a%20b+c%2b%2B%c3%A9", out, true) && out == "a b c++\xC3\xA9");
	CHECK(oauth2::PercentDecode("a+b", out, false) && out == "a+b");
	const char* const bad[] = { "%", "%4", "a%zz", "%G0", "abc%2" };
	for (const char* text : bad)
	{
		out = "unchanged";
		CHECK(!oauth2::PercentDecode(text, out, true) && out == "unchanged");
	}

	// Every byte value survives a round trip in both forms
	std::string all;
	for (int i = 0; i < 256; ++i)
		all += static_cast<char>(i);
	CHECK(oauth2::PercentDecode(oauth2::FormEncode(all), out, true) && out == all);
	CHECK(oauth2::PercentDecode(oauth2::PercentEncode(all), out, false) && out == all);
}

void TestParams()
{
	const oauth2::Params params = {
		{ "grant_type", "authorization_code" },
		{ "redirect_uri", "http://127.0.0.1:5000/" },
		{ "scope", "a b&c=d" },
		{ "empty", "" }
	};
	CHECK(oauth2::BuildForm(params) ==
		"grant_type=authorization_code&redirect_uri=http%3A%2F%2F127.0.0.1%3A5000%2F"
		"&scope=a+b%26c%3Dd&empty=");
	CHECK(oauth2::BuildQuery(params) ==
		"grant_type=authorization_code&redirect_uri=http%3A%2F%2F127.0.0.1%3A5000%2F"
		"&scope=a%20b%26c%3Dd&empty=");

	oauth2::Params parsed;
	CHECK(oauth2::ParseQuery(oauth2::BuildForm(params), parsed) && parsed == params);
	CHECK(oauth2::ParseQuery(oauth2::BuildQuery(params), parsed) && parsed == params);

	CHECK(oauth2::ParseQuery("code=4%2F0Ab&state=x+y&&flag&e=", parsed));
	const oauth2::Params expected = { { "code", "4/0Ab" }, { "state", "x y" }, { "flag", "" },
		{ "e", "" } };
	CHECK(parsed == expected);
	CHECK(oauth2::ParseQuery("", parsed) && parsed.empty());

	parsed = expected;
	CHECK(!oauth2::ParseQuery("code=%zz", parsed) && parsed == expected);
}

void TestPkce()
{
	// RFC 7636 appendix B
	const std::string verifierBytes = Bytes({ 116, 24, 223, 180, 151, 153, 224, 37, 79, 250,
		96, 125, 216, 173, 187, 186, 22, 212, 37, 77, 105, 214, 191, 240, 91, 88, 5, 88, 83,
		132, 141, 121 });
	const std::string verifier = "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk";
	CHECK(oauth2::Base64UrlEncode(verifierBytes) == verifier);
	CHECK(oauth2::PkceChallengeS256(verifier) == "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM");
	CHECK(oauth2::IsValidPkceVerifier(verifier));
	CHECK(std::string(oauth2::Pkce::cMethod) == "S256");

	// New verifiers: valid, 43 characters, different each time, matching
	// challenge
	std::set<std::string> seen;
	for (int i = 0; i < 100; ++i)
	{
		const oauth2::Pkce pkce = oauth2::MakePkce();
		CHECK(pkce.verifier.size() == 43);
		CHECK(oauth2::IsValidPkceVerifier(pkce.verifier));
		CHECK(pkce.challenge == oauth2::PkceChallengeS256(pkce.verifier));
		CHECK(pkce.challenge.size() == 43);
		seen.insert(pkce.verifier);
	}
	CHECK(seen.size() == 100);

	CHECK(!oauth2::IsValidPkceVerifier(std::string(42, 'a')));
	CHECK(oauth2::IsValidPkceVerifier(std::string(128, 'a')));
	CHECK(!oauth2::IsValidPkceVerifier(std::string(129, 'a')));
	CHECK(!oauth2::IsValidPkceVerifier(std::string(42, 'a') + "+"));
}

void TestRandom()
{
	CHECK(oauth2::RandomBytes(0).empty());
	CHECK(oauth2::RandomBytes(32).size() == 32);
	CHECK(oauth2::RandomBytes(32) != oauth2::RandomBytes(32));

	std::set<std::string> states;
	for (int i = 0; i < 100; ++i)
	{
		const std::string state = oauth2::MakeState();
		CHECK(state.size() == 22);
		std::string decoded;
		CHECK(oauth2::Base64UrlDecode(state, decoded) && decoded.size() == 16);
		states.insert(state);
	}
	CHECK(states.size() == 100);

	CHECK(oauth2::ConstantTimeEquals("", ""));
	CHECK(oauth2::ConstantTimeEquals("abc", "abc"));
	CHECK(!oauth2::ConstantTimeEquals("abc", "abd"));
	CHECK(!oauth2::ConstantTimeEquals("abc", "abcd"));
	CHECK(!oauth2::ConstantTimeEquals("", "a"));
}

void TestRedact()
{
	CHECK(oauth2::Redact("") == "<redacted 0 bytes>");
	CHECK(oauth2::Redact("x") == "<redacted 1 byte>");
	CHECK(oauth2::Redact("ya29.secret") == "<redacted 11 bytes>");

	// A token request body: only the secret values change
	CHECK(oauth2::RedactParams("grant_type=authorization_code&code=4%2F0AbC&code_verifier=dBjf"
		"&client_id=123.apps&client_secret=GOCSPX-x&redirect_uri=http%3A%2F%2F127.0.0.1") ==
		"grant_type=authorization_code&code=<redacted 8 bytes>&code_verifier=<redacted 4 bytes>"
		"&client_id=123.apps&client_secret=<redacted 8 bytes>&redirect_uri=http%3A%2F%2F127.0.0.1");
	CHECK(oauth2::RedactParams("grant_type=refresh_token&refresh_token=1%2F%2F0g") ==
		"grant_type=refresh_token&refresh_token=<redacted 9 bytes>");
	// Encoded names are recognised; empty parts and parts without '=' kept
	CHECK(oauth2::RedactParams("c%6Fde=abc&&flag&state=s") ==
		"c%6Fde=<redacted 3 bytes>&&flag&state=s");
	// A name that only starts like a secret one is kept
	CHECK(oauth2::RedactParams("codes=1&code_challenge=x") == "codes=1&code_challenge=x");
	// Custom list
	CHECK(oauth2::RedactParams("a=1&b=22", { "b" }) == "a=1&b=<redacted 2 bytes>");
	CHECK(oauth2::RedactParams("") == "");
}

// The vendored nlohmann/json: strict parsing as section 7 of the plan
// requires (no comments, no trailing commas), and no implicit conversions
void TestJson()
{
	const nlohmann::json value = nlohmann::json::parse(R"({"version": 1, "a": ["x"]})");
	CHECK(value.at("version").get<int>() == 1);
	CHECK(value.at("a").at(0).get<std::string>() == "x");

	const char* const bad[] = { R"({"a": 1,})", R"([1, 2,])", "{\"a\": 1} // c",
		"/* c */ {\"a\": 1}", "{'a': 1}", "{\"a\": 1} x", "" };
	for (const char* text : bad)
	{
		bool refused = false;
		try
		{
			const nlohmann::json parsed = nlohmann::json::parse(text);
			(void)parsed;
		}
		catch (const nlohmann::json::parse_error&)
		{
			refused = true;
		}
		CHECK(refused);
	}
}

// ---------------------------------------------------------------------------
// Provider configuration (task 1.3)
// ---------------------------------------------------------------------------

const char* const cShippedFile = "data/oauth2-providers.conf.json";

std::string ReadText(const std::string& path)
{
	std::ifstream in(path, std::ios::binary);
	std::ostringstream text;
	text << in.rdbuf();
	return text.str();
}

void WriteText(const std::string& path, const std::string& text, mode_t mode)
{
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	out << text;
	out.close();
	chmod(path.c_str(), mode);
}

mode_t Mode(const std::string& path)
{
	struct stat info;
	return stat(path.c_str(), &info) == 0 ? (info.st_mode & 07777) : 0;
}

// A small valid system file for the error tests
const char* const cSystem = R"({
  "version": 1,
  "providers": {
    "example": {
      "name": "Example",
      "hosts": ["imap.example.com", "smtp.example.com"],
      "domains": ["example.com"],
      "authorization_endpoint": "https://auth.example.com/authorize",
      "token_endpoint": "https://auth.example.com/token",
      "scopes": { "mail": ["mail"], "calendar": ["cal"] },
      "redirect": "http://127.0.0.1"
    }
  }
})";

// Loads cSystem and a user text; checks that it fails with the message part
// at line:column of the given file
void ExpectError(const std::string& system, const std::string& user, const std::string& file,
	size_t line, size_t column, const std::string& part, int sourceLine)
{
	oauth2::ProviderConfig config;
	oauth2::ConfigError error;
	const bool ok = config.LoadTexts("system.json", system, "user.json", user, error);
	const bool good = !ok && error.file == file && error.line == line && error.column == column &&
		error.message.find(part) != std::string::npos;
	Check(good, ("error \"" + part + "\" at " + file + ":" + std::to_string(line) + ":" +
		std::to_string(column)).c_str(), __FILE__, sourceLine);
	if (!good)
		std::fprintf(stderr, "    got: %s\n", ok ? "(no error)" : error.ToString().c_str());
}

#define EXPECT_USER_ERROR(user, line, column, part) \
	ExpectError(cSystem, user, "user.json", line, column, part, __LINE__)

void TestShippedFile()
{
	oauth2::ProviderConfig config;
	oauth2::ConfigError error;
	const std::string text = ReadText(cShippedFile);
	CHECK(!text.empty());
	const bool ok = config.LoadTexts(cShippedFile, text, "", "", error);
	CHECK(ok);
	if (!ok)
	{
		std::fprintf(stderr, "    %s\n", error.ToString().c_str());
		return;
	}
	CHECK(config.SystemFileFound());

	// The plan's hostname lookups
	struct { const char* host; const char* provider; } const lookups[] = {
		{ "imap.gmail.com", "google" }, { "outlook.office365.com", "microsoft" },
		{ "smtp-mail.outlook.com", "microsoft" }, { "imap.mail.yahoo.com", "yahoo" },
		{ "smtp.gmail.com", "google" }, { "pop.gmail.com", "google" },
		{ "apidata.googleusercontent.com", "google" }, { "smtp.office365.com", "microsoft" },
		{ "IMAP.Gmail.COM.", "google" }, { "imap.yandex.com", "yandex" },
		{ "imap.mail.ru", "mailru" }, { "imap.zoho.com", "zoho" } };
	for (const auto& lookup : lookups)
	{
		const oauth2::Provider* p = config.FindByHost(lookup.host);
		CHECK(p && p->id == lookup.provider);
	}
	CHECK(!config.FindByHost("imap.example.org"));
	CHECK(!config.FindByHost("gmail.com"));
	CHECK(!config.FindByHost(""));

	CHECK(config.FindByEmail("someone@gmail.com") && config.FindByEmail("someone@gmail.com")->id == "google");
	CHECK(config.FindByEmail("A.B@Outlook.COM") && config.FindByEmail("A.B@Outlook.COM")->id == "microsoft");
	CHECK(config.FindByEmail("x@yahoo.com") && config.FindByEmail("x@yahoo.com")->id == "yahoo");
	CHECK(!config.FindByEmail("someone@example.org"));
	CHECK(!config.FindByEmail("no-at-sign"));
	CHECK(!config.FindByEmail("trailing@"));

	// No client ID is shipped
	for (const oauth2::Provider& p : config.Providers())
	{
		CHECK(p.client_id.empty());
		CHECK(p.client_secret.empty());
		CHECK(!p.redirect.empty());
		CHECK(!p.sasl.empty());
	}
	CHECK(config.Providers().size() == 6);

	const oauth2::Provider* google = config.Find("google");
	CHECK(google && !google->SupportsDeviceCode());
	CHECK(google && google->authorization_params.at("access_type") == "offline");
	// A refresh token only comes with the consent screen (task 1.6)
	CHECK(google && google->authorization_params.at("prompt") == "consent");
	CHECK(google && google->ScopesFor({ oauth2::cScopeMail, oauth2::cScopeCalendar }) ==
		std::vector<std::string>({ "openid", "email", "https://mail.google.com/",
			"https://www.googleapis.com/auth/calendar" }));
	CHECK(google && google->sasl.front() == "OAUTHBEARER");

	const oauth2::Provider* microsoft = config.Find("microsoft");
	CHECK(microsoft && microsoft->SupportsDeviceCode());
	CHECK(microsoft && microsoft->ResolveEndpoint(microsoft->token_endpoint) ==
		"https://login.microsoftonline.com/common/oauth2/v2.0/token");
	CHECK(microsoft && microsoft->redirect == "http://localhost");
	// Its token endpoint checks a code before the client (task 1.6)
	CHECK(microsoft && microsoft->client_check == oauth2::cClientCheckDevice);
	CHECK(microsoft && microsoft->client_id_format == oauth2::cClientIdUuid);
	CHECK(google && google->client_check == oauth2::cClientCheckToken && google->client_id_format.empty());
	CHECK(microsoft && microsoft->ScopesFor({ oauth2::cScopeCalendar }) ==
		std::vector<std::string>({ "offline_access", "openid", "email", "profile" }));

	// Level 3: the account's own settings
	if (microsoft)
	{
		oauth2::AccountSettings settings;
		settings.client_id = "account-id";
		settings.tenant = "contoso.onmicrosoft.com";
		const oauth2::Provider p = oauth2::WithAccountSettings(*microsoft, settings);
		CHECK(p.client_id == "account-id");
		CHECK(p.client_secret.empty());
		CHECK(p.ResolveEndpoint(p.authorization_endpoint) ==
			"https://login.microsoftonline.com/contoso.onmicrosoft.com/oauth2/v2.0/authorize");
	}
}

// client_id_format and client_check (task 1.6)
void TestClientChecks()
{
	oauth2::ProviderConfig config;
	oauth2::ConfigError error;
	CHECK(config.LoadTexts("system.json", cSystem, "user.json",
		R"({"version": 1, "providers": {"example": {"client_id_format": "uuid",)"
		R"( "client_check": "device_authorization", "device_authorization_endpoint": "https://a.test/d"}}})",
		error));
	const oauth2::Provider* p = config.Find("example");
	CHECK(p && p->client_id_format == "uuid" && p->client_check == "device_authorization");
	CHECK(config.LoadTexts("system.json", cSystem, "", "", error));
	p = config.Find("example");
	CHECK(p && p->client_id_format.empty() && p->client_check == "token");

	oauth2::Provider ms;
	ms.name = "Microsoft";
	ms.client_id_format = oauth2::cClientIdUuid;
	std::string message;
	for (const char* good : { "781465e6-6d7e-4788-95da-b70ee25bc2a5", "781465E6-6D7E-4788-95DA-B70EE25BC2A5", "" })
	{
		ms.client_id = good;
		message.clear();
		CHECK(oauth2::CheckClientIdFormat(ms, message) && message.empty());
	}
	for (const char* bad : { "wrong-id", "781465e6-6d7e-4788-95da-b70ee25bc2a", "781465e6-6d7e-4788-95da-b70ee25bc2a5 ",
		"781465e66d7e-4788-95da-b70ee25bc2a5-", "781465g6-6d7e-4788-95da-b70ee25bc2a5",
		"{781465e6-6d7e-4788-95da-b70ee25bc2a}", "S3CR3T~secret.pasted.as.the.client.id" })
	{
		ms.client_id = bad;
		CHECK(!oauth2::CheckClientIdFormat(ms, message));
		// The value is not repeated (a pasted secret)
		CHECK(message.find(bad) == std::string::npos && message.find("S3CR3T") == std::string::npos);
	}
	ms.client_id = "wrong-id";
	CHECK(oauth2::CheckClientIdFormat(ms, message) == false && message == "the client ID (8 characters) is not in "
		"Microsoft's form xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx (hexadecimal digits)");
	// No format: anything
	ms.client_id_format.clear();
	CHECK(oauth2::CheckClientIdFormat(ms, message));
}

void TestMerge()
{
	oauth2::ProviderConfig config;
	oauth2::ConfigError error;

	// A user client_id replaces nothing else
	CHECK(config.LoadTexts("system.json", cSystem, "user.json",
		R"({"version": 1, "providers": {"example": {"client_id": "abc", "client_secret": "s"}}})", error));
	const oauth2::Provider* p = config.Find("example");
	CHECK(p && p->client_id == "abc" && p->client_secret == "s");
	CHECK(p && p->name == "Example" && p->token_endpoint == "https://auth.example.com/token");
	CHECK(p && p->hosts.size() == 2 && p->scopes.size() == 2 && p->pkce);
	CHECK(p && p->sasl == std::vector<std::string>({ "XOAUTH2" }));

	// Objects merge key by key; arrays are replaced whole
	CHECK(config.LoadTexts("system.json", cSystem, "user.json",
		R"({"version": 1, "providers": {"example": {"scopes": {"mail": ["m2"]}, "hosts": ["mx.example.com"]}}})",
		error));
	p = config.Find("example");
	CHECK(p && p->scopes.at("mail") == std::vector<std::string>({ "m2" }));
	CHECK(p && p->scopes.at("calendar") == std::vector<std::string>({ "cal" }));
	CHECK(p && p->hosts == std::vector<std::string>({ "mx.example.com" }));
	CHECK(config.FindByHost("mx.example.com") == p);
	CHECK(!config.FindByHost("imap.example.com"));

	// "enabled": false hides a provider
	CHECK(config.LoadTexts("system.json", cSystem, "user.json",
		R"({"version": 1, "providers": {"example": {"enabled": false}}})", error));
	CHECK(!config.Find("example") && !config.FindByHost("imap.example.com"));
	CHECK(config.Providers().empty());

	// A complete new provider in the user file, and comments everywhere
	CHECK(config.LoadTexts("system.json", cSystem, "user.json", R"({
	  "_comment": "mine",
	  "version": 1,
	  "providers": {
	    "_comment": "x",
	    "acme": {
	      "_comment": "y",
	      "name": "Acme", "hosts": ["imap.acme.test"],
	      "authorization_endpoint": "https://acme.test:8443/auth?x=1",
	      "token_endpoint": "https://acme.test/token",
	      "scopes": {"_comment": "z", "mail": ["imap"]},
	      "authorization_params": {"_comment": "w", "prompt": "consent"},
	      "redirect": "http://[::1]/acme"
	    }
	  }
	})", error));
	CHECK(config.Providers().size() == 2);
	p = config.FindByHost("imap.acme.test");
	CHECK(p && p->id == "acme" && p->redirect == "http://[::1]/acme");
	CHECK(p && p->authorization_params.size() == 1 && p->scopes.size() == 1);

	// An empty user file object is fine; so is no user file
	CHECK(config.LoadTexts("system.json", cSystem, "user.json", R"({"version": 1})", error));
	CHECK(config.LoadTexts("system.json", cSystem, "", "", error));
	CHECK(config.Providers().size() == 1);

	// No system file: unavailable, but the user file is still checked
	CHECK(config.LoadTexts("", "", "user.json", R"({"version": 1, "providers": {"example": {"client_id": "a"}}})", error));
	CHECK(!config.SystemFileFound() && config.Providers().empty() && !config.FindByHost("imap.example.com"));
	CHECK(!config.LoadTexts("", "", "user.json", R"({"version": 1, "x": 1})", error));
}

void TestConfigErrors()
{
	// Syntax errors (nlohmann/json), with hints
	EXPECT_USER_ERROR("{\n  \"version\": 1,\n}", 3, 1, "remove the comma");
	EXPECT_USER_ERROR("{\n  \"version\": 1,\n  \"providers\": {\"a\": {\"hosts\": [\"x\",]}}\n}", 3, 37,
		"remove the comma");
	EXPECT_USER_ERROR("{\n  // a comment\n  \"version\": 1\n}", 2, 3, "no comments");
	EXPECT_USER_ERROR("{\n  /* a comment */ \"version\": 1\n}", 2, 3, "no comments");
	EXPECT_USER_ERROR("{'version': 1}", 1, 2, "double quotes");
	EXPECT_USER_ERROR("{\"version\": 1", 1, 14, "unexpected end of input");
	EXPECT_USER_ERROR("", 1, 1, "empty input");
	EXPECT_USER_ERROR("{\"version\": 1} x", 1, 16, "syntax error");
	// Columns count characters, not bytes ("x" is the 21st character, the
	// 23rd byte)
	EXPECT_USER_ERROR("{\"_comment\": \"\xC3\xA9t\xC3\xA9\", x}", 1, 21, "syntax error");

	// Duplicate keys
	EXPECT_USER_ERROR("{\"version\": 1,\n \"version\": 1}", 2, 2, "duplicate key \"version\" at the top level");
	EXPECT_USER_ERROR(
		"{\"version\": 1, \"providers\": {\"example\": {\n  \"client_id\": \"a\",\n  \"client_id\": \"b\"}}}",
		3, 3, "duplicate key \"client_id\" in provider \"example\"");

	// Schema errors, at the key or the value
	EXPECT_USER_ERROR("{\"version\": 1, \"providers\": {\"example\": {\n  \"clientid\": \"a\"}}}", 2, 3,
		"unknown key \"clientid\" in provider \"example\"");
	EXPECT_USER_ERROR("{\"version\": 1, \"provider\": {}}", 1, 16, "unknown key \"provider\" at the top level");
	EXPECT_USER_ERROR("{\"version\": 2}", 1, 13, "\"version\" must be 1");
	EXPECT_USER_ERROR("{\"version\": \"1\"}", 1, 13, "\"version\" must be 1");
	EXPECT_USER_ERROR("{\"providers\": {}}", 1, 1, "missing \"version\"");
	EXPECT_USER_ERROR("[1]", 1, 1, "must contain a JSON object");
	EXPECT_USER_ERROR("{\"version\": 1, \"providers\": []}", 1, 29, "\"providers\" must be an object");
	EXPECT_USER_ERROR("{\"version\": 1, \"providers\": {\"Example\": {}}}", 1, 30,
		"provider identifier \"Example\"");
	EXPECT_USER_ERROR("{\"version\": 1, \"providers\": {\"example\": {\"pkce\": \"yes\"}}}", 1, 50,
		"\"pkce\" of provider \"example\" must be true or false");
	EXPECT_USER_ERROR("{\"version\": 1, \"providers\": {\"example\": {\"client_id\": 12}}}", 1, 55,
		"must be a string");
	EXPECT_USER_ERROR("{\"version\": 1, \"providers\": {\"example\": {\"client_id\": \"abc \"}}}", 1, 55,
		"starts or ends with a space");
	EXPECT_USER_ERROR("{\"version\": 1, \"providers\": {\"example\": {\"token_endpoint\": \"http://a.test/t\"}}}",
		1, 60, "must be an https:// URL");
	EXPECT_USER_ERROR("{\"version\": 1, \"providers\": {\"example\": {\"token_endpoint\": \"https://\"}}}",
		1, 60, "must be an https:// URL");
	EXPECT_USER_ERROR("{\"version\": 1, \"providers\": {\"example\": {\"token_endpoint\": \"https://a.test/t t\"}}}",
		1, 60, "must be an https:// URL");
	EXPECT_USER_ERROR("{\"version\": 1, \"providers\": {\"example\": {\"redirect\": \"http://127.0.0.1:8080\"}}}",
		1, 54, "without a port");
	EXPECT_USER_ERROR("{\"version\": 1, \"providers\": {\"example\": {\"redirect\": \"https://localhost\"}}}",
		1, 54, "without a port");
	EXPECT_USER_ERROR("{\"version\": 1, \"providers\": {\"example\": {\"hosts\": [\"IMAP.example.com\"]}}}",
		1, 52, "lower-case name");
	EXPECT_USER_ERROR("{\"version\": 1, \"providers\": {\"example\": {\"hosts\": [\"a.test\", \"a.test\"]}}}",
		1, 62, "listed twice");
	EXPECT_USER_ERROR("{\"version\": 1, \"providers\": {\"example\": {\"redirect_domains\": \"live.com\"}}}",
		1, 62, "must be a list of names");
	EXPECT_USER_ERROR("{\"version\": 1, \"providers\": {\"example\": {\"redirect_domains\": [\"https://live.com\"]}}}",
		1, 63, "lower-case name");
	EXPECT_USER_ERROR("{\"version\": 1, \"providers\": {\"example\": {\"scopes\": {\"notes\": []}}}}", 1, 53,
		"unknown scope group \"notes\"");
	EXPECT_USER_ERROR("{\"version\": 1, \"providers\": {\"example\": {\"scopes\": {\"mail\": [\"a b\"]}}}}", 1, 62,
		"without spaces");
	EXPECT_USER_ERROR("{\"version\": 1, \"providers\": {\"example\": {\"sasl\": [\"PLAIN\"]}}}", 1, 51,
		"\"OAUTHBEARER\" or \"XOAUTH2\"");
	EXPECT_USER_ERROR("{\"version\": 1, \"providers\": {\"example\": {\"sasl\": []}}}", 1, 50, "non-empty list");
	EXPECT_USER_ERROR(
		"{\"version\": 1, \"providers\": {\"example\": {\"authorization_params\": {\"scope\": \"x\"}}}}", 1, 67,
		"set by the application");
	EXPECT_USER_ERROR("{\"version\": 1, \"providers\": {\"example\": {\"_comment\": 1}}}", 1, 54,
		"\"_comment\" must be a string");
	// Several errors: the first in the text is reported, with the count
	EXPECT_USER_ERROR("{\"version\": 1, \"providers\": {\"example\": {\"a\": 1,\n \"b\": 2}}}", 1, 42,
		"(and 1 more error)");

	// After merging: missing required key, host listed twice, {tenant}
	EXPECT_USER_ERROR("{\"version\": 1, \"providers\": {\"acme\": {\"name\": \"Acme\"}}}", 1, 30,
		"provider \"acme\" lacks the required key \"authorization_endpoint\"");
	EXPECT_USER_ERROR("{\"version\": 1, \"providers\": {\"acme\": {\"name\": \"Acme\", \"hosts\": [\"imap.example.com\"],"
		" \"authorization_endpoint\": \"https://a.test/a\", \"token_endpoint\": \"https://a.test/t\","
		" \"scopes\": {\"mail\": [\"m\"]}, \"redirect\": \"http://localhost\"}}}", 1, 55,
		"server \"imap.example.com\" is listed by providers \"example\" and \"acme\"");
	EXPECT_USER_ERROR("{\"version\": 1, \"providers\": {\"example\": {\"token_endpoint\": \"https://a.test/{tenant}/t\"}}}",
		1, 30, "no \"tenant\"");

	// The client checks (task 1.6): known values; a device code check needs
	// a device endpoint
	EXPECT_USER_ERROR("{\"version\": 1, \"providers\": {\"example\": {\"client_id_format\": \"guid\"}}}", 1, 62,
		"\"client_id_format\" of provider \"example\" must be \"uuid\"");
	EXPECT_USER_ERROR("{\"version\": 1, \"providers\": {\"example\": {\"client_check\": \"browser\"}}}", 1, 58,
		"must be \"token\" or \"device_authorization\"");
	EXPECT_USER_ERROR("{\"version\": 1, \"providers\": {\"example\": {\"client_check\": \"device_authorization\"}}}", 1, 42,
		"checks its client with \"device_authorization\" but has no \"device_authorization_endpoint\"");

	// An error in the system file names the system file
	ExpectError("{\"version\": 1,\n \"providers\": {\"x\": {\"name\": \"X\"}}}", "{\"version\": 1}", "system.json",
		2, 16, "lacks the required key", __LINE__);
	ExpectError("{\"version\": 1,}", "{\"version\": 1}", "system.json", 1, 15, "remove the comma", __LINE__);

	// The message format
	oauth2::ConfigError error;
	error.file = "/nonexistent/f.json";
	error.line = 3;
	error.column = 7;
	error.message = "m";
	CHECK(error.ToString() == "/nonexistent/f.json:3:7: m");
	error.line = 0;
	CHECK(error.ToString() == "/nonexistent/f.json: m");
}

// A temporary folder in obj/tests/ (removed at the end)
struct TempDir
{
	std::string path;
	std::vector<std::string> files;
	std::vector<std::string> dirs;

	TempDir()
	{
		char name[] = "obj/tests/tmp-XXXXXX";
		if (mkdtemp(name))
		{
			char* real = realpath(name, nullptr);
			path = real ? real : name;
			std::free(real);
		}
	}
	~TempDir()
	{
		for (auto it = files.rbegin(); it != files.rend(); ++it)
			unlink(it->c_str());
		for (auto it = dirs.rbegin(); it != dirs.rend(); ++it)
		{
			chmod(it->c_str(), 0700);
			rmdir(it->c_str());
		}
		rmdir(path.c_str());
	}
	std::string Dir(const std::string& name, mode_t mode)
	{
		const std::string dir = path + "/" + name;
		mkdir(dir.c_str(), 0700);
		chmod(dir.c_str(), mode);
		dirs.push_back(dir);
		return dir;
	}
	std::string File(const std::string& name, const std::string& text, mode_t mode)
	{
		const std::string file = path + "/" + name;
		WriteText(file, text, mode);
		files.push_back(file);
		return file;
	}
	std::string Link(const std::string& name, const std::string& target)
	{
		const std::string link = path + "/" + name;
		symlink(target.c_str(), link.c_str());
		files.push_back(link);
		return link;
	}
};

bool HasIssue(const std::vector<oauth2::PermissionIssue>& issues, const std::string& part, bool fixable)
{
	for (const oauth2::PermissionIssue& issue : issues)
	{
		if (issue.problem.find(part) != std::string::npos && issue.fixable == fixable)
			return true;
	}
	return false;
}

void TestPermissions()
{
	TempDir tmp;
	CHECK(!tmp.path.empty());
	if (tmp.path.empty())
		return;

	oauth2::PermissionContext context;
	context.home = tmp.Dir("home", 0700);
	tmp.Dir("home/.config", 0700);
	const std::string folder = tmp.Dir("home/.config/app", 0700);
	const std::string file = tmp.File("home/.config/app/oauth2-providers.conf.json", "{}", 0600);
	const std::string missing = folder + "/missing.json";

	// Correct files: nothing; a missing file: nothing
	CHECK(oauth2::CheckPermissions(file, oauth2::FileRole::User, context).empty());
	CHECK(oauth2::CheckPermissions(missing, oauth2::FileRole::User, context).empty());
	std::string error;
	CHECK(oauth2::FixPermissions(missing, error, context));
	chmod(file.c_str(), 0400);
	CHECK(oauth2::CheckPermissions(file, oauth2::FileRole::User, context).empty());

	// 0644 and 0666: a fixable warning; the fix restores 0600
	for (mode_t mode : { mode_t(0644), mode_t(0666), mode_t(0604) })
	{
		chmod(file.c_str(), mode);
		const std::vector<oauth2::PermissionIssue> issues =
			oauth2::CheckPermissions(file, oauth2::FileRole::User, context);
		CHECK(issues.size() == 1 && HasIssue(issues, "readable or writable by group or others", true));
		CHECK(issues.size() == 1 && issues[0].advice.find("chmod 600") != std::string::npos);
		CHECK(oauth2::FixPermissions(file, error, context));
		CHECK(Mode(file) == 0600);
		CHECK(oauth2::CheckPermissions(file, oauth2::FileRole::User, context).empty());
	}

	// A group-writable folder: fixable; the fix makes it 0700
	chmod(folder.c_str(), 0775);
	std::vector<oauth2::PermissionIssue> issues = oauth2::CheckPermissions(file, oauth2::FileRole::User, context);
	CHECK(issues.size() == 1 && HasIssue(issues, "folder writable by group or others", true));
	CHECK(oauth2::FixPermissions(file, error, context));
	CHECK(Mode(folder) == 0700);
	CHECK(oauth2::CheckPermissions(file, oauth2::FileRole::User, context).empty());

	// A foreign owner (simulated): warned, not fixable, the fix refuses
	oauth2::PermissionContext foreign = context;
	foreign.user = getuid() + 1;
	chmod(file.c_str(), 0644);
	issues = oauth2::CheckPermissions(file, oauth2::FileRole::User, foreign);
	CHECK(HasIssue(issues, "not owned by you", false));
	CHECK(HasIssue(issues, "readable or writable by group or others", false));
	error.clear();
	CHECK(!oauth2::FixPermissions(file, error, foreign) && error.find("another user") != std::string::npos);
	CHECK(Mode(file) == 0644);
	CHECK(oauth2::FixPermissions(file, error, context) && Mode(file) == 0600);

	// A symbolic link to a file outside the home folder: warned, not fixed
	const std::string outside = tmp.File("outside.json", "{}", 0600);
	const std::string link = tmp.Link("home/.config/app/linked.json", outside);
	issues = oauth2::CheckPermissions(link, oauth2::FileRole::User, context);
	CHECK(issues.size() == 1 && HasIssue(issues, "symbolic link pointing outside your home folder", false));
	error.clear();
	CHECK(!oauth2::FixPermissions(link, error, context) && error.find("symbolic link") != std::string::npos);
	// A link inside the home folder is accepted
	const std::string inside = tmp.File("home/inside.json", "{}", 0600);
	const std::string link2 = tmp.Link("home/.config/app/linked2.json", inside);
	CHECK(oauth2::CheckPermissions(link2, oauth2::FileRole::User, context).empty());

	// The system file: root-owned (simulated) and 0644 is correct
	oauth2::PermissionContext system = context;
	system.system_owner = getuid();
	const std::string sysFolder = tmp.Dir("etc", 0755);
	const std::string sysFile = tmp.File("etc/oauth2-providers.conf.json", "{}", 0644);
	CHECK(oauth2::CheckPermissions(sysFile, oauth2::FileRole::System, system).empty());
	// Root-owned but group-writable: warned, never fixable
	chmod(sysFile.c_str(), 0664);
	issues = oauth2::CheckPermissions(sysFile, oauth2::FileRole::System, system);
	CHECK(issues.size() == 1 && HasIssue(issues, "writable by group or others", false));
	CHECK(issues.size() == 1 && issues[0].advice.find("as root") != std::string::npos);
	chmod(sysFile.c_str(), 0644);
	chmod(sysFolder.c_str(), 0775);
	issues = oauth2::CheckPermissions(sysFile, oauth2::FileRole::System, system);
	CHECK(issues.size() == 1 && HasIssue(issues, "folder writable by group or others", false));
	chmod(sysFolder.c_str(), 0755);
	// Not owned by root
	system.system_owner = getuid() + 1;
	issues = oauth2::CheckPermissions(sysFile, oauth2::FileRole::System, system);
	CHECK(issues.size() == 1 && HasIssue(issues, "not owned by root", false));

	// Warnings never stop loading: a 0666 user file still loads
	const std::string userFile = tmp.File("home/.config/app/user.json",
		"{\"version\": 1, \"providers\": {\"google\": {\"client_id\": \"id-from-user\"}}}", 0666);
	CHECK(!oauth2::CheckPermissions(userFile, oauth2::FileRole::User, context).empty());
	oauth2::ConfigFiles files;
	files.system_file = cShippedFile;
	files.user_file = userFile;
	oauth2::ProviderConfig config;
	oauth2::ConfigError configError;
	CHECK(config.Load(files, configError));
	CHECK(config.Find("google") && config.Find("google")->client_id == "id-from-user");

	// Missing user file: fine; unreadable or not a file: an error
	files.user_file = missing;
	CHECK(config.Load(files, configError) && config.SystemFileFound());
	files.system_file = folder + "/no-system.json";
	CHECK(config.Load(files, configError) && !config.SystemFileFound());
	files.system_file = cShippedFile;
	files.user_file = folder;
	CHECK(!config.Load(files, configError) && configError.message == "not a regular file");
	if (getuid() != 0)
	{
		chmod(userFile.c_str(), 0);
		files.user_file = userFile;
		CHECK(!config.Load(files, configError) && configError.line == 0 &&
			configError.file == userFile);
		chmod(userFile.c_str(), 0600);
	}
}

void TestPaths()
{
	oauth2::Identity identity;
	identity.id = "my-mail.2";
	CHECK(oauth2::EnvironmentPrefix(identity) == "MY_MAIL_2");
	identity.id = "mulberry";
	CHECK(oauth2::EnvironmentPrefix(identity) == "MULBERRY");
	CHECK(oauth2::SystemConfigVariable(identity) == "MULBERRY_OAUTH2_SYSTEM_CONFIG");

	const char* oldXdg = std::getenv("XDG_CONFIG_HOME");
	const std::string savedXdg = oldXdg ? oldXdg : "";
	const char* oldSystem = std::getenv("MULBERRY_OAUTH2_SYSTEM_CONFIG");
	const std::string savedSystem = oldSystem ? oldSystem : "";

	setenv("XDG_CONFIG_HOME", "/xdg/config", 1);
	unsetenv("MULBERRY_OAUTH2_SYSTEM_CONFIG");
	oauth2::ConfigFiles files = oauth2::DefaultConfigFiles(identity, "/usr/local/etc");
	CHECK(files.user_file == "/xdg/config/mulberry/oauth2-providers.conf.json");
	CHECK(files.system_file == "/usr/local/etc/mulberry/oauth2-providers.conf.json");
	CHECK(!files.system_file_explicit);

	// A relative XDG_CONFIG_HOME is ignored, as the specification says
	setenv("XDG_CONFIG_HOME", "relative", 1);
	files = oauth2::DefaultConfigFiles(identity, "/etc");
	CHECK(files.user_file == oauth2::HomeDirectory() + "/.config/mulberry/oauth2-providers.conf.json");
	CHECK(files.system_file == "/etc/mulberry/oauth2-providers.conf.json");

	setenv("MULBERRY_OAUTH2_SYSTEM_CONFIG", "/build/tree/data.json", 1);
	files = oauth2::DefaultConfigFiles(identity, "/etc");
	CHECK(files.system_file == "/build/tree/data.json" && files.system_file_explicit);

	if (oldXdg)
		setenv("XDG_CONFIG_HOME", savedXdg.c_str(), 1);
	else
		unsetenv("XDG_CONFIG_HOME");
	if (oldSystem)
		setenv("MULBERRY_OAUTH2_SYSTEM_CONFIG", savedSystem.c_str(), 1);
	else
		unsetenv("MULBERRY_OAUTH2_SYSTEM_CONFIG");

	const std::string home = oauth2::HomeDirectory();
	CHECK(!home.empty());
	CHECK(oauth2::DisplayPath(home + "/.config/x") == "~/.config/x");
	CHECK(oauth2::DisplayPath(home + "x/y") == home + "x/y");
	CHECK(oauth2::DisplayPath("/etc/x") == "/etc/x");
}


// Redirects of requests that carry secrets (task 1.4)
void TestRedirectRules()
{
	oauth2::HttpSettings settings;
	const std::vector<std::string> microsoft = { "live.com", "microsoft.com" };
	const std::string from = "https://login.microsoftonline.com/common/oauth2/v2.0/token";
	std::string error;

	// The same host: always, whatever the list
	CHECK(oauth2::IsRedirectAllowed(from, "https://login.microsoftonline.com/other", {}, settings, error));
	CHECK(oauth2::IsRedirectAllowed(from, "https://LOGIN.microsoftonline.com.:8443/x", {}, settings, error));
	// Listed domains and their subdomains
	CHECK(oauth2::IsRedirectAllowed(from, "https://login.live.com/oauth20_token.srf", microsoft, settings, error));
	CHECK(oauth2::IsRedirectAllowed(from, "https://live.com/x", microsoft, settings, error));
	CHECK(oauth2::IsRedirectAllowed(from, "https://a.b.microsoft.com/x", microsoft, settings, error));
	CHECK(oauth2::IsRedirectAllowed(from, "https://Login.Live.Com./x", microsoft, settings, error));
	CHECK(oauth2::IsRedirectAllowed(from, "https://login.live.com/x", { "LIVE.COM." }, settings, error));
	// Everything else
	CHECK(!oauth2::IsRedirectAllowed(from, "https://login.live.com/x", {}, settings, error));
	CHECK(error == "host login.live.com is neither the endpoint's host nor in the provider's redirect_domains");
	const char* const refusedListed[] = {
		"https://evillive.com/x", "https://live.com.evil.org/x", "https://microsoftonline.com/x",
		"https://login.microsoftonline.com.evil.org/x", "http://login.live.com/x",
		"https://user:pw@login.live.com/x", "ftp://login.live.com/x", "/relative", "",
		"http://127.0.0.1/x"
	};
	for (const char* to : refusedListed)
		CHECK(!oauth2::IsRedirectAllowed(from, to, microsoft, settings, error));
	CHECK(!oauth2::IsRedirectAllowed(from, "https://login.live.com/x", { "" }, settings, error));
	// A domain matches whole labels only
	CHECK(!oauth2::IsRedirectAllowed(from, "https://login.live.com/x", { "ive.com" }, settings, error));

	// Tests: http to loopback only when allowed
	settings.allow_http_loopback = true;
	CHECK(oauth2::IsRedirectAllowed("http://127.0.0.1:5/a", "http://127.0.0.1:6/b", {}, settings, error));
	CHECK(!oauth2::IsRedirectAllowed("http://127.0.0.1:5/a", "http://localhost:5/b", {}, settings, error));
	CHECK(oauth2::IsRedirectAllowed("http://127.0.0.1:5/a", "http://localhost:5/b", { "localhost" }, settings, error));
	CHECK(!oauth2::IsRedirectAllowed("https://a.test/", "http://a.test/", {}, settings, error));
}

// Token endpoint answers (task 1.4), without network
void TestTokenResponse()
{
	using oauth2::TokenError;
	oauth2::TokenResponse r;
	TokenError e;

	// Success, Google style
	CHECK(oauth2::ParseTokenResponse(200, "application/json",
		R"({"access_token":"ya29.a","expires_in":3599,"refresh_token":"1//r","scope":"openid https://mail.google.com/ email",)"
		R"("token_type":"Bearer","id_token":"h.p.s","refresh_token_expires_in":604799})", 1000, r, e));
	CHECK(e.kind == TokenError::Kind::None);
	CHECK(r.access_token == "ya29.a");
	CHECK(r.token_type == "Bearer");
	CHECK(r.expires_in == 3599);
	CHECK(r.refresh_token == "1//r");
	CHECK(r.scope_given);
	CHECK((r.scopes == std::vector<std::string>{"openid", "https://mail.google.com/", "email"}));
	CHECK(r.id_token == "h.p.s");
	CHECK(r.refresh_token_expires_in == 604799);
	CHECK(r.received_at == 1000);

	// Minimal: type in lower case, no expiry, no scope, no refresh token
	CHECK(oauth2::ParseTokenResponse(200, "", R"({"access_token":"a","token_type":"bearer"})", 5, r, e));
	CHECK(r.token_type == "Bearer");
	CHECK(r.expires_in == -1);
	CHECK(!r.scope_given && r.scopes.empty());
	CHECK(r.refresh_token.empty());
	CHECK(oauth2::ParseTokenResponse(200, "", R"({"access_token":"a"})", 5, r, e));
	CHECK(r.token_type == "Bearer");

	// expires_in variants
	CHECK(oauth2::ParseTokenResponse(200, "", R"({"access_token":"a","expires_in":"3600"})", 0, r, e) && r.expires_in == 3600);
	CHECK(oauth2::ParseTokenResponse(200, "", R"({"access_token":"a","expires_in":3600.0})", 0, r, e) && r.expires_in == 3600);
	CHECK(oauth2::ParseTokenResponse(200, "", R"({"access_token":"a","expires_in":null})", 0, r, e) && r.expires_in == -1);
	for (const char* bad : {R"("soon")", "-1", R"("")", R"("-5")", "true", "99999999999", R"("12345678901")"})
	{
		CHECK(!oauth2::ParseTokenResponse(200, "", std::string(R"({"access_token":"a","expires_in":)") + bad + "}", 0, r, e));
		CHECK(e.kind == TokenError::Kind::Protocol);
	}

	// Protocol errors, never quoting the body
	const char* const protocol[] = {
		R"({"access_token":"SECRET",)", R"(["SECRET"])", "", "SECRET", R"({"token_type":"Bearer"})",
		R"({"access_token":""})", R"({"access_token":42})", R"({"access_token":"SECRET","token_type":"mac"})",
		R"({"access_token":"SECRET","token_type":7})"
	};
	for (const char* body : protocol)
	{
		CHECK(!oauth2::ParseTokenResponse(200, "application/json", body, 0, r, e));
		CHECK(e.kind == TokenError::Kind::Protocol);
		CHECK(e.message.find("SECRET") == std::string::npos);
		CHECK(r.access_token.empty());
	}
	oauth2::ParseTokenResponse(200, "application/json", R"({"access_token":"x","token_type":"mac"})", 0, r, e);
	CHECK(e.message == "unsupported token_type \"mac\" (only Bearer is supported)");
	oauth2::ParseTokenResponse(200, "application/json", "[1]", 0, r, e);
	CHECK(e.message == "the token endpoint's answer is not a JSON object (application/json, 3 bytes)");

	// HTTP errors without an OAuth2 error
	CHECK(!oauth2::ParseTokenResponse(500, "text/html", "<html>SECRET</html>", 0, r, e));
	CHECK(e.kind == TokenError::Kind::Http && e.http_status == 500);
	CHECK(e.message == "HTTP 500 from the token endpoint (text/html, 19 bytes)");
	CHECK(!oauth2::ParseTokenResponse(302, "", "", 0, r, e));
	CHECK(e.kind == TokenError::Kind::Http);
	CHECK(e.message == "HTTP 302 from the token endpoint: a redirect that was not followed");
	CHECK(!oauth2::ParseTokenResponse(201, "", R"({"access_token":"a"})", 0, r, e));
	CHECK(e.kind == TokenError::Kind::Http);

	// OAuth2 errors (RFC 6749 section 5.2)
	CHECK(!oauth2::ParseTokenResponse(400, "application/json",
		R"({"error":"invalid_grant","error_description":"Token has been expired or revoked."})", 0, r, e));
	CHECK(e.kind == TokenError::Kind::OAuth && e.http_status == 400);
	CHECK(e.error == "invalid_grant");
	CHECK(e.RequiresSignIn() && !e.IsClientProblem());
	CHECK(e.message == "invalid_grant: Token has been expired or revoked.");

	// Google error_subtype
	CHECK(!oauth2::ParseTokenResponse(400, "", R"J({"error":"invalid_grant","error_description":"reauth related error (invalid_rapt)",)J"
		R"J("error_uri":"https://support.google.com/a/answer/9368756","error_subtype":"invalid_rapt"})J", 0, r, e));
	CHECK(e.subtype == "invalid_rapt");
	CHECK(e.uri == "https://support.google.com/a/answer/9368756");
	CHECK(e.message == "invalid_grant (invalid_rapt): reauth related error (invalid_rapt)");

	// Microsoft: error_codes, suberror, multi-line description, correlation ID
	CHECK(!oauth2::ParseTokenResponse(400, "", R"({"error":"invalid_grant","error_description":"AADSTS50173: The provided grant has expired due to it being revoked.\r\nTrace ID: t\r\nCorrelation ID: c",)"
		R"("error_codes":[50173],"suberror":"bad_token","correlation_id":"c-1"})", 0, r, e));
	CHECK(e.aadsts == 50173);
	CHECK(e.subtype == "bad_token");
	CHECK(e.correlation_id == "c-1");
	CHECK(e.description == "AADSTS50173: The provided grant has expired due to it being revoked.");
	// The code from the description when error_codes is missing
	CHECK(!oauth2::ParseTokenResponse(401, "", R"({"error":"invalid_client","error_description":"AADSTS700016: Application not found"})", 0, r, e));
	CHECK(e.aadsts == 700016);
	CHECK(e.IsClientProblem() && !e.RequiresSignIn());
	CHECK(!oauth2::ParseTokenResponse(400, "", R"({"error":"unauthorized_client"})", 0, r, e));
	CHECK(e.IsClientProblem() && e.aadsts == 0 && e.message == "unauthorized_client");

	// An error in a 200 answer, an error object, an unusable error value
	CHECK(!oauth2::ParseTokenResponse(200, "", R"({"error":"authorization_pending","access_token":"x"})", 0, r, e));
	CHECK(e.kind == TokenError::Kind::OAuth && e.error == "authorization_pending");
	CHECK(!oauth2::ParseTokenResponse(400, "", R"({"error":{"errorId":"INVALID_INPUT","localizedMessage":"not supported"}})", 0, r, e));
	CHECK(e.error == "INVALID_INPUT" && e.description == "not supported");
	CHECK(!oauth2::ParseTokenResponse(400, "", R"({"error":12})", 0, r, e));
	CHECK(e.error == "unknown_error");

	// Descriptions: control characters, outer spaces, length (UTF-8 kept whole)
	CHECK(!oauth2::ParseTokenResponse(400, "", R"({"error":"x","error_description":"  a\tb\u0007c  "})", 0, r, e));
	CHECK(e.description == "a b c");
	std::string longText;
	for (int i = 0; i < 200; ++i)
		longText += "\xc3\xa9";		// 400 bytes
	CHECK(!oauth2::ParseTokenResponse(400, "", R"({"error":"x","error_description":")" + longText + "\"}", 0, r, e));
	CHECK(e.description.size() == 303);
	CHECK(e.description.substr(0, 300) == longText.substr(0, 300));

	CHECK(std::string(oauth2::KindName(TokenError::Kind::OAuth)) == "oauth");
	CHECK(std::string(oauth2::KindName(TokenError::Kind::Timeout)) == "timeout");
}

void TestTokenRotation()
{
	oauth2::Tokens tokens;
	tokens.refresh_token = "r1";
	tokens.scopes = {"a", "b"};
	CHECK(tokens.NeedsRefresh(100));

	oauth2::TokenResponse r;
	r.access_token = "t1";
	r.expires_in = 3600;
	r.received_at = 1000;
	CHECK(!oauth2::UpdateTokens(tokens, r));		// no refresh token: kept
	CHECK(tokens.refresh_token == "r1");
	CHECK(tokens.access_token == "t1");
	CHECK(tokens.expires_at == 4600);
	CHECK((tokens.scopes == std::vector<std::string>{"a", "b"}));
	CHECK(!tokens.NeedsRefresh(4000));
	CHECK(tokens.NeedsRefresh(4540));
	CHECK(!tokens.NeedsRefresh(4539));
	CHECK(tokens.NeedsRefresh(4540, 0) == false);
	CHECK(tokens.NeedsRefresh(4600, 0));

	r.refresh_token = "r2";						// rotation
	r.scope_given = true;
	r.scopes = {"a"};
	r.id_token = "id";
	CHECK(oauth2::UpdateTokens(tokens, r));
	CHECK(tokens.refresh_token == "r2");
	CHECK((tokens.scopes == std::vector<std::string>{"a"}));
	CHECK(tokens.id_token == "id");
	CHECK(!oauth2::UpdateTokens(tokens, r));		// the same one again: no change

	r.expires_in = -1;							// unknown expiry: refresh before use
	oauth2::UpdateTokens(tokens, r);
	CHECK(tokens.expires_at == 0 && tokens.NeedsRefresh(0));
}

void TestTokenLog()
{
	CHECK(oauth2::RedactJsonBody(R"({"access_token":"abc","token_type":"Bearer","refresh_token":"defg","id_token":"h","expires_in":1})") ==
		R"({"access_token":"<redacted 3 bytes>","token_type":"Bearer","refresh_token":"<redacted 4 bytes>","id_token":"<redacted 1 byte>","expires_in":1})");
	CHECK(oauth2::RedactJsonBody(R"({"error":"invalid_grant"})") == R"({"error":"invalid_grant"})");
	CHECK(oauth2::RedactJsonBody(R"({"access_token":{"x":"SECRET"}})") == R"({"access_token":"<redacted>"})");
	CHECK(oauth2::RedactJsonBody(R"({"access_token":"SECRET",)") == "(25 bytes, not JSON)");
	CHECK(oauth2::RedactJsonBody(R"(["SECRET"])") == "(10 bytes, not JSON)");
	CHECK(oauth2::RedactJsonBody("") == "(0 bytes, not JSON)");

	CHECK(oauth2::RedactHeaderLine("Authorization: Bearer abc") == "Authorization: <redacted 10 bytes>");
	CHECK(oauth2::RedactHeaderLine("set-cookie: a=b; Path=/") == "set-cookie: <redacted 11 bytes>");
	CHECK(oauth2::RedactHeaderLine("Cookie:x") == "Cookie:<redacted 1 byte>");
	CHECK(oauth2::RedactHeaderLine("Proxy-Authorization: Basic eA==") == "Proxy-Authorization: <redacted 10 bytes>");
	CHECK(oauth2::RedactHeaderLine("Content-Type: application/json") == "Content-Type: application/json");
	CHECK(oauth2::RedactHeaderLine("POST /token HTTP/1.1") == "POST /token HTTP/1.1");
}

// Requests refused before sending: no network is used
void TestTokenRequestChecks()
{
	using oauth2::TokenError;
	std::vector<std::string> log;
	oauth2::HttpSettings settings;
	settings.log = [&log](const std::string& line) { log.push_back(line); };
	oauth2::TokenEndpoint endpoint;
	endpoint.url = "https://127.0.0.1:1/token";
	endpoint.client_id = "id";
	oauth2::TokenResponse r;
	TokenError e;
	const std::string verifier(43, 'v');

	CHECK(!oauth2::ExchangeCode(endpoint, "", verifier, "http://127.0.0.1/", settings, r, e));
	CHECK(e.kind == TokenError::Kind::Config && e.message == "no authorization code");
	CHECK(!oauth2::ExchangeCode(endpoint, "c", verifier, "", settings, r, e));
	CHECK(e.kind == TokenError::Kind::Config && e.message == "no redirect URI");
	CHECK(!oauth2::ExchangeCode(endpoint, "c", "short", "http://127.0.0.1/", settings, r, e));
	CHECK(e.kind == TokenError::Kind::Config && e.message == "invalid PKCE code verifier");
	CHECK(!oauth2::RefreshToken(endpoint, "", {}, settings, r, e));
	CHECK(e.kind == TokenError::Kind::Config && e.message == "no refresh token");

	oauth2::TokenEndpoint noClient = endpoint;
	noClient.client_id.clear();
	CHECK(!oauth2::RefreshToken(noClient, "r", {}, settings, r, e));
	CHECK(e.kind == TokenError::Kind::Config && e.message == "no client ID for this provider");

	// http:// only to loopback, and only when allowed (tests)
	const char* const refused[] = {
		"http://127.0.0.1:1/token", "http://example.org/token", "ftp://example.org/token",
		"https://user:pw@example.org/token", "https://user@example.org/token", "not a url", ""
	};
	for (const char* url : refused)
	{
		oauth2::TokenEndpoint bad = endpoint;
		bad.url = url;
		CHECK(!oauth2::RefreshToken(bad, "SECRET-refresh", {}, settings, r, e));
		CHECK(e.kind == TokenError::Kind::Config);
	}
	settings.allow_http_loopback = true;
	bool loopback = false;
	std::string error;
	for (const char* url : {"http://127.0.0.1:8/t", "http://[::1]:8/t", "http://LOCALHOST/t", "https://localhost/t"})
	{
		CHECK(oauth2::CheckRequestUrl(url, settings, loopback, error));
		CHECK(loopback);
	}
	CHECK(!oauth2::CheckRequestUrl("http://127.0.0.2/t", settings, loopback, error));
	CHECK(!oauth2::CheckRequestUrl("http://example.org/t", settings, loopback, error));
	CHECK(oauth2::CheckRequestUrl("https://oauth2.googleapis.com/token", settings, loopback, error));
	CHECK(!loopback);

	// The refusals are logged, never with the refresh token
	CHECK(!log.empty());
	for (const std::string& line : log)
		CHECK(line.find("SECRET") == std::string::npos);
}

void TestTokenEndpointOf()
{
	oauth2::Provider p;
	p.token_endpoint = "https://login.microsoftonline.com/{tenant}/oauth2/v2.0/token";
	p.tenant = "consumers";
	p.client_id = "cid";
	p.client_secret = "cs";
	p.redirect_domains = { "live.com" };
	const oauth2::TokenEndpoint e = oauth2::TokenEndpointOf(p);
	CHECK(e.redirect_domains == p.redirect_domains);
	CHECK(e.url == "https://login.microsoftonline.com/consumers/oauth2/v2.0/token");
	CHECK(e.client_id == "cid");
	CHECK(e.client_secret == "cs");
}


// The browser's redirect (task 1.5; also the pasted URL of task 6.2)
void TestInterpretRedirect()
{
	using Outcome = oauth2::RedirectResult::Outcome;
	const std::string state = "s-123";

	oauth2::RedirectResult r = oauth2::InterpretRedirect({ {"code", "c1"}, {"state", state}, {"scope", "a b"} }, state);
	CHECK(r.outcome == Outcome::Code && r.code == "c1");
	CHECK(r.params.size() == 3);
	CHECK(r.message.find("c1") == std::string::npos);

	r = oauth2::InterpretRedirect({ {"error", "access_denied"}, {"error_description", "no\tway"}, {"state", state} }, state);
	CHECK(r.outcome == Outcome::Error && r.error == "access_denied" && r.error_description == "no way");
	CHECK(r.message == "the provider did not grant access: access_denied: no way");
	// An error wins over a code
	r = oauth2::InterpretRedirect({ {"code", "c1"}, {"error", "server_error"}, {"state", state} }, state);
	CHECK(r.outcome == Outcome::Error && r.code.empty());

	// Refused: the listener keeps waiting
	r = oauth2::InterpretRedirect({ {"code", "c1"}, {"state", "s-124"} }, state);
	CHECK(r.outcome == Outcome::None && r.code.empty() && r.message.find("wrong state") == 0);
	CHECK(oauth2::InterpretRedirect({ {"code", "c1"}, {"state", "s-12"} }, state).outcome == Outcome::None);
	CHECK(oauth2::InterpretRedirect({ {"code", "c1"} }, state).message == "no state in the redirect");
	CHECK(oauth2::InterpretRedirect({ {"error", "x"} }, state).outcome == Outcome::None);
	CHECK(oauth2::InterpretRedirect({ {"code", "c1"}, {"state", ""} }, "").message == "no state was expected");
	CHECK(oauth2::InterpretRedirect({ {"state", state} }, state).message == "neither a code nor an error in the redirect");
	CHECK(oauth2::InterpretRedirect({ {"code", ""}, {"state", state} }, state).outcome == Outcome::None);
	for (const char* name : { "code", "state", "error", "error_description", "error_uri" })
	{
		oauth2::Params params = { {"code", "c1"}, {"state", state}, {name, "one"}, {name, "two"} };
		if (std::string(name) == "code" || std::string(name) == "state")
			params.pop_back();
		r = oauth2::InterpretRedirect(params, state);
		CHECK(r.outcome == Outcome::None && r.message == std::string("parameter \"") + name + "\" given twice");
	}
	// Other parameters may repeat
	CHECK(oauth2::InterpretRedirect({ {"code", "c1"}, {"state", state}, {"x", "1"}, {"x", "2"} }, state).outcome == Outcome::Code);

	CHECK(std::string(oauth2::OutcomeName(Outcome::Cancelled)) == "cancelled");
	CHECK(oauth2::IsLoopbackRedirect("http://127.0.0.1"));
	CHECK(oauth2::IsLoopbackRedirect("http://localhost/cb"));
	CHECK(oauth2::IsLoopbackRedirect("http://[::1]/a/b"));
	CHECK(!oauth2::IsLoopbackRedirect("http://127.0.0.1:80"));
	CHECK(!oauth2::IsLoopbackRedirect("http://127.0.0.1?x"));
	CHECK(!oauth2::IsLoopbackRedirect("http://localhost.example.org"));
	CHECK(!oauth2::IsLoopbackRedirect("https://localhost"));

	// Open() refuses anything else, without opening a socket
	oauth2::LoopbackListener listener;
	std::string error;
	CHECK(!listener.Open("http://example.org/", error));
	CHECK(error == "not a loopback redirect: \"http://example.org/\"");
	CHECK(listener.RedirectUri().empty() && listener.Port() == 0);
	CHECK(listener.Wait(state, oauth2::ListenerSettings()).outcome == Outcome::Failed);
}

// The authorization request and the granted scopes (task 1.6)
void TestAuthorizationUrl()
{
	oauth2::Provider p;
	p.authorization_endpoint = "https://login.example/{tenant}/authorize";
	p.tenant = "common";
	p.client_id = "cid";
	p.scopes = { {"always", {"openid", "email"}}, {"mail", {"https://mail.example/"}}, {"calendar", {"cal"}} };
	p.authorization_params = { {"prompt", "consent"}, {"access_type", "offline"} };

	oauth2::AuthorizationRequest r;
	r.scopes = p.ScopesFor({ oauth2::cScopeMail });
	r.redirect_uri = "http://127.0.0.1:5000/cb";
	r.state = "st";
	r.code_challenge = "ch";
	r.login_hint = "a+b@x.org";
	const std::string base = "https://login.example/common/authorize?response_type=code&client_id=cid"
		"&redirect_uri=http%3A%2F%2F127.0.0.1%3A5000%2Fcb&scope=openid%20email%20https%3A%2F%2Fmail.example%2F"
		"&state=st";
	CHECK(oauth2::BuildAuthorizationUrl(p, r) == base +
		"&code_challenge=ch&code_challenge_method=S256&login_hint=a%2Bb%40x.org&access_type=offline&prompt=consent");

	// Overrides: removed, replaced, added in name order; the fixed ones stay
	r.overrides = { {"prompt", ""}, {"state", "other"}, {"client_id", "x"}, {"login_hint", "y"} };
	CHECK(oauth2::BuildAuthorizationUrl(p, r) == base +
		"&code_challenge=ch&code_challenge_method=S256&login_hint=a%2Bb%40x.org&access_type=offline");
	r.overrides = { {"prompt", "select_account"}, {"extra", "1 2"} };
	CHECK(oauth2::BuildAuthorizationUrl(p, r) == base +
		"&code_challenge=ch&code_challenge_method=S256&login_hint=a%2Bb%40x.org&access_type=offline&extra=1%202"
		"&prompt=select_account");

	// No PKCE, no login hint, no extra parameter; an endpoint with a query
	r.overrides.clear();
	r.login_hint.clear();
	p.pkce = false;
	p.authorization_params.clear();
	CHECK(oauth2::BuildAuthorizationUrl(p, r) == base);
	p.pkce = true;
	r.code_challenge.clear();
	CHECK(oauth2::BuildAuthorizationUrl(p, r) == base);
	p.authorization_endpoint = "https://login.example/auth?tenant=x";
	CHECK(oauth2::BuildAuthorizationUrl(p, r).find("https://login.example/auth?tenant=x&response_type=code&") == 0);

	// Granted scopes: Google reports "email" as userinfo.email
	const std::vector<std::string> requested = { "openid", "email", "https://mail.example/", "cal" };
	const std::vector<std::string> granted = { "openid", "https://www.googleapis.com/auth/userinfo.email",
		"https://mail.example/" };
	CHECK(oauth2::MissingScopes(p, requested, true, granted) == std::vector<std::string>({ "cal" }));
	CHECK(oauth2::MissingScopes(p, requested, false, granted).empty());
	CHECK(oauth2::MissingScopes(p, requested, true, {}) ==
		std::vector<std::string>({ "https://mail.example/", "cal" }));
	CHECK(oauth2::RefreshScopes(p, requested, true, granted) ==
		std::vector<std::string>({ "openid", "email", "https://mail.example/" }));
	CHECK(oauth2::RefreshScopes(p, requested, false, {}) == requested);
}

std::string MakeJwt(const std::string& payload)
{
	return oauth2::Base64UrlEncode("{\"alg\":\"RS256\",\"typ\":\"JWT\"}") + "." +
		oauth2::Base64UrlEncode(payload) + ".c2lnbmF0dXJl";
}

// Device authorization answers and poll results (task 1.7)
void TestDeviceCode()
{
	oauth2::DeviceAuthorization a;
	oauth2::TokenError e;
	const std::string json = "application/json";

	// Microsoft's answer
	CHECK(oauth2::ParseDeviceAuthorization(200, json, R"({"user_code":"ABCD-EFGH","device_code":"S3CR3T-dc",)"
		R"("verification_uri":"https://microsoft.com/devicelogin","expires_in":900,"interval":5,)"
		R"("message":"To sign in, use a web browser\r\nsecond line"})", 1000, a, e));
	CHECK(a.device_code == "S3CR3T-dc" && a.user_code == "ABCD-EFGH" && a.received_at == 1000);
	CHECK(a.verification_uri == "https://microsoft.com/devicelogin" && a.verification_uri_complete.empty());
	CHECK(a.expires_in == 900 && a.interval == 5 && a.message == "To sign in, use a web browser");
	CHECK(e.kind == oauth2::TokenError::Kind::None);

	// Google's older name, numbers as strings, the complete URI; no
	// interval: 5
	CHECK(oauth2::ParseDeviceAuthorization(200, json, R"({"device_code":"d","user_code":"ABC-DEF-GHI",)"
		R"("verification_url":"https://www.google.com/device","expires_in":"1800",)"
		R"("verification_uri_complete":"https://www.google.com/device?code=ABC-DEF-GHI"})", 0, a, e));
	CHECK(a.verification_uri == "https://www.google.com/device" && a.expires_in == 1800 && a.interval == 5);
	CHECK(a.verification_uri_complete == "https://www.google.com/device?code=ABC-DEF-GHI");
	// An interval of 0, or not a number: 5; a string: read
	CHECK(oauth2::ParseDeviceAuthorization(200, json, R"({"device_code":"d","user_code":"U",)"
		R"("verification_uri":"http://x.test/d","expires_in":60,"interval":0})", 0, a, e) && a.interval == 5);
	CHECK(oauth2::ParseDeviceAuthorization(200, json, R"({"device_code":"d","user_code":"U",)"
		R"("verification_uri":"http://x.test/d","expires_in":60,"interval":"x"})", 0, a, e) && a.interval == 5);
	CHECK(oauth2::ParseDeviceAuthorization(200, json, R"({"device_code":"d","user_code":"U",)"
		R"("verification_uri":"http://x.test/d","expires_in":60,"interval":"3"})", 0, a, e) && a.interval == 3);
	// A complete URI that could not be shown as it is: dropped
	CHECK(oauth2::ParseDeviceAuthorization(200, json, R"({"device_code":"d","user_code":"U",)"
		R"("verification_uri":"https://x.test/d","expires_in":60,)"
		R"("verification_uri_complete":"https://x.test/d?code=A B"})", 0, a, e) && a.verification_uri_complete.empty());

	// Refused answers: the field named, never the device code, nothing kept
	const struct
	{
		const char* body;
		const char* part;
	} bad[] = {
		{ R"({"user_code":"U","verification_uri":"https://x.test/d","expires_in":60})", "no valid device_code" },
		{ R"({"device_code":"S3CR3T","verification_uri":"https://x.test/d","expires_in":60})", "no valid user_code" },
		{ R"({"device_code":"S3CR3T","user_code":"AB CD","verification_uri":"https://x.test/d","expires_in":60})",
			"no valid user_code" },
		{ "{\"device_code\":\"S3CR3T\",\"user_code\":\"AB\\u001b[2JCD\",\"verification_uri\":\"https://x.test/d\","
			"\"expires_in\":60}", "no valid user_code" },
		{ R"({"device_code":"S3CR3T","user_code":"U","expires_in":60})", "no valid verification_uri" },
		{ R"({"device_code":"S3CR3T","user_code":"U","verification_uri":"javascript:void","expires_in":60})",
			"no valid verification_uri" },
		{ R"({"device_code":"S3CR3T","user_code":"U","verification_uri":"https://","expires_in":60})",
			"no valid verification_uri" },
		{ R"({"device_code":"S3CR3T","user_code":"U","verification_uri":"https://x.test/d"})", "no valid expires_in" },
		{ R"({"device_code":"S3CR3T","user_code":"U","verification_uri":"https://x.test/d","expires_in":0})",
			"no valid expires_in" },
		{ R"(["S3CR3T"])", "not a JSON object" },
	};
	for (const auto& item : bad)
	{
		a.device_code = "kept?";
		CHECK(!oauth2::ParseDeviceAuthorization(200, json, item.body, 0, a, e));
		CHECK(e.kind == oauth2::TokenError::Kind::Protocol && e.message.find(item.part) != std::string::npos);
		CHECK(e.message.find("S3CR3T") == std::string::npos && a.device_code.empty());
	}
	CHECK(!oauth2::ParseDeviceAuthorization(502, "text/html", "<html>S3CR3T</html>", 0, a, e));
	CHECK(e.kind == oauth2::TokenError::Kind::Http && e.http_status == 502 &&
		e.message == "HTTP 502 from the device authorization endpoint (text/html, 19 bytes)");

	// Errors: Microsoft's answers for an unknown client
	CHECK(!oauth2::ParseDeviceAuthorization(400, json, R"({"error":"invalid_request","error_codes":[50059],)"
		R"("error_description":"AADSTS50059: No tenant-identifying information found.\r\nTrace ID: t"})", 0, a, e));
	CHECK(e.kind == oauth2::TokenError::Kind::OAuth && e.aadsts == 50059 && oauth2::IsUnknownClient(e) &&
		!e.IsClientProblem());
	CHECK(!oauth2::ParseDeviceAuthorization(400, json, R"({"error":"unauthorized_client",)"
		R"("error_description":"AADSTS700016: Application with identifier 'x' was not found"})", 0, a, e));
	CHECK(e.aadsts == 700016 && oauth2::IsUnknownClient(e) && e.IsClientProblem());
	CHECK(!oauth2::ParseDeviceAuthorization(400, json, R"({"error":"invalid_scope"})", 0, a, e));
	CHECK(!oauth2::IsUnknownClient(e));
	oauth2::TokenError network;
	network.kind = oauth2::TokenError::Kind::Network;
	CHECK(!oauth2::IsUnknownClient(network));

	// Poll results (RFC 8628 section 3.5; Microsoft's names)
	const struct
	{
		const char* error;
		oauth2::DevicePoll poll;
	} polls[] = {
		{ "authorization_pending", oauth2::DevicePoll::Pending },
		{ "slow_down", oauth2::DevicePoll::SlowDown },
		{ "access_denied", oauth2::DevicePoll::Denied },
		{ "authorization_declined", oauth2::DevicePoll::Denied },
		{ "expired_token", oauth2::DevicePoll::Expired },
		{ "bad_verification_code", oauth2::DevicePoll::Failed },
		{ "invalid_grant", oauth2::DevicePoll::Failed },
		{ "invalid_client", oauth2::DevicePoll::Failed },
	};
	for (const auto& item : polls)
	{
		oauth2::TokenError poll;
		poll.kind = oauth2::TokenError::Kind::OAuth;
		poll.error = item.error;
		CHECK(oauth2::ClassifyDevicePoll(poll) == item.poll);
	}
	CHECK(oauth2::ClassifyDevicePoll(network) == oauth2::DevicePoll::Failed);
	CHECK(oauth2::ClassifyDevicePoll(oauth2::TokenError()) == oauth2::DevicePoll::Granted);
	CHECK(std::string(oauth2::DevicePollName(oauth2::DevicePoll::SlowDown)) == "slow down");

	// No request without a device code
	oauth2::TokenResponse r;
	CHECK(oauth2::PollDeviceToken(oauth2::TokenEndpoint(), "", oauth2::HttpSettings(), r, e) ==
		oauth2::DevicePoll::Failed && e.kind == oauth2::TokenError::Kind::Config);
}

// ID token claims (task 1.6; account check of task 3.5)
void TestIdToken()
{
	oauth2::IdTokenClaims c;
	std::string error;
	CHECK(oauth2::ParseIdToken(MakeJwt(R"({"iss":"https://accounts.google.com","sub":"123","aud":"cid",)"
		R"("email":"U@x.org","email_verified":true,"name":"N","exp":1700000000,"iat":1699996400,"extra":[1]})"),
		c, error));
	CHECK(c.issuer == "https://accounts.google.com" && c.subject == "123" && c.name == "N");
	CHECK(c.audience == std::vector<std::string>({ "cid" }) && c.HasAudience("cid") && !c.HasAudience("ci"));
	CHECK(c.email == "U@x.org" && c.email_verified && c.email_verified_given && c.Address() == "U@x.org");
	CHECK(c.expires_at == 1700000000 && c.issued_at == 1699996400);

	// Microsoft: no email, the address in preferred_username; aud as a list
	c = oauth2::IdTokenClaims();
	CHECK(oauth2::ParseIdToken(MakeJwt(R"({"aud":["a","cid"],"preferred_username":"me@outlook.com",)"
		R"("email_verified":"true","exp":1.7e9,"email":null})"), c, error));
	CHECK(c.HasAudience("cid") && c.email.empty() && c.Address() == "me@outlook.com");
	CHECK(c.email_verified && c.email_verified_given && c.expires_at == 1700000000);
	c = oauth2::IdTokenClaims();
	CHECK(oauth2::ParseIdToken(MakeJwt(R"({"preferred_username":"me","email_verified":false})"), c, error));
	CHECK(c.Address().empty() && !c.email_verified && c.email_verified_given && c.audience.empty());

	// No email_verified claim (Microsoft), or null: not stated
	c = oauth2::IdTokenClaims();
	CHECK(oauth2::ParseIdToken(MakeJwt(R"({"email":"me@outlook.com"})"), c, error));
	CHECK(c.email == "me@outlook.com" && !c.email_verified && !c.email_verified_given);
	CHECK(oauth2::ParseIdToken(MakeJwt(R"({"email":"me@outlook.com","email_verified":null})"), c, error));
	CHECK(!c.email_verified && !c.email_verified_given);

	// Refused, with nothing changed and no part of the token in the message
	const oauth2::IdTokenClaims before = c;
	const struct { std::string token; std::string message; } cases[] = {
		{ "secretpart.secretpart", "the ID token is not a JWT (three parts separated by dots)" },
		{ MakeJwt("{}") + ".more", "the ID token is not a JWT (three parts separated by dots)" },
		{ "", "the ID token is not a JWT (three parts separated by dots)" },
		{ "a.secret+part.c", "the ID token's payload is not base64url" },
		{ MakeJwt("secret, not JSON"), "the ID token's payload is not a JSON object" },
		{ MakeJwt("[\"secret\"]"), "the ID token's payload is not a JSON object" },
		{ MakeJwt(R"({"email":42})"), "the ID token's email claim has the wrong type" },
		{ MakeJwt(R"({"aud":7})"), "the ID token's aud claim has the wrong type" },
		{ MakeJwt(R"({"aud":["a",7]})"), "the ID token's aud claim has the wrong type" },
		{ MakeJwt(R"({"exp":"1700000000"})"), "the ID token's exp claim has the wrong type" },
		{ MakeJwt(R"({"iat":-5})"), "the ID token's iat claim has the wrong type" },
		{ MakeJwt(R"({"email_verified":1})"), "the ID token's email_verified claim has the wrong type" },
		{ MakeJwt(R"({"sub":{"x":"secret"}})"), "the ID token's sub claim has the wrong type" },
	};
	for (const auto& item : cases)
	{
		error.clear();
		CHECK(!oauth2::ParseIdToken(item.token, c, error));
		CHECK(error == item.message);
		CHECK(error.find("secret") == std::string::npos);
		CHECK(c.Address() == before.Address() && c.audience == before.audience);
	}
}

} // namespace

int main()
{
	TestVersion();
	TestIdentityId();
	TestBase64Url();
	TestPercentEncoding();
	TestParams();
	TestPkce();
	TestRandom();
	TestRedact();
	TestJson();
	TestShippedFile();
	TestClientChecks();
	TestMerge();
	TestConfigErrors();
	TestPermissions();
	TestPaths();
	TestRedirectRules();
	TestInterpretRedirect();
	TestTokenResponse();
	TestTokenRotation();
	TestTokenLog();
	TestTokenRequestChecks();
	TestTokenEndpointOf();
	TestAuthorizationUrl();
	TestIdToken();
	TestDeviceCode();

	if (sFailures != 0)
	{
		std::fprintf(stderr, "oauth2-unit: %d of %d checks failed\n", sFailures, sChecks);
		return 1;
	}
	std::printf("oauth2-unit: all %d checks passed\n", sChecks);
	return 0;
}
