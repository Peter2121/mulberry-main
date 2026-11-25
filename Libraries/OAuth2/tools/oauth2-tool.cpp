/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

// oauth2-tool: command-line test tool of the OAuth2 core
// (OAUTH2-PLAN.md, stage 1).  It checks the provider configuration, the
// registrations, the sign-in and the tokens without Mulberry.  Commands are
// added task by task.
//
// The application identity and the system configuration folder come from
// the build (configure), through OAUTH2_TOOL_APP_ID, OAUTH2_TOOL_APP_NAME,
// OAUTH2_TOOL_APP_VERSION and OAUTH2_TOOL_SYSCONFDIR.

#include "oauth2/Identity.h"
#include "oauth2/Paths.h"
#include "oauth2/Permissions.h"
#include "oauth2/ProviderConfig.h"
#include "oauth2/Redact.h"
#include "oauth2/Version.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifndef OAUTH2_TOOL_APP_ID
#error "OAUTH2_TOOL_APP_ID must be defined by the build"
#endif
#ifndef OAUTH2_TOOL_APP_NAME
#error "OAUTH2_TOOL_APP_NAME must be defined by the build"
#endif
#ifndef OAUTH2_TOOL_APP_VERSION
#error "OAUTH2_TOOL_APP_VERSION must be defined by the build"
#endif
#ifndef OAUTH2_TOOL_SYSCONFDIR
#error "OAUTH2_TOOL_SYSCONFDIR must be defined by the build"
#endif

namespace
{

const char* const cToolName = "oauth2-tool";

// Exit statuses: 0 success, 1 the command found nothing, 2 usage or
// configuration error (section 7 of the plan: "exits with status 2")
const int cExitNotFound = 1;
const int cExitError = 2;

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
		"Usage: %s [options] command [argument]\n"
		"\n"
		"Test tool of the OAuth2 core (OAUTH2-PLAN.md, stage 1).\n"
		"\n"
		"Commands:\n"
		"  providers          check the provider configuration and list the providers\n"
		"  lookup NAME        the provider of a server name or an e-mail address\n"
		"  --version          versions of the tool, the library and libcurl\n"
		"  --help             this help\n"
		"\n"
		"Options:\n"
		"  --system-config FILE  system provider file instead of the installed one\n"
		"                        (also: $%s)\n"
		"  --user-config FILE    user provider file instead of the usual one\n"
		"  --no-user-config      ignore the user provider file\n"
		"  --fix-permissions     repair the permissions of the user provider file\n"
		"                        (mode 0600, folder 0700) before reading it\n"
		"\n"
		"Exit status: 0 success, 1 nothing found, 2 usage or configuration error.\n",
		cToolName, oauth2::SystemConfigVariable(ToolIdentity()).c_str());
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

struct Options
{
	oauth2::ConfigFiles files;
	bool fix_permissions = false;
	std::string command;
	std::vector<std::string> arguments;
};

// Prints the permission warnings of one file; returns their number
size_t ReportPermissions(const std::string& path, oauth2::FileRole role)
{
	const std::vector<oauth2::PermissionIssue> issues = oauth2::CheckPermissions(path, role);
	for (const oauth2::PermissionIssue& issue : issues)
	{
		std::fprintf(stderr, "%s: warning: %s: %s\n", cToolName,
			oauth2::DisplayPath(issue.path).c_str(), issue.problem.c_str());
		std::fprintf(stderr, "    %s%s\n", issue.fixable ? "fix (or use --fix-permissions): " : "fix: ",
			issue.advice.c_str());
	}
	return issues.size();
}

// Permission checks before reading (section 7): warnings only
void CheckFiles(const Options& options)
{
	if (!options.files.user_file.empty())
	{
		if (options.fix_permissions)
		{
			std::string error;
			if (!oauth2::FixPermissions(options.files.user_file, error))
				std::fprintf(stderr, "%s: warning: permissions not fixed: %s\n", cToolName,
					oauth2::DisplayPath(error).c_str());
		}
		ReportPermissions(options.files.user_file, oauth2::FileRole::User);
	}
	// A system file named explicitly (build tree) is not expected to belong
	// to root
	if (!options.files.system_file.empty() && !options.files.system_file_explicit)
		ReportPermissions(options.files.system_file, oauth2::FileRole::System);
}

bool LoadConfig(const Options& options, oauth2::ProviderConfig& config)
{
	CheckFiles(options);
	oauth2::ConfigError error;
	if (!config.Load(options.files, error))
	{
		std::fprintf(stderr, "%s: %s\n", cToolName, error.ToString().c_str());
		return false;
	}
	return true;
}

std::string JoinWords(const std::vector<std::string>& words)
{
	std::string result;
	for (const std::string& word : words)
		result += (result.empty() ? "" : " ") + word;
	return result;
}

