/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

// One-shot loopback HTTP listener for the browser's redirect (RFC 8252
// section 7.3; OAUTH2-PLAN.md, task 1.5).
//
// Open() binds an ephemeral port on 127.0.0.1, on [::1], or on both with
// the same port for "localhost" (a browser may resolve localhost to either).
// RedirectUri() is then the redirect_uri of the authorization request and of
// the code exchange.  Wait() serves requests until one carries the expected
// state with a code or an error, answers it with a short page, and closes
// every socket.  Anything else (favicon, wrong path or method, wrong or
// missing state, a foreign Host header) gets an error status and the
// listener keeps waiting.  Wait() blocks; call it off the UI thread.

#ifndef OAUTH2_LISTENER_H
#define OAUTH2_LISTENER_H

#include "oauth2/Encoding.h"
#include "oauth2/Http.h"

#include <atomic>
#include <string>
#include <vector>

namespace oauth2
{

// "http://127.0.0.1", "http://localhost" or "http://[::1]", without a port,
// optionally followed by a path of RFC 3986 unreserved characters and '/'
bool IsLoopbackRedirect(const std::string& redirect);

struct ListenerSettings
{
	std::string app_name;					// shown on the browser page
	long timeout = 300;						// seconds, whole wait
	const std::atomic<bool>* cancel = nullptr;	// set by another thread
	LogFunction log;						// code and state redacted
};

// What the browser brought back
struct RedirectResult
{
	enum class Outcome
	{
		None,
		Code,			// code holds the authorization code
		Error,			// the provider's error (RFC 6749 section 4.1.2.1)
		Timeout,
		Cancelled,
		Failed			// the listener itself failed (message)
	};

	Outcome outcome = Outcome::None;
	std::string code;
	std::string error;				// "access_denied"...
	std::string error_description;
	std::string error_uri;
	Params params;					// every query parameter (code included)
	std::string message;			// one readable line, never the code
};

const char* OutcomeName(RedirectResult::Outcome outcome);

// The parameters of a redirect for the given state: Code, Error, or None
// with a message (state missing or wrong, no code nor error, a parameter
// given twice).  Also for a redirect URL pasted by the user (task 6.2).
RedirectResult InterpretRedirect(const Params& params, const std::string& expected_state);

class LoopbackListener
{
public:
	LoopbackListener();
	~LoopbackListener();
	LoopbackListener(const LoopbackListener&) = delete;
	LoopbackListener& operator=(const LoopbackListener&) = delete;

	// redirect: the provider's redirect (IsLoopbackRedirect()).  False, with
	// error, if it is not one or no socket could be opened.
	bool Open(const std::string& redirect, std::string& error, const LogFunction& log = LogFunction());

	// "http://127.0.0.1:PORT/path"; empty before Open()
	std::string RedirectUri() const;
	unsigned short Port() const;

	// Serves until the redirect with expected_state arrives, the time limit,
	// or the cancellation; then closes.  Open() must have succeeded.
	RedirectResult Wait(const std::string& expected_state, const ListenerSettings& settings);

	// Closes every socket (also done by Wait() and the destructor)
	void Close();

private:
	struct Impl;
	Impl* mImpl;
};

} // namespace oauth2

#endif
