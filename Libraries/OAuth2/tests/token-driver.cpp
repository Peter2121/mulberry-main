/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

// oauth2-token-driver: runs one token request of the OAuth2 core against a
// local mock server (tests/token-server.py, tests/token-tests.sh).  Test
// values only: unlike the library's defaults, http:// to 127.0.0.1 is
// accepted, and secrets are given on the command line.
//
// Usage:
//   oauth2-token-driver [options] URL exchange CODE REDIRECT_URI
//   oauth2-token-driver [options] URL refresh TOKENFILE
//
// Options:
//   --client-id ID       (default "test-client")
//   --client-secret S    sent in the form
//   --verifier V         PKCE code verifier (exchange)
//   --scope "A B"        scopes (refresh)
//   --redirect-domain D  a domain redirects may lead to (repeatable)
//   --save FILE          exchange: write the refresh token to FILE (0600)
//   --timeout S          whole request, in seconds
//   --log FILE           append the library's log to FILE
//   --show-token         print the tokens instead of their redaction
//
// TOKENFILE holds the refresh token on its first line; after a rotation it
// is replaced (through a temporary file, mode 0600).  Output: "name: value"
// lines.  Exit status: 0 success, 1 the request failed, 2 usage error.

#include "oauth2/Redact.h"
#include "oauth2/TokenClient.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace
{

const char* const cName = "oauth2-token-driver";

struct Options
{
	oauth2::TokenEndpoint endpoint;
	std::string verifier;
	std::vector<std::string> scopes;
	std::string save;
	std::string log;
	long timeout = 0;
	bool show_token = false;
	std::vector<std::string> arguments;
};

int Usage()
{
	std::fprintf(stderr,
		"usage: %s [options] URL exchange CODE REDIRECT_URI\n"
		"       %s [options] URL refresh TOKENFILE\n"
		"options: --client-id ID, --client-secret S, --verifier V, --scope \"A B\",\n"
		"         --redirect-domain D, --save FILE, --timeout S, --log FILE, --show-token\n",
		cName, cName);
	return 2;
}

std::vector<std::string> Split(const std::string& text)
{
	std::vector<std::string> words;
	size_t start = 0;
	while (start < text.size())
	{
		size_t end = text.find(' ', start);
		if (end == std::string::npos)
			end = text.size();
		if (end > start)
			words.push_back(text.substr(start, end - start));
		start = end + 1;
	}
	return words;
}

std::string Show(const Options& options, const std::string& secret)
{
	return options.show_token ? secret : oauth2::Redact(secret);
}

bool ReadTokenFile(const std::string& path, std::string& token)
{
	std::ifstream in(path);
	if (!in || !std::getline(in, token) || token.empty())
	{
		std::fprintf(stderr, "%s: no refresh token in %s\n", cName, path.c_str());
		return false;
	}
	return true;
}

// Writes the token to path.tmp (created with mode 0600), then renames it
bool WriteTokenFile(const std::string& path, const std::string& token)
{
	const std::string temp = path + ".tmp";
	::unlink(temp.c_str());
	const int fd = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
	if (fd < 0)
	{
		std::perror(temp.c_str());
		return false;
	}
	const std::string line = token + "\n";
	const bool written = ::write(fd, line.data(), line.size()) == (ssize_t) line.size();
	const bool closed = ::close(fd) == 0;
	if (!written || !closed || ::rename(temp.c_str(), path.c_str()) != 0)
	{
		std::perror(path.c_str());
		::unlink(temp.c_str());
		return false;
	}
	return true;
}

void PrintResponse(const Options& options, const oauth2::TokenResponse& response)
{
	std::printf("result: ok\n");
	std::printf("token_type: %s\n", response.token_type.c_str());
	std::printf("access_token: %s\n", Show(options, response.access_token).c_str());
	std::printf("expires_in: %ld\n", response.expires_in);
	if (response.scope_given)
	{
		std::string scope;
		for (const std::string& s : response.scopes)
			scope += (scope.empty() ? "" : " ") + s;
		std::printf("scope: %s\n", scope.c_str());
	}
	else
		std::printf("scope: (not given)\n");
	std::printf("id_token: %s\n", response.id_token.empty() ? "(none)" : Show(options, response.id_token).c_str());
	if (response.refresh_token_expires_in >= 0)
		std::printf("refresh_token_expires_in: %ld\n", response.refresh_token_expires_in);
}

int PrintError(const oauth2::TokenError& error)
{
	std::printf("result: error\n");
	std::printf("kind: %s\n", oauth2::KindName(error.kind));
	if (error.http_status)
		std::printf("http_status: %ld\n", error.http_status);
	if (!error.error.empty())
		std::printf("error: %s\n", error.error.c_str());
	if (!error.subtype.empty())
		std::printf("subtype: %s\n", error.subtype.c_str());
	if (error.aadsts)
		std::printf("aadsts: %ld\n", error.aadsts);
	if (!error.correlation_id.empty())
		std::printf("correlation_id: %s\n", error.correlation_id.c_str());
	std::printf("requires_sign_in: %s\n", error.RequiresSignIn() ? "yes" : "no");
	std::printf("client_problem: %s\n", error.IsClientProblem() ? "yes" : "no");
	std::printf("message: %s\n", error.message.c_str());
	return 1;
}

} // namespace