void PrintProvider(const oauth2::Provider& p)
{
	std::printf("%s (%s)\n", p.id.c_str(), p.name.c_str());
	std::printf("  hosts:          %s\n", JoinWords(p.hosts).c_str());
	std::printf("  domains:        %s\n", JoinWords(p.domains).c_str());
	std::printf("  authorization:  %s\n", p.ResolveEndpoint(p.authorization_endpoint).c_str());
	std::printf("  token:          %s\n", p.ResolveEndpoint(p.token_endpoint).c_str());
	std::printf("  device code:    %s\n", p.SupportsDeviceCode() ?
		p.ResolveEndpoint(p.device_authorization_endpoint).c_str() : "(not offered)");
	std::printf("  revocation:     %s\n", p.revocation_endpoint.empty() ?
		"(none)" : p.ResolveEndpoint(p.revocation_endpoint).c_str());
	for (const auto& group : p.scopes)
		std::printf("  scopes %-8s %s\n", (group.first + ":").c_str(), JoinWords(group.second).c_str());
	for (const auto& param : p.authorization_params)
		std::printf("  parameter:      %s=%s\n", param.first.c_str(), param.second.c_str());
	std::printf("  redirect:       %s (PKCE %s)\n", p.redirect.c_str(), p.pkce ? "on" : "off");
	std::printf("  SASL:           %s\n", JoinWords(p.sasl).c_str());
	if (!p.tenant.empty())
		std::printf("  tenant:         %s\n", p.tenant.c_str());
	std::printf("  client ID:      %s\n", p.client_id.empty() ? "(none)" : p.client_id.c_str());
	std::printf("  client secret:  %s\n", p.client_secret.empty() ?
		"(none)" : oauth2::Redact(p.client_secret).c_str());
}

void PrintFiles(const Options& options, const oauth2::ProviderConfig& config)
{
	std::printf("system file: %s%s\n",
		options.files.system_file.empty() ? "(none)" : oauth2::DisplayPath(options.files.system_file).c_str(),
		config.SystemFileFound() ? "" : " (not found)");
	std::printf("user file:   %s\n",
		options.files.user_file.empty() ? "(none)" : oauth2::DisplayPath(options.files.user_file).c_str());
}

int CommandProviders(const Options& options)
{
	oauth2::ProviderConfig config;
	if (!LoadConfig(options, config))
		return cExitError;
	PrintFiles(options, config);
	if (!config.SystemFileFound())
	{
		std::fprintf(stderr, "%s: OAuth2 provider configuration not found (expected at %s)\n",
			cToolName, oauth2::DisplayPath(options.files.system_file).c_str());
		return cExitNotFound;
	}
	for (const oauth2::Provider& provider : config.Providers())
	{
		std::printf("\n");
		PrintProvider(provider);
	}
	return 0;
}

int CommandLookup(const Options& options)
{
	if (options.arguments.size() != 1)
	{
		std::fprintf(stderr, "%s: lookup needs one server name or e-mail address\n", cToolName);
		return cExitError;
	}
	oauth2::ProviderConfig config;
	if (!LoadConfig(options, config))
		return cExitError;
	if (!config.SystemFileFound())
	{
		std::fprintf(stderr, "%s: OAuth2 provider configuration not found (expected at %s)\n",
			cToolName, oauth2::DisplayPath(options.files.system_file).c_str());
		return cExitNotFound;
	}
	const std::string& name = options.arguments[0];
	const oauth2::Provider* provider = name.find('@') != std::string::npos ?
		config.FindByEmail(name) : config.FindByHost(name);
	if (!provider)
	{
		std::printf("%s: no provider\n", name.c_str());
		return cExitNotFound;
	}
	std::printf("%s: %s (%s)\n", name.c_str(), provider->id.c_str(), provider->name.c_str());
	return 0;
}

// Parses the command line; false after printing an error
bool ParseOptions(int argc, char* argv[], Options& options)
{
	options.files = oauth2::DefaultConfigFiles(ToolIdentity(), OAUTH2_TOOL_SYSCONFDIR);
	for (int i = 1; i < argc; ++i)
	{
		const std::string arg = argv[i];
		if (!options.command.empty())
			options.arguments.push_back(arg);
		else if (arg == "--system-config" || arg == "--user-config")
		{
			if (i + 1 >= argc)
			{
				std::fprintf(stderr, "%s: %s needs a file name\n", cToolName, arg.c_str());
				return false;
			}
			if (arg == "--system-config")
			{
				options.files.system_file = argv[++i];
				options.files.system_file_explicit = true;
			}
			else
				options.files.user_file = argv[++i];
		}
		else if (arg == "--no-user-config")
			options.files.user_file.clear();
		else if (arg == "--fix-permissions")
			options.fix_permissions = true;
		else if (arg == "--version" || arg == "--help" || arg == "-h" || arg.empty() || arg[0] != '-')
			options.command = arg;
		else
		{
			std::fprintf(stderr, "%s: unknown option \"%s\"\n", cToolName, arg.c_str());
			return false;
		}
	}
	return true;
}

} // namespace

int main(int argc, char* argv[])
{
	if (!oauth2::IsValidIdentityId(OAUTH2_TOOL_APP_ID))
	{
		std::fprintf(stderr, "%s: invalid application identifier \"%s\" (build setting)\n",
			cToolName, OAUTH2_TOOL_APP_ID);
		return cExitError;
	}

	Options options;
	if (!ParseOptions(argc, argv, options))
	{
		PrintUsage(stderr);
		return cExitError;
	}

	if (options.command == "--version")
		return PrintVersion();
	if (options.command == "--help" || options.command == "-h")
	{
		PrintUsage(stdout);
		return 0;
	}
	if (options.command == "providers")
		return CommandProviders(options);
	if (options.command == "lookup")
		return CommandLookup(options);

	if (!options.command.empty())
		std::fprintf(stderr, "%s: unknown command \"%s\"\n", cToolName, options.command.c_str());
	PrintUsage(stderr);
	return cExitError;
}
