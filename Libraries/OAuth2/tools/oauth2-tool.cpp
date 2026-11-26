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
// login and refresh (task 1.6) keep the refresh token in a file of their
// own, mode 0600, for this tool only: ~/.config/<app>/oauth2-tool/
// <provider>-<account>.json.  Mulberry keeps its tokens in the keyring.
//
// The application identity and the system configuration folder come from
// the build (configure), through OAUTH2_TOOL_APP_ID, OAUTH2_TOOL_APP_NAME,
// OAUTH2_TOOL_APP_VERSION and OAUTH2_TOOL_SYSCONFDIR.

#include "oauth2/Authorization.h"
#include "oauth2/Browser.h"
#include "oauth2/DeviceCode.h"
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

#include <nlohmann/json.hpp>

#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
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

// Exit statuses: 0 success, 1 the command found nothing or the sign-in
// failed, 2 usage or configuration error (section 7 of the plan: "exits
// with status 2")
const int cExitNotFound = 1;
const int cExitFailed = 1;
const int cExitError = 2;

// Set by Ctrl-C (SIGINT) or SIGTERM during login and refresh: the listener
// and the token request stop and report the cancellation
std::atomic<bool> sCancel(false);

extern "C" void OnCancelSignal(int)
{
	sCancel = true;
}

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
		"  login ACCOUNT      sign in in the browser as ACCOUNT (an e-mail address),\n"
		"                     show what was granted, keep the refresh token\n"
		"  device ACCOUNT     sign in on another device (a phone) with a code, for\n"
		"                     providers that offer it (Microsoft); keeps the token as login\n"
		"  refresh ACCOUNT    new access token from the kept refresh token\n"
		"  --version          versions of the tool, the library and libcurl\n"
		"  --help             this help\n"
		"\n"
		"Options:\n"
		"  --system-config FILE  system provider file instead of the installed one\n"
		"                        (also: $%s)\n"
		"  --user-config FILE    user provider file instead of the usual one\n"
		"  --no-user-config      ignore the user provider file\n"
		"  --fix-permissions     repair the permissions of the user provider file and\n"
		"                        of the token file (mode 0600, folder 0700) first\n"
		"\n"
		"Options of login, device and refresh:\n"
		"  --provider ID         the provider (default: from the account's domain)\n"
		"  --client-id ID        client ID instead of the configured one\n"
		"  --tenant T            Microsoft tenant instead of the configured one\n"
		"  --services LIST       scope groups, separated by commas (default: all the\n"
		"                        provider has: mail,calendar,contacts for Google)\n"
		"  --prompt VALUE        the prompt parameter (\"\" for none; Google's default\n"
		"                        is consent, needed for a refresh token)\n"
		"  --no-browser          only print the URL to open\n"
		"  --no-client-check     skip the checks of the client ID (its form, and the\n"
		"                        provider's answer before the browser opens)\n"
		"  --timeout S           wait for the sign-in at most S seconds (login: default\n"
		"                        300; device: default until the code expires)\n"
		"  --token-file FILE     instead of %s/oauth2-tool/PROVIDER-ACCOUNT.json\n"
		"  --ca-file FILE        certificate authorities for the token endpoint (tests)\n"
		"  --verbose             log of the listener and the token requests on stderr\n"
		"                        (secrets redacted)\n"
		"  --show-token          print the tokens instead of their redaction\n"
		"\n"
		"Exit status: 0 success, 1 nothing found or the sign-in failed, 2 usage or\n"
		"configuration error.\n",
		cToolName, oauth2::SystemConfigVariable(ToolIdentity()).c_str(),
		oauth2::DisplayPath(oauth2::UserConfigDirectory(ToolIdentity())).c_str());
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

	// login and refresh
	std::string provider;
	oauth2::AccountSettings account;	// --client-id, --tenant
	std::vector<std::string> services;
	bool prompt_set = false;
	std::string prompt;
	bool no_browser = false;
	bool client_check = true;
	long timeout = 300;
	bool timeout_set = false;
	std::string token_file;
	std::string ca_file;
	bool verbose = false;
	bool show_token = false;
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
	std::printf("  redirects to:   %s\n", p.redirect_domains.empty() ?
		"(same host only)" : JoinWords(p.redirect_domains).c_str());
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

// ---------------------------------------------------------------------------
// login and refresh (task 1.6)
// ---------------------------------------------------------------------------

typedef nlohmann::ordered_json Json;

// What the token file keeps between login and refresh
struct TokenFile
{
	std::string provider;
	std::string account;
	std::string client_id;
	std::string tenant;
	std::vector<std::string> requested_scopes;
	bool scope_given = false;
	std::vector<std::string> granted_scopes;
	std::string email;			// from the ID token
	std::string saved_at;
	std::string refresh_token;
};

