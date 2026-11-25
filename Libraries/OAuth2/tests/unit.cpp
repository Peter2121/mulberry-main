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

#include "oauth2/Identity.h"
#include "oauth2/Version.h"

#include <cstdio>
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

} // namespace

int main()
{
	TestVersion();
	TestIdentityId();

	if (sFailures != 0)
	{
		std::fprintf(stderr, "oauth2-unit: %d of %d checks failed\n", sFailures, sChecks);
		return 1;
	}
	std::printf("oauth2-unit: all %d checks passed\n", sChecks);
	return 0;
}
