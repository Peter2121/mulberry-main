/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

// oauth2-tool: command-line test tool of the OAuth2 core
// (OAUTH2-PLAN.md, stage 1).  It checks the provider registrations, the
// sign-in and the tokens without Mulberry.  Commands are added task by
// task; for now only --version and --help exist.
//
// The application identity comes from the build (configure --with-app-id),
// through OAUTH2_TOOL_APP_ID, OAUTH2_TOOL_APP_NAME and
// OAUTH2_TOOL_APP_VERSION.

#include "oauth2/Identity.h"
#include "oauth2/Version.h"

#include <cstdio>
#include <cstring>

#ifndef OAUTH2_TOOL_APP_ID
#error "OAUTH2_TOOL_APP_ID must be defined by the build"
#endif
#ifndef OAUTH2_TOOL_APP_NAME
#error "OAUTH2_TOOL_APP_NAME must be defined by the build"
#endif
#ifndef OAUTH2_TOOL_APP_VERSION
#error "OAUTH2_TOOL_APP_VERSION must be defined by the build"
#endif

namespace
{

const char* const cToolName = "oauth2-tool";

oauth2::Identity ToolIdentity()
{
	oauth2::Identity identity;
	identity.id = OAUTH2_TOOL_APP_ID;
	identity.name = OAUTH2_TOOL_APP_NAME;
	identity.version = OAUTH2_TOOL_APP_VERSION;
	return identity;
}

void PrintUsage(std::FILE* out)
{
	std::fprintf(out,
		"Usage: %s [--version | --help]\n"
		"\n"
		"Test tool of the OAuth2 core.  Commands (login, refresh, device,\n"
		"imap-check, smtp-check) are added by the tasks of stage 1 of\n"
		"OAUTH2-PLAN.md.\n"
		"\n"
		"  --version  show the versions of the tool, the library and libcurl\n"
		"  --help     show this help\n",
		cToolName);
}

int PrintVersion()
{
	const oauth2::Identity identity = ToolIdentity();
	std::printf("%s (%s %s, identity \"%s\")\n", cToolName,
		identity.name.c_str(), identity.version.c_str(), identity.id.c_str());
	std::printf("libOAuth2 %s\n", oauth2::LibraryVersion());
	const std::string tls = oauth2::CurlTlsBackend();
	std::printf("libcurl %s%s%s\n", oauth2::CurlVersion().c_str(),
		tls.empty() ? "" : ", ", tls.c_str());
	return 0;
}

} // namespace

int main(int argc, char* argv[])
{
	if (!oauth2::IsValidIdentityId(OAUTH2_TOOL_APP_ID))
	{
		std::fprintf(stderr, "%s: invalid application identifier \"%s\" (build setting)\n",
			cToolName, OAUTH2_TOOL_APP_ID);
		return 2;
	}

	if (argc == 2 && std::strcmp(argv[1], "--version") == 0)
		return PrintVersion();
	if (argc == 2 && (std::strcmp(argv[1], "--help") == 0 || std::strcmp(argv[1], "-h") == 0))
	{
		PrintUsage(stdout);
		return 0;
	}

	if (argc >= 2)
		std::fprintf(stderr, "%s: unknown command or option \"%s\"\n", cToolName, argv[1]);
	PrintUsage(stderr);
	return 2;
}