const int cTokenFileVersion = 1;

std::string LowerAscii(std::string text)
{
	for (char& c : text)
		if (c >= 'A' && c <= 'Z')
			c = static_cast<char>(c - 'A' + 'a');
	return text;
}

// The account as part of a file name: lower case; anything but letters,
// digits and ._@+- as %XX
std::string FileNamePart(const std::string& text)
{
	static const char cHex[] = "0123456789ABCDEF";
	std::string result;
	for (unsigned char c : LowerAscii(text))
	{
		if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '@' ||
			c == '+' || c == '-')
			result += static_cast<char>(c);
		else
		{
			result += '%';
			result += cHex[c >> 4];
			result += cHex[c & 0x0f];
		}
	}
	// No "." or ".." as a whole name
	if (!result.empty() && result[0] == '.')
		result = "%2E" + result.substr(1);
	return result;
}

std::string TokenFilePath(const Options& options, const oauth2::Provider& provider, const std::string& account)
{
	if (!options.token_file.empty())
		return options.token_file;
	const std::string folder = oauth2::UserConfigDirectory(ToolIdentity());
	if (folder.empty())
		return std::string();
	return folder + "/oauth2-tool/" + provider.id + "-" + FileNamePart(account) + ".json";
}

// Creates the missing folders of path's parent, mode 0700
bool MakeParentFolders(const std::string& path, std::string& error)
{
	const size_t slash = path.rfind('/');
	if (slash == std::string::npos || slash == 0)
		return true;
	const std::string folder = path.substr(0, slash);
	for (size_t i = 1; i <= folder.size(); ++i)
	{
		if (i < folder.size() && folder[i] != '/')
			continue;
		const std::string part = folder.substr(0, i);
		if (mkdir(part.c_str(), 0700) != 0 && errno != EEXIST)
		{
			error = oauth2::DisplayPath(part) + ": " + std::strerror(errno);
			return false;
		}
	}
	return true;
}

std::string UtcNow()
{
	const std::time_t now = std::time(nullptr);
	struct tm utc;
	char text[32] = "";
	if (gmtime_r(&now, &utc))
		std::strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%SZ", &utc);
	return text;
}

std::string LocalTime(std::time_t when)
{
	struct tm local;
	char text[64] = "";
	if (localtime_r(&when, &local))
		std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S %Z", &local);
	return text;
}

// Writes the file through a temporary file in the same folder (mode 0600)
// and a rename, so a crash leaves the old file or the new one
bool SaveTokenFile(const std::string& path, const TokenFile& file, std::string& error)
{
	if (!MakeParentFolders(path, error))
		return false;
	Json json;
	json["version"] = cTokenFileVersion;
	json["provider"] = file.provider;
	json["account"] = file.account;
	json["client_id"] = file.client_id;
	if (!file.tenant.empty())
		json["tenant"] = file.tenant;
	json["requested_scopes"] = file.requested_scopes;
	json["scope_given"] = file.scope_given;
	json["granted_scopes"] = file.granted_scopes;
	json["email"] = file.email;
	json["saved_at"] = file.saved_at;
	json["refresh_token"] = file.refresh_token;
	const std::string text = json.dump(2) + "\n";

	std::string temp = path + ".tmp-XXXXXX";
	std::vector<char> name(temp.begin(), temp.end());
	name.push_back('\0');
	const int fd = mkstemp(name.data());
	if (fd < 0)
	{
		error = oauth2::DisplayPath(path) + ": " + std::strerror(errno);
		return false;
	}
	temp = name.data();
	bool ok = fchmod(fd, 0600) == 0;
	size_t written = 0;
	while (ok && written < text.size())
	{
		const ssize_t n = write(fd, text.data() + written, text.size() - written);
		if (n < 0 && errno == EINTR)
			continue;
		ok = n > 0;
		if (ok)
			written += static_cast<size_t>(n);
	}
	ok = ok && fsync(fd) == 0;
	const int saved_errno = errno;
	ok = (close(fd) == 0) && ok;
	if (!ok || rename(temp.c_str(), path.c_str()) != 0)
	{
		error = oauth2::DisplayPath(path) + ": " + std::strerror(ok ? errno : saved_errno);
		unlink(temp.c_str());
		return false;
	}
	return true;
}

bool StringList(const Json& value, std::vector<std::string>& out)
{
	if (!value.is_array())
		return false;
	std::vector<std::string> list;
	for (const Json& item : value)
	{
		if (!item.is_string())
			return false;
		list.push_back(item.get<std::string>());
	}
	out = list;
	return true;
}

