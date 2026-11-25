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

#include "oauth2/Encoding.h"
#include "oauth2/Identity.h"
#include "oauth2/Pkce.h"
#include "oauth2/Random.h"
#include "oauth2/Redact.h"
#include "oauth2/Version.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <set>
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

	if (sFailures != 0)
	{
		std::fprintf(stderr, "oauth2-unit: %d of %d checks failed\n", sFailures, sChecks);
		return 1;
	}
	std::printf("oauth2-unit: all %d checks passed\n", sChecks);
	return 0;
}