int main(int argc, char* argv[])
{
	Options options;
	options.endpoint.client_id = "test-client";
	for (int i = 1; i < argc; ++i)
	{
		const std::string arg = argv[i];
		const bool value = i + 1 < argc;
		if (!options.arguments.empty() || arg.empty() || arg[0] != '-')
			options.arguments.push_back(arg);
		else if (arg == "--show-token")
			options.show_token = true;
		else if (!value)
			return Usage();
		else if (arg == "--client-id")
			options.endpoint.client_id = argv[++i];
		else if (arg == "--client-secret")
			options.endpoint.client_secret = argv[++i];
		else if (arg == "--verifier")
			options.verifier = argv[++i];
		else if (arg == "--scope")
			options.scopes = Split(argv[++i]);
		else if (arg == "--redirect-domain")
			options.endpoint.redirect_domains.push_back(argv[++i]);
		else if (arg == "--save")
			options.save = argv[++i];
		else if (arg == "--timeout")
			options.timeout = std::atol(argv[++i]);
		else if (arg == "--log")
			options.log = argv[++i];
		else
			return Usage();
	}
	if (options.arguments.size() < 2)
		return Usage();
	options.endpoint.url = options.arguments[0];
	const std::string command = options.arguments[1];

	oauth2::HttpSettings settings;
	settings.allow_http_loopback = true;
	if (options.timeout > 0)
		settings.timeout = options.timeout;
	std::FILE* log = nullptr;
	if (!options.log.empty())
	{
		log = std::fopen(options.log.c_str(), "a");
		if (!log)
		{
			std::perror(options.log.c_str());
			return 2;
		}
		settings.log = [log](const std::string& line) { std::fprintf(log, "%s\n", line.c_str()); };
	}

	int status = 2;
	oauth2::TokenResponse response;
	oauth2::TokenError error;
	if (command == "exchange" && options.arguments.size() == 4)
	{
		if (!oauth2::ExchangeCode(options.endpoint, options.arguments[2], options.verifier,
				options.arguments[3], settings, response, error))
			status = PrintError(error);
		else
		{
			PrintResponse(options, response);
			std::printf("refresh_token: %s\n", response.refresh_token.empty() ?
				"(none)" : Show(options, response.refresh_token).c_str());
			status = 0;
			if (!options.save.empty() && !response.refresh_token.empty() &&
				!WriteTokenFile(options.save, response.refresh_token))
				status = 1;
		}
	}
	else if (command == "refresh" && options.arguments.size() == 3)
	{
		const std::string& file = options.arguments[2];
		oauth2::Tokens tokens;
		if (!ReadTokenFile(file, tokens.refresh_token))
			status = 1;
		else if (!oauth2::RefreshToken(options.endpoint, tokens.refresh_token, options.scopes,
				settings, response, error))
			status = PrintError(error);
		else
		{
			PrintResponse(options, response);
			const bool rotated = oauth2::UpdateTokens(tokens, response);
			std::printf("refresh_token: %s\n", rotated ? "rotated (file updated)" : "unchanged");
			status = 0;
			if (rotated && !WriteTokenFile(file, tokens.refresh_token))
				status = 1;
		}
	}
	else
		status = Usage();

	if (log)
		std::fclose(log);
	return status;
}