// 1: no such file; 2: unreadable or malformed; 0: read
int LoadTokenFile(const std::string& path, TokenFile& file, std::string& error)
{
	std::ifstream in(path, std::ios::binary);
	if (!in)
	{
		error = oauth2::DisplayPath(path) + ": " + std::strerror(errno);
		return errno == ENOENT ? cExitNotFound : cExitError;
	}
	std::stringstream buffer;
	buffer << in.rdbuf();
	Json json = Json::parse(buffer.str(), nullptr, false);
	// Messages never quote the file: it holds a token
	const std::string where = oauth2::DisplayPath(path) + ": ";
	if (json.is_discarded() || !json.is_object())
	{
		error = where + "not a JSON object";
		return cExitError;
	}
	const auto text = [&json](const char* name, std::string& out, bool required) {
		const auto it = json.find(name);
		if (it == json.end())
			return !required;
		if (!it->is_string())
			return false;
		out = it->get<std::string>();
		return !required || !out.empty();
	};
	TokenFile read;
	const auto version = json.find("version");
	if (version == json.end() || !version->is_number_integer() || version->get<int>() != cTokenFileVersion)
		error = where + "unknown version (expected " + std::to_string(cTokenFileVersion) + ")";
	else if (!text("provider", read.provider, true) || !text("account", read.account, false) ||
		!text("client_id", read.client_id, true) || !text("tenant", read.tenant, false) ||
		!text("email", read.email, false) || !text("saved_at", read.saved_at, false) ||
		!text("refresh_token", read.refresh_token, true))
		error = where + "a field is missing or not a string";
	else if (!json.contains("requested_scopes") || !StringList(json["requested_scopes"], read.requested_scopes) ||
		(json.contains("granted_scopes") && !StringList(json["granted_scopes"], read.granted_scopes)))
		error = where + "a scope list is missing or not a list of strings";
	else if (json.contains("scope_given") && !json["scope_given"].is_boolean())
		error = where + "scope_given is not true or false";
	if (!error.empty())
		return cExitError;
	read.scope_given = json.value("scope_given", false);
	file = read;
	return 0;
}

// The provider of a login or refresh, with --client-id and --tenant
// applied; false after printing an error
bool FindProvider(const Options& options, const oauth2::ProviderConfig& config, const std::string& account,
	oauth2::Provider& provider)
{
	if (!config.SystemFileFound())
	{
		std::fprintf(stderr, "%s: OAuth2 provider configuration not found (expected at %s)\n",
			cToolName, oauth2::DisplayPath(options.files.system_file).c_str());
		return false;
	}
	const oauth2::Provider* found = nullptr;
	if (!options.provider.empty())
	{
		found = config.Find(options.provider);
		if (!found)
		{
			std::fprintf(stderr, "%s: no provider \"%s\" (see: %s providers)\n", cToolName,
				options.provider.c_str(), cToolName);
			return false;
		}
	}
	else
	{
		found = config.FindByEmail(account);
		if (!found)
		{
			std::fprintf(stderr, "%s: no provider for \"%s\": name it with --provider\n", cToolName,
				account.c_str());
			return false;
		}
	}
	provider = oauth2::WithAccountSettings(*found, options.account);
	if (provider.client_id.empty())
	{
		std::fprintf(stderr, "%s: no client ID for %s: add \"client_id\" to its entry in %s, or use --client-id\n",
			cToolName, provider.id.c_str(), options.files.user_file.empty() ? "the user provider file" :
			oauth2::DisplayPath(options.files.user_file).c_str());
		return false;
	}
	return true;
}

// The scope groups asked for: --services, or every group of the provider,
// mail first
bool ScopeGroups(const Options& options, const oauth2::Provider& provider, std::vector<std::string>& groups)
{
	if (!options.services.empty())
	{
		for (const std::string& group : options.services)
			if (group == oauth2::cScopeAlways || provider.scopes.find(group) == provider.scopes.end())
			{
				std::fprintf(stderr, "%s: %s has no scope group \"%s\"\n", cToolName, provider.id.c_str(),
					group.c_str());
				return false;
			}
		groups = options.services;
		return true;
	}
	groups.clear();
	for (const char* group : { oauth2::cScopeMail, oauth2::cScopeCalendar, oauth2::cScopeContacts })
		if (provider.scopes.count(group))
			groups.push_back(group);
	for (const auto& group : provider.scopes)
		if (group.first != oauth2::cScopeAlways &&
			std::find(groups.begin(), groups.end(), group.first) == groups.end())
			groups.push_back(group.first);
	return true;
}

oauth2::HttpSettings ToolHttpSettings(const Options& options)
{
	oauth2::HttpSettings settings;
	settings.ca_file = options.ca_file;
	settings.cancel = &sCancel;
	if (options.verbose)
		settings.log = [](const std::string& line) { std::fprintf(stderr, "%s\n", line.c_str()); };
	return settings;
}

