/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

// oauth2-listener-driver: opens the OAuth2 core's loopback listener and
// waits for one redirect, for tests/listener-tests.sh (curl plays the
// browser).
//
// Usage: oauth2-listener-driver [options] REDIRECT STATE
//
// Options:
//   --ready FILE        write "PORT REDIRECT_URI" to FILE once listening
//   --timeout S         whole wait, in seconds (default 300)
//   --cancel-after S    cancel from another thread after S seconds
//   --app NAME          application name shown on the page
//   --log FILE          append the listener's log to FILE
//   --show-code         print the code instead of its redaction
//
// Output: "name: value" lines.  Exit status: 0 a code was received, 1 any
// other end (error, timeout, cancelled), 2 usage error or no listener.

#include "oauth2/Listener.h"
#include "oauth2/Redact.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

namespace
{

const char* const cName = "oauth2-listener-driver";

int Usage()
{
	std::fprintf(stderr, "usage: %s [--ready FILE] [--timeout S] [--cancel-after S] [--app NAME]\n"
		"       [--log FILE] [--show-code] REDIRECT STATE\n", cName);
	return 2;
}

bool WriteReady(const std::string& path, const std::string& text)
{
	const std::string temp = path + ".tmp";
	std::FILE* f = std::fopen(temp.c_str(), "w");
	if (!f)
		return false;
	const bool ok = std::fputs(text.c_str(), f) >= 0;
	return (std::fclose(f) == 0) && ok && std::rename(temp.c_str(), path.c_str()) == 0;
}

} // namespace

int main(int argc, char* argv[])
{
	std::string ready;
	std::string log_file;
	long cancel_after = -1;
	bool show_code = false;
	oauth2::ListenerSettings settings;
	std::string arguments[2];
	int count = 0;

	for (int i = 1; i < argc; ++i)
	{
		const std::string arg = argv[i];
		const bool value = i + 1 < argc;
		if (count > 0 || arg.empty() || arg[0] != '-')
		{
			if (count >= 2)
				return Usage();
			arguments[count++] = arg;
		}
		else if (arg == "--show-code")
			show_code = true;
		else if (!value)
			return Usage();
		else if (arg == "--ready")
			ready = argv[++i];
		else if (arg == "--timeout")
			settings.timeout = std::atol(argv[++i]);
		else if (arg == "--cancel-after")
			cancel_after = std::atol(argv[++i]);
		else if (arg == "--app")
			settings.app_name = argv[++i];
		else if (arg == "--log")
			log_file = argv[++i];
		else
			return Usage();
	}
	if (count != 2)
		return Usage();

	std::FILE* log = nullptr;
	if (!log_file.empty())
	{
		log = std::fopen(log_file.c_str(), "a");
		if (!log)
		{
			std::perror(log_file.c_str());
			return 2;
		}
		settings.log = [log](const std::string& line) { std::fprintf(log, "%s\n", line.c_str()); std::fflush(log); };
	}

	oauth2::LoopbackListener listener;
	std::string error;
	if (!listener.Open(arguments[0], error, settings.log))
	{
		std::printf("result: failed\nmessage: %s\n", error.c_str());
		return 2;
	}
	std::printf("redirect_uri: %s\n", listener.RedirectUri().c_str());
	std::fflush(stdout);
	if (!ready.empty() &&
		!WriteReady(ready, std::to_string(listener.Port()) + " " + listener.RedirectUri() + "\n"))
	{
		std::perror(ready.c_str());
		return 2;
	}

	std::atomic<bool> cancel(false);
	std::thread canceller;
	if (cancel_after >= 0)
	{
		settings.cancel = &cancel;
		canceller = std::thread([&cancel, cancel_after] {
			std::this_thread::sleep_for(std::chrono::seconds(cancel_after));
			cancel = true;
		});
	}

	const oauth2::RedirectResult result = listener.Wait(arguments[1], settings);
	if (canceller.joinable())
	{
		cancel = true;
		canceller.join();
	}

	std::printf("result: %s\n", oauth2::OutcomeName(result.outcome));
	if (!result.code.empty())
		std::printf("code: %s\n", show_code ? result.code.c_str() : oauth2::Redact(result.code).c_str());
	if (!result.error.empty())
		std::printf("error: %s\n", result.error.c_str());
	if (!result.error_description.empty())
		std::printf("error_description: %s\n", result.error_description.c_str());
	std::string names;
	for (const auto& param : result.params)
		names += (names.empty() ? "" : " ") + param.first;
	std::printf("params: %s\n", names.c_str());
	std::printf("message: %s\n", result.message.c_str());
	if (log)
		std::fclose(log);
	return result.outcome == oauth2::RedirectResult::Outcome::Code ? 0 : 1;
}
