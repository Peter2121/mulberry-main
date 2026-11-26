/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

#include "oauth2/Browser.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <vector>

extern char** environ;

namespace oauth2
{

namespace
{

// How long a command may take to fail before it counts as opened: xdg-open
// returns at once with a desktop environment, but runs the browser in the
// foreground without one
const int cXdgOpenGraceMs = 3000;
const int cBrowserGraceMs = 1000;
const int cPollMs = 50;

enum class Spawn
{
	Exited,		// exit status 0
	Running,	// still running after the grace period, or not ours to wait for
	Failed		// not started, or a non-zero exit status
};

void Log(const LogFunction& log, const std::string& line)
{
	if (log)
		log(line);
}

bool IsOpenableUrl(const std::string& url)
{
	if (url.compare(0, 8, "https://") != 0 && url.compare(0, 7, "http://") != 0)
		return false;
	for (unsigned char c : url)
		if (c <= 0x20 || c == 0x7f)
			return false;
	return true;
}

void SleepMs(int ms)
{
	struct timespec delay;
	delay.tv_sec = ms / 1000;
	delay.tv_nsec = (ms % 1000) * 1000000L;
	while (nanosleep(&delay, &delay) == -1 && errno == EINTR)
		;
}

Spawn Run(const std::vector<std::string>& words, int grace_ms, std::string& detail)
{
	std::vector<char*> argv;
	for (const std::string& word : words)
		argv.push_back(const_cast<char*>(word.c_str()));
	argv.push_back(nullptr);

	posix_spawn_file_actions_t actions;
	posix_spawn_file_actions_init(&actions);
	posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
	posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);

	// Own process group; no blocked signals; the usual signals back to
	// their defaults (an ignored signal would stay ignored across exec)
	posix_spawnattr_t attributes;
	posix_spawnattr_init(&attributes);
	posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);
	posix_spawnattr_setpgroup(&attributes, 0);
	sigset_t none;
	sigemptyset(&none);
	posix_spawnattr_setsigmask(&attributes, &none);
	sigset_t defaults;
	sigemptyset(&defaults);
	for (int signal_number : { SIGINT, SIGQUIT, SIGTERM, SIGHUP, SIGPIPE, SIGCHLD })
		sigaddset(&defaults, signal_number);
	posix_spawnattr_setsigdefault(&attributes, &defaults);

	pid_t pid = 0;
	const int rc = posix_spawnp(&pid, argv[0], &actions, &attributes, argv.data(), environ);
	posix_spawn_file_actions_destroy(&actions);
	posix_spawnattr_destroy(&attributes);
	if (rc != 0)
	{
		detail = std::strerror(rc);
		return Spawn::Failed;
	}

	for (int waited = 0; waited < grace_ms; waited += cPollMs)
	{
		int status = 0;
		const pid_t done = waitpid(pid, &status, WNOHANG);
		if (done == pid)
		{
			if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
			{
				detail = "exit status 0";
				return Spawn::Exited;
			}
			if (WIFEXITED(status))
				// 127: the command was not found by a posix_spawnp() that
				// reports it from the child
				detail = "exit status " + std::to_string(WEXITSTATUS(status));
			else if (WIFSIGNALED(status))
				detail = "killed by signal " + std::to_string(WTERMSIG(status));
			else
				detail = "stopped";
			return Spawn::Failed;
		}
		if (done == -1 && errno != EINTR)
		{
			// ECHILD: the host reaps its children itself (SIGCHLD ignored)
			detail = "started";
			return Spawn::Running;
		}
		SleepMs(cPollMs);
	}
	detail = "still running";
	return Spawn::Running;
}

// One entry of $BROWSER as words, with the URL put in
std::vector<std::string> BrowserWords(const std::string& entry, const std::string& url)
{
	std::vector<std::string> words;
	bool placed = false;
	size_t i = 0;
	while (i < entry.size())
	{
		while (i < entry.size() && (entry[i] == ' ' || entry[i] == '\t'))
			++i;
		if (i >= entry.size())
			break;
		std::string word;
		for (; i < entry.size() && entry[i] != ' ' && entry[i] != '\t'; ++i)
		{
			if (entry[i] == '%' && i + 1 < entry.size() && entry[i + 1] == 's')
			{
				word += url;
				placed = true;
				++i;
			}
			else if (entry[i] == '%' && i + 1 < entry.size() && entry[i + 1] == '%')
			{
				word += '%';
				++i;
			}
			else
				word += entry[i];
		}
		words.push_back(word);
	}
	if (!words.empty() && !placed)
		words.push_back(url);
	return words;
}

} // namespace

BrowserLaunch OpenBrowser(const std::string& url, const LogFunction& log)
{
	BrowserLaunch launch;
	if (!IsOpenableUrl(url))
	{
		launch.message = "not an http or https URL without spaces: not opened";
		Log(log, "* browser: " + launch.message);
		return launch;
	}

	std::vector<std::vector<std::string>> commands;
	commands.push_back({ "xdg-open", url });
	const char* browser = std::getenv("BROWSER");
	if (browser)
	{
		const std::string list = browser;
		size_t start = 0;
		while (start <= list.size())
		{
			size_t end = list.find(':', start);
			if (end == std::string::npos)
				end = list.size();
			const std::vector<std::string> words = BrowserWords(list.substr(start, end - start), url);
			if (!words.empty())
				commands.push_back(words);
			start = end + 1;
		}
	}

	std::string tried;
	for (size_t i = 0; i < commands.size(); ++i)
	{
		const std::string& name = commands[i][0];
		std::string detail;
		const Spawn result = Run(commands[i], i == 0 ? cXdgOpenGraceMs : cBrowserGraceMs, detail);
		Log(log, "* browser: " + name + ": " + detail);
		if (result != Spawn::Failed)
		{
			launch.opened = true;
			launch.command = name;
			return launch;
		}
		tried += (tried.empty() ? "" : ", ") + name + " (" + detail + ")";
	}
	launch.message = "no browser could be started: " + tried + (browser ? "" : "; $BROWSER is not set");
	return launch;
}

} // namespace oauth2