void InstallCancelHandler()
{
	struct sigaction action;
	std::memset(&action, 0, sizeof(action));
	action.sa_handler = OnCancelSignal;
	sigemptyset(&action.sa_mask);
	sigaction(SIGINT, &action, nullptr);
	sigaction(SIGTERM, &action, nullptr);
}

std::string ShowSecret(const Options& options, const std::string& secret)
{
	return options.show_token ? secret : oauth2::Redact(secret);
}

void PrintRegistrationHint()
{
	std::fprintf(stderr, "    the registration was refused: check the client ID, the client secret and the\n"
		"    tenant in the provider files (docs/oauth2/)\n");
}

void PrintTokenError(const char* what, const oauth2::TokenError& error)
{
	std::fprintf(stderr, "%s: %s failed: %s\n", cToolName, what, error.message.c_str());
	if (!error.uri.empty())
		std::fprintf(stderr, "    see %s\n", error.uri.c_str());
	if (!error.correlation_id.empty())
		std::fprintf(stderr, "    Microsoft correlation ID: %s\n", error.correlation_id.c_str());
	if (error.IsClientProblem())
		PrintRegistrationHint();
}

// The answer, as "name: value" lines; returns the address of the ID token
std::string PrintTokens(const Options& options, const oauth2::Provider& provider,
	const std::vector<std::string>& requested, const oauth2::TokenResponse& response,
	const std::string& account)
{
	std::printf("token type: %s\n", response.token_type.c_str());
	if (response.expires_in >= 0)
		std::printf("expires in: %ld s (%s)\n", response.expires_in,
			LocalTime(response.received_at + response.expires_in).c_str());
	else
		std::printf("expires in: not given\n");
	std::printf("access token: %s\n", ShowSecret(options, response.access_token).c_str());
	std::printf("requested scopes: %s\n", JoinWords(requested).c_str());
	if (response.scope_given)
		std::printf("granted scopes: %s\n", JoinWords(response.scopes).c_str());
	else
		std::printf("granted scopes: not listed by the provider (all requested)\n");
	const std::vector<std::string> missing =
		oauth2::MissingScopes(provider, requested, response.scope_given, response.scopes);
	if (!missing.empty())
		std::printf("not granted: %s\n", JoinWords(missing).c_str());
	if (response.refresh_token_expires_in >= 0)
		std::printf("refresh token expires in: %ld s\n", response.refresh_token_expires_in);

	std::string address;
	if (response.id_token.empty())
	{
		std::printf("ID token: not given\n");
		return address;
	}
	oauth2::IdTokenClaims claims;
	std::string error;
	if (!oauth2::ParseIdToken(response.id_token, claims, error))
	{
		std::printf("ID token: unreadable (%s)\n", error.c_str());
		return address;
	}
	address = claims.Address();
	std::printf("signed in as: %s%s\n", address.empty() ? "(no e-mail address in the ID token)" : address.c_str(),
		// Nothing when the provider does not say (Microsoft)
		claims.email.empty() || !claims.email_verified_given ? ""
			: claims.email_verified ? " (verified)" : " (not verified)");
	if (!claims.name.empty())
		std::printf("name: %s\n", claims.name.c_str());
	if (options.show_token)
		std::printf("ID token: %s\n", response.id_token.c_str());
	if (!claims.HasAudience(provider.client_id))
		std::fprintf(stderr, "%s: warning: the ID token was issued to another client\n", cToolName);
	if (!address.empty() && LowerAscii(address) != LowerAscii(account))
		std::fprintf(stderr, "%s: note: signed in as %s, not as %s\n", cToolName, address.c_str(),
			account.c_str());
	return address;
}

// Reports the token file's permissions (and fixes them with
// --fix-permissions)
void CheckTokenFile(const Options& options, const std::string& path)
{
	if (options.fix_permissions)
	{
		std::string error;
		if (!oauth2::FixPermissions(path, error))
			std::fprintf(stderr, "%s: warning: permissions not fixed: %s\n", cToolName,
				oauth2::DisplayPath(error).c_str());
	}
	ReportPermissions(path, oauth2::FileRole::User);
}

int SaveSignIn(const Options& options, const oauth2::Provider& provider, const std::string& account,
	const std::vector<std::string>& scopes, const oauth2::TokenResponse& response, const std::string& path);

// The offline check of the client ID's form; false after the message
bool CheckClientIdForm(const Options& options, const oauth2::Provider& provider)
{
	std::string format_error;
	if (!options.client_check || oauth2::CheckClientIdFormat(provider, format_error))
		return true;
	std::fprintf(stderr, "%s: %s\n    check the client ID in the provider files or --client-id\n",
		cToolName, format_error.c_str());
	return false;
}

