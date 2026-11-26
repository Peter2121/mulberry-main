/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

// Opening the sign-in URL in the user's browser (OAUTH2-PLAN.md, task 1.6):
// xdg-open, then the commands of $BROWSER.  Each is started directly with
// posix_spawnp(), never through a shell, so the '&' and ';' of the URL are
// never interpreted.

#ifndef OAUTH2_BROWSER_H
#define OAUTH2_BROWSER_H

#include "oauth2/Http.h"

#include <string>

namespace oauth2
{

struct BrowserLaunch
{
	bool opened = false;	// a command started and did not fail at once
	std::string command;	// the one that worked ("xdg-open")
	std::string message;	// what was tried, one line: shown when !opened
};

// Starts xdg-open URL.  If it cannot be started or fails within a few
// seconds, tries each entry of $BROWSER (a list separated by ':'; each
// entry is split into words at spaces, "%s" is replaced by the URL, which
// is otherwise added as the last argument; "%%" is a '%').
//
// The commands run in their own process group (Ctrl-C in the terminal does
// not reach the browser), with standard input and output on /dev/null.  A
// command that is still running after its grace period (a browser started
// in the foreground) counts as opened; it is left running, and the caller
// reaps it or exits.
//
// Only http:// and https:// URLs without spaces or control characters are
// opened: anything else is refused (a URL starting with '-' would be an
// option).  The log gets the commands tried, never the URL.
BrowserLaunch OpenBrowser(const std::string& url, const LogFunction& log = LogFunction());

} // namespace oauth2

#endif