int CommandLogin(const Options& options)
{
	if (options.arguments.size() != 1 || options.arguments[0].empty())
	{
		std::fprintf(stderr, "%s: login needs one account (an e-mail address)\n", cToolName);
		return cExitError;
	}
	const std::string& account = options.arguments[0];
	oauth2::ProviderConfig config;
	if (!LoadConfig(options, config))
		return cExitError;
	oauth2::Provider provider;
	std::vector<std::string> groups;
	if (!FindProvider(options, config, account, provider) || !ScopeGroups(options, provider, groups))
		return cExitError;
	if (!CheckClientIdForm(options, provider))
		return cExitError;
	const std::string path = TokenFilePath(options, provider, account);
	if (path.empty())
	{
		std::fprintf(stderr, "%s: no home folder for the token file: use --token-file\n", cToolName);
		return cExitError;
	}
	const std::vector<std::string> scopes = provider.ScopesFor(groups);
	InstallCancelHandler();
	const oauth2::HttpSettings http = ToolHttpSettings(options);

	oauth2::LoopbackListener listener;
	std::string error;
	if (!listener.Open(provider.redirect, error, http.log))
	{
		std::fprintf(stderr, "%s: %s\n", cToolName, error.c_str());
		return cExitError;
	}
	// Kept: Wait() closes the listener, which forgets it
	const std::string redirect_uri = listener.RedirectUri();
	const oauth2::TokenEndpoint endpoint = oauth2::TokenEndpointOf(provider);
	std::printf("provider: %s (%s)\n", provider.id.c_str(), provider.name.c_str());
	std::printf("account: %s\n", account.c_str());
	std::printf("client ID: %s\n", provider.client_id.c_str());
	std::printf("redirect URI: %s\n", redirect_uri.c_str());
	std::fflush(stdout);

	if (options.client_check)
	{
		oauth2::TokenError probe;
		if (!oauth2::ProbeClient(provider, scopes, redirect_uri, http, probe))
		{
			PrintTokenError("the check of the client registration", probe);
			// Refused by the provider (Microsoft's AADSTS50059 is not
			// IsClientProblem(), which has said it already)
			if (probe.kind == oauth2::TokenError::Kind::OAuth && !probe.IsClientProblem())
				PrintRegistrationHint();
			return cExitFailed;
		}
	}

	const oauth2::Pkce pkce = oauth2::MakePkce();
	const std::string state = oauth2::MakeState();
	oauth2::AuthorizationRequest request;
	request.scopes = scopes;
	request.redirect_uri = redirect_uri;
	request.state = state;
	if (provider.pkce)
		request.code_challenge = pkce.challenge;
	request.login_hint = account;
	if (options.prompt_set)
		request.overrides.emplace_back("prompt", options.prompt);
	const std::string url = oauth2::BuildAuthorizationUrl(provider, request);

	std::fprintf(stderr, "\nSign in to %s as %s in the browser. If it does not open, open this URL:\n\n%s\n\n",
		provider.name.c_str(), account.c_str(), url.c_str());
	if (!options.no_browser)
	{
		const oauth2::BrowserLaunch launch = oauth2::OpenBrowser(url, http.log);
		if (launch.opened)
			std::fprintf(stderr, "Browser started (%s).\n", launch.command.c_str());
		else
			std::fprintf(stderr, "%s: %s\n", cToolName, launch.message.c_str());
	}
	std::fprintf(stderr, "Waiting for the browser (at most %ld s; Ctrl-C to cancel)...\n", options.timeout);

	oauth2::ListenerSettings wait;
	wait.app_name = ToolIdentity().name;
	wait.timeout = options.timeout;
	wait.cancel = &sCancel;
	wait.log = http.log;
	const oauth2::RedirectResult redirect = listener.Wait(state, wait);
	switch (redirect.outcome)
	{
	case oauth2::RedirectResult::Outcome::Code:
		break;
	case oauth2::RedirectResult::Outcome::Error:
		std::fprintf(stderr, "%s: the provider did not sign you in: %s%s%s\n", cToolName, redirect.error.c_str(),
			redirect.error_description.empty() ? "" : ": ", redirect.error_description.c_str());
		if (!redirect.error_uri.empty())
			std::fprintf(stderr, "    see %s\n", redirect.error_uri.c_str());
		return cExitFailed;
	case oauth2::RedirectResult::Outcome::Timeout:
		std::fprintf(stderr, "%s: no answer from the browser within %ld s\n", cToolName, options.timeout);
		return cExitFailed;
	case oauth2::RedirectResult::Outcome::Cancelled:
		std::fprintf(stderr, "%s: cancelled\n", cToolName);
		return cExitFailed;
	default:
		std::fprintf(stderr, "%s: %s\n", cToolName, redirect.message.c_str());
		return cExitFailed;
	}

	oauth2::TokenResponse response;
	oauth2::TokenError token_error;
	if (!oauth2::ExchangeCode(endpoint, redirect.code, provider.pkce ? pkce.verifier : std::string(),
			redirect_uri, http, response, token_error))
	{
		PrintTokenError("the code exchange", token_error);
		return cExitFailed;
	}
	return SaveSignIn(options, provider, account, scopes, response, path);
}

// The end of login and device: prints the answer, keeps the refresh token
int SaveSignIn(const Options& options, const oauth2::Provider& provider, const std::string& account,
	const std::vector<std::string>& scopes, const oauth2::TokenResponse& response, const std::string& path)
{
	const std::string address = PrintTokens(options, provider, scopes, response, account);

	if (response.refresh_token.empty())
	{
		std::printf("refresh token: none\n");
		std::fprintf(stderr, "%s: the provider gave no refresh token; %s kept unchanged\n", cToolName,
			oauth2::DisplayPath(path).c_str());
		return cExitFailed;
	}
	TokenFile file;
	file.provider = provider.id;
	file.account = account;
	file.client_id = provider.client_id;
	file.tenant = provider.tenant;
	file.requested_scopes = scopes;
	file.scope_given = response.scope_given;
	file.granted_scopes = response.scopes;
	file.email = address;
	file.saved_at = UtcNow();
	file.refresh_token = response.refresh_token;
	std::string error;
	if (!SaveTokenFile(path, file, error))
	{
		std::printf("refresh token: %s, not saved\n", ShowSecret(options, response.refresh_token).c_str());
		std::fprintf(stderr, "%s: %s\n", cToolName, error.c_str());
		return cExitFailed;
	}
	std::printf("refresh token: %s, saved in %s\n", ShowSecret(options, response.refresh_token).c_str(),
		oauth2::DisplayPath(path).c_str());
	return 0;
}

int CommandDevice(const Options& options)
{
	if (options.arguments.size() != 1 || options.arguments[0].empty())
	{
		std::fprintf(stderr, "%s: device needs one account (an e-mail address)\n", cToolName);
		return cExitError;
	}
	const std::string& account = options.arguments[0];
	oauth2::ProviderConfig config;
	if (!LoadConfig(options, config))
		return cExitError;
	oauth2::Provider provider;
	std::vector<std::string> groups;
	if (!FindProvider(options, config, account, provider) || !ScopeGroups(options, provider, groups))
		return cExitError;
	if (!provider.SupportsDeviceCode())
	{
		std::fprintf(stderr, "%s: %s offers no device code sign-in (no \"device_authorization_endpoint\" in "
			"its entry): use %s login\n", cToolName, provider.name.c_str(), cToolName);
		return cExitError;
	}
	if (!CheckClientIdForm(options, provider))
		return cExitError;
	const std::string path = TokenFilePath(options, provider, account);
	if (path.empty())
	{
		std::fprintf(stderr, "%s: no home folder for the token file: use --token-file\n", cToolName);
		return cExitError;
	}
	const std::vector<std::string> scopes = provider.ScopesFor(groups);
	InstallCancelHandler();
	const oauth2::HttpSettings http = ToolHttpSettings(options);
	std::printf("provider: %s (%s)\n", provider.id.c_str(), provider.name.c_str());
	std::printf("account: %s\n", account.c_str());
	std::printf("client ID: %s\n", provider.client_id.c_str());
	std::fflush(stdout);

	// The request also checks the client: no ProbeClient()
	oauth2::DeviceAuthorization authorization;
	oauth2::TokenError error;
	if (!oauth2::RequestDeviceAuthorization(provider, scopes, http, authorization, error))
	{
		PrintTokenError("the device code request", error);
		if (oauth2::IsUnknownClient(error) && !error.IsClientProblem())
			PrintRegistrationHint();
		return error.kind == oauth2::TokenError::Kind::Config ? cExitError : cExitFailed;
	}

	std::fprintf(stderr, "\nOn a phone or another computer, open\n\n    %s\n\nand enter the code\n\n    %s\n\n",
		authorization.verification_uri.c_str(), authorization.user_code.c_str());
	if (!authorization.verification_uri_complete.empty())
		std::fprintf(stderr, "(or open %s, which holds the code)\n", authorization.verification_uri_complete.c_str());
	std::fprintf(stderr, "Then sign in to %s as %s. The code expires in %ld s (%s).\n", provider.name.c_str(),
		account.c_str(), authorization.expires_in,
		LocalTime(authorization.received_at + authorization.expires_in).c_str());
	if (options.timeout_set && options.timeout < authorization.expires_in)
		std::fprintf(stderr, "Waiting for the sign-in (at most %ld s; Ctrl-C to cancel)...\n", options.timeout);
	else
		std::fprintf(stderr, "Waiting for the sign-in (Ctrl-C to cancel)...\n");

	const oauth2::TokenEndpoint endpoint = oauth2::TokenEndpointOf(provider);
	oauth2::TokenResponse response;
	if (!oauth2::WaitForDeviceToken(endpoint, authorization, options.timeout_set ? options.timeout : 0, http,
		response, error))
	{
		switch (oauth2::ClassifyDevicePoll(error))
		{
		case oauth2::DevicePoll::Denied:
			std::fprintf(stderr, "%s: the sign-in was refused: %s\n", cToolName, error.message.c_str());
			break;
		case oauth2::DevicePoll::Expired:
			std::fprintf(stderr, "%s: the provider says the code expired (%s): run the command again\n",
				cToolName, error.message.c_str());
			break;
		default:
			if (error.kind == oauth2::TokenError::Kind::Cancelled)
				std::fprintf(stderr, "%s: cancelled\n", cToolName);
			else if (error.kind == oauth2::TokenError::Kind::Timeout)
				std::fprintf(stderr, "%s: %s\n", cToolName, error.message.c_str());
			else
			{
				PrintTokenError("waiting for the sign-in", error);
				// Microsoft: the device code is a "public client flow"
				if (error.aadsts == 7000218)
					std::fprintf(stderr, "    Microsoft: turn on \"Allow public client flows\" in the app "
						"registration\n    (docs/oauth2/register-microsoft.md, step 3)\n");
			}
			break;
		}
		return cExitFailed;
	}
	return SaveSignIn(options, provider, account, scopes, response, path);
}

int CommandRefresh(const Options& options)
{
	if (options.arguments.size() != 1 || options.arguments[0].empty())
	{
		std::fprintf(stderr, "%s: refresh needs one account (an e-mail address)\n", cToolName);
		return cExitError;
	}
	const std::string& account = options.arguments[0];
	oauth2::ProviderConfig config;
	if (!LoadConfig(options, config))
		return cExitError;
	oauth2::Provider provider;
	if (!FindProvider(options, config, account, provider))
		return cExitError;
	const std::string path = TokenFilePath(options, provider, account);
	if (path.empty())
	{
		std::fprintf(stderr, "%s: no home folder for the token file: use --token-file\n", cToolName);
		return cExitError;
	}
	CheckTokenFile(options, path);
	TokenFile file;
	std::string error;
	const int status = LoadTokenFile(path, file, error);
	if (status != 0)
	{
		std::fprintf(stderr, "%s: %s\n", cToolName, error.c_str());
		if (status == cExitNotFound)
			std::fprintf(stderr, "    sign in first: %s login %s\n", cToolName, account.c_str());
		return status;
	}
	if (file.provider != provider.id)
	{
		std::fprintf(stderr, "%s: %s holds a token of provider %s, not %s\n", cToolName,
			oauth2::DisplayPath(path).c_str(), file.provider.c_str(), provider.id.c_str());
		return cExitError;
	}
	// A refresh token works only with the client it was issued to
	if (file.client_id != provider.client_id)
	{
		std::fprintf(stderr, "%s: the refresh token was issued to client ID %s, but the configuration has %s:\n"
			"    sign in again with: %s login %s\n", cToolName, file.client_id.c_str(), provider.client_id.c_str(),
			cToolName, account.c_str());
		return cExitFailed;
	}

	InstallCancelHandler();
	const oauth2::HttpSettings http = ToolHttpSettings(options);
	const std::vector<std::string> scopes =
		oauth2::RefreshScopes(provider, file.requested_scopes, file.scope_given, file.granted_scopes);
	std::printf("provider: %s (%s)\n", provider.id.c_str(), provider.name.c_str());
	std::printf("account: %s\n", account.c_str());
	std::printf("token file: %s (saved %s)\n", oauth2::DisplayPath(path).c_str(), file.saved_at.c_str());
	std::fflush(stdout);

	oauth2::TokenResponse response;
	oauth2::TokenError token_error;
	if (!oauth2::RefreshToken(oauth2::TokenEndpointOf(provider), file.refresh_token, scopes, http, response,
			token_error))
	{
		PrintTokenError("the refresh", token_error);
		if (token_error.RequiresSignIn())
			std::fprintf(stderr, "    the refresh token is no longer valid (expired, revoked or replaced):\n"
				"    sign in again with: %s login %s\n", cToolName, account.c_str());
		return cExitFailed;
	}
	const std::string address = PrintTokens(options, provider, scopes, response, account);

	const bool rotated = !response.refresh_token.empty() && response.refresh_token != file.refresh_token;
	const bool scopes_changed = response.scope_given &&
		(!file.scope_given || response.scopes != file.granted_scopes);
	if (rotated)
		file.refresh_token = response.refresh_token;
	if (response.scope_given)
	{
		file.scope_given = true;
		file.granted_scopes = response.scopes;
	}
	if (!address.empty())
		file.email = address;
	if (!rotated && !scopes_changed)
	{
		std::printf("refresh token: unchanged\n");
		return 0;
	}
	file.saved_at = UtcNow();
	if (!SaveTokenFile(path, file, error))
	{
		std::printf("refresh token: %s, not saved\n", ShowSecret(options, file.refresh_token).c_str());
		std::fprintf(stderr, "%s: %s\n", cToolName, error.c_str());
		// A rotated token that is not saved is lost: the old one is dead
		if (rotated)
			std::fprintf(stderr, "    the old refresh token is no longer valid: sign in again\n");
		return cExitFailed;
	}
	std::printf("refresh token: %s%s, saved in %s\n", rotated ? "rotated, " : "",
		ShowSecret(options, file.refresh_token).c_str(), oauth2::DisplayPath(path).c_str());
	return 0;
}

// Options that take a value
bool TakesValue(const std::string& arg)
{
	static const char* const cNames[] = { "--system-config", "--user-config", "--provider", "--client-id",
		"--tenant", "--services", "--prompt", "--timeout", "--token-file", "--ca-file" };
	return std::find(std::begin(cNames), std::end(cNames), arg) != std::end(cNames);
}

std::vector<std::string> SplitList(const std::string& text)
{
	std::vector<std::string> items;
	std::stringstream stream(text);
	std::string item;
	while (std::getline(stream, item, ','))
		if (!item.empty())
			items.push_back(item);
	return items;
}

// Parses the command line; false after printing an error.  Options may come
// before or after the command; "--" ends them.
bool ParseOptions(int argc, char* argv[], Options& options)
{
	options.files = oauth2::DefaultConfigFiles(ToolIdentity(), OAUTH2_TOOL_SYSCONFDIR);
	bool options_ended = false;
	for (int i = 1; i < argc; ++i)
	{
		const std::string arg = argv[i];
		const bool option = !options_ended && arg.size() > 1 && arg[0] == '-' && arg != "--version" &&
			arg != "--help" && arg != "-h";
		if (!option)
		{
			if (arg == "--" && !options_ended)
				options_ended = true;
			else if (options.command.empty())
				options.command = arg;
			else
				options.arguments.push_back(arg);
			continue;
		}
		if (TakesValue(arg) && i + 1 >= argc)
		{
			std::fprintf(stderr, "%s: %s needs a value\n", cToolName, arg.c_str());
			return false;
		}
		if (arg == "--system-config")
		{
			options.files.system_file = argv[++i];
			options.files.system_file_explicit = true;
		}
		else if (arg == "--user-config")
			options.files.user_file = argv[++i];
		else if (arg == "--no-user-config")
			options.files.user_file.clear();
		else if (arg == "--fix-permissions")
			options.fix_permissions = true;
		else if (arg == "--provider")
			options.provider = argv[++i];
		else if (arg == "--client-id")
			options.account.client_id = argv[++i];
		else if (arg == "--tenant")
			options.account.tenant = argv[++i];
		else if (arg == "--services")
			options.services = SplitList(argv[++i]);
		else if (arg == "--prompt")
		{
			options.prompt_set = true;
			options.prompt = argv[++i];
		}
		else if (arg == "--no-browser")
			options.no_browser = true;
		else if (arg == "--no-client-check")
			options.client_check = false;
		else if (arg == "--timeout")
		{
			char* end = nullptr;
			const std::string value = argv[++i];
			options.timeout = std::strtol(value.c_str(), &end, 10);
			options.timeout_set = true;
			if (value.empty() || *end != '\0' || options.timeout <= 0 || options.timeout > 3600)
			{
				std::fprintf(stderr, "%s: --timeout needs a number of seconds from 1 to 3600\n", cToolName);
				return false;
			}
		}
		else if (arg == "--token-file")
			options.token_file = argv[++i];
		else if (arg == "--ca-file")
			options.ca_file = argv[++i];
		else if (arg == "--verbose" || arg == "-v")
			options.verbose = true;
		else if (arg == "--show-token")
			options.show_token = true;
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
	if (options.command == "login")
		return CommandLogin(options);
	if (options.command == "device")
		return CommandDevice(options);
	if (options.command == "refresh")
		return CommandRefresh(options);

	if (!options.command.empty())
		std::fprintf(stderr, "%s: unknown command \"%s\"\n", cToolName, options.command.c_str());
	PrintUsage(stderr);
	return cExitError;
}
