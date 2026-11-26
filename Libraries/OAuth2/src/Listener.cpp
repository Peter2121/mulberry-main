/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

#include "oauth2/Listener.h"

#include "oauth2/Random.h"
#include "oauth2/Redact.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <map>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace oauth2
{

namespace
{

typedef std::chrono::steady_clock Clock;

const size_t cMaxRequest = 8192;		// request line and headers, bytes
const size_t cMaxClients = 16;			// open connections; the oldest goes
const long cClientSeconds = 10;			// to send a whole request
const int cTickMs = 100;				// cancellation check interval
const int cSendMs = 1000;				// to send an answer
const int cOpenAttempts = 10;			// to find a port free on both families

struct Base
{
	const char* prefix;
	const char* host;		// as in the URL and the Host header
	bool ipv4;
	bool ipv6;
};

const Base cBases[] = {
	{ "http://127.0.0.1", "127.0.0.1", true, false },
	{ "http://localhost", "localhost", true, true },
	{ "http://[::1]", "[::1]", false, true }
};

std::string Lower(std::string text)
{
	for (char& c : text)
		c = (char) std::tolower((unsigned char) c);
	return text;
}

// Text from the network for a log line or a page: no control characters,
// at most max bytes
std::string Printable(const std::string& text, size_t max = 300)
{
	std::string result;
	for (char c : text)
	{
		if (result.size() >= max)
		{
			result += "...";
			break;
		}
		result += ((unsigned char) c < 0x20 || c == 0x7f) ? ' ' : c;
	}
	return result;
}

std::string HtmlEscape(const std::string& text)
{
	std::string result;
	for (char c : text)
	{
		switch (c)
		{
		case '&': result += "&amp;"; break;
		case '<': result += "&lt;"; break;
		case '>': result += "&gt;"; break;
		case '"': result += "&quot;"; break;
		case '\'': result += "&#39;"; break;
		default: result += c; break;
		}
	}
	return result;
}

// The names redacted in the listener's log: the secrets and the state
const std::vector<std::string>& LogRedactedNames()
{
	static const std::vector<std::string> sNames = [] {
		std::vector<std::string> names = SecretFieldNames();
		names.push_back("state");
		return names;
	}();
	return sNames;
}

// A listening socket on the loopback address of family; not inherited by
// the processes the application starts (browser)
int OpenSocket(int family, unsigned short port, int& error)
{
	const int fd = ::socket(family, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
	if (fd < 0)
	{
		error = errno;
		return -1;
	}

	int result;
	if (family == AF_INET)
	{
		sockaddr_in address;
		std::memset(&address, 0, sizeof(address));
		address.sin_family = AF_INET;
		address.sin_port = htons(port);
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		result = ::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address));
	}
	else
	{
		const int on = 1;
		::setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &on, sizeof(on));
		sockaddr_in6 address;
		std::memset(&address, 0, sizeof(address));
		address.sin6_family = AF_INET6;
		address.sin6_port = htons(port);
		address.sin6_addr = in6addr_loopback;
		result = ::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address));
	}
	if (result != 0 || ::listen(fd, (int) cMaxClients) != 0)
	{
		error = errno;
		::close(fd);
		return -1;
	}
	return fd;
}

unsigned short LocalPort(int fd)
{
	sockaddr_storage address;
	socklen_t length = sizeof(address);
	if (::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &length) != 0)
		return 0;
	if (address.ss_family == AF_INET)
		return ntohs(reinterpret_cast<sockaddr_in*>(&address)->sin_port);
	if (address.ss_family == AF_INET6)
		return ntohs(reinterpret_cast<sockaddr_in6*>(&address)->sin6_port);
	return 0;
}

struct Client
{
	int fd = -1;
	std::string data;
	Clock::time_point deadline;
};

struct Answer
{
	int status = 400;
	std::string reason = "Bad Request";
	std::string body;
	bool html = false;
	std::string why;			// for the log
	bool final = false;			// the redirect: stop after it
	RedirectResult result;
};

Answer Plain(int status, const char* reason, const std::string& why)
{
	Answer answer;
	answer.status = status;
	answer.reason = reason;
	answer.body = std::string(reason) + "\n";
	answer.why = why;
	return answer;
}

std::string Page(const std::string& title, const std::string& text)
{
	return "<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\"><title>" + HtmlEscape(title) +
		"</title>\n<style>body{font-family:sans-serif;margin:3em auto;max-width:36em;"
		"padding:0 1em;line-height:1.5}</style></head>\n<body><h1>" + HtmlEscape(title) +
		"</h1>\n<p>" + HtmlEscape(text) + "</p></body></html>\n";
}

void SendAll(int fd, const std::string& data)
{
	size_t sent = 0;
	const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(cSendMs);
	while (sent < data.size() && Clock::now() < deadline)
	{
		const ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
		if (n > 0)
		{
			sent += (size_t) n;
			continue;
		}
		if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
		{
			pollfd p = { fd, POLLOUT, 0 };
			::poll(&p, 1, 50);
			continue;
		}
		break;
	}
}

void Respond(int fd, const Answer& answer)
{
	std::string head = "HTTP/1.1 " + std::to_string(answer.status) + " " + answer.reason + "\r\n";
	head += answer.html ? "Content-Type: text/html; charset=utf-8\r\n" : "Content-Type: text/plain; charset=utf-8\r\n";
	head += "Content-Length: " + std::to_string(answer.body.size()) + "\r\n";
	if (answer.status == 405)
		head += "Allow: GET\r\n";
	head += "Cache-Control: no-store\r\n"
		"Referrer-Policy: no-referrer\r\n"
		"X-Content-Type-Options: nosniff\r\n"
		"Content-Security-Policy: default-src 'none'; style-src 'unsafe-inline'\r\n"
		"Connection: close\r\n\r\n";
	SendAll(fd, head + answer.body);
	::shutdown(fd, SHUT_WR);
}

} // namespace

bool IsLoopbackRedirect(const std::string& redirect)
{
	for (const Base& base : cBases)
	{
		const size_t length = std::strlen(base.prefix);
		if (redirect.compare(0, length, base.prefix) != 0)
			continue;
		const std::string path = redirect.substr(length);
		if (path.empty())
			return true;
		if (path[0] != '/')
			return false;
		for (char c : path)
		{
			if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
				c == '-' || c == '.' || c == '_' || c == '~' || c == '/'))
				return false;
		}
		return true;
	}
	return false;
}

const char* OutcomeName(RedirectResult::Outcome outcome)
{
	switch (outcome)
	{
	case RedirectResult::Outcome::None:			return "none";
	case RedirectResult::Outcome::Code:			return "code";
	case RedirectResult::Outcome::Error:		return "error";
	case RedirectResult::Outcome::Timeout:		return "timeout";
	case RedirectResult::Outcome::Cancelled:	return "cancelled";
	case RedirectResult::Outcome::Failed:		return "failed";
	}
	return "unknown";
}

RedirectResult InterpretRedirect(const Params& params, const std::string& expected_state)
{
	RedirectResult result;
	result.params = params;

	std::map<std::string, std::string> values;
	for (const auto& param : params)
	{
		const bool known = param.first == "code" || param.first == "state" || param.first == "error" ||
			param.first == "error_description" || param.first == "error_uri";
		if (known && !values.emplace(param.first, param.second).second)
		{
			result.message = "parameter \"" + param.first + "\" given twice";
			return result;
		}
	}

	if (expected_state.empty())
		result.message = "no state was expected";
	else if (!values.count("state"))
		result.message = "no state in the redirect";
	else if (!ConstantTimeEquals(values["state"], expected_state))
		result.message = "wrong state (not the sign-in this application started)";
	else if (values.count("error"))
	{
		result.outcome = RedirectResult::Outcome::Error;
		result.error = Printable(values["error"], 100);
		result.error_description = Printable(values["error_description"]);
		result.error_uri = Printable(values["error_uri"]);
		result.message = "the provider did not grant access: " + result.error;
		if (!result.error_description.empty())
			result.message += ": " + result.error_description;
	}
	else if (!values["code"].empty())
	{
		result.outcome = RedirectResult::Outcome::Code;
		result.code = values["code"];
		result.message = "authorization code received";
	}
	else
		result.message = "neither a code nor an error in the redirect";
	return result;
}

struct LoopbackListener::Impl
{
	std::vector<int> listeners;
	unsigned short port = 0;
	std::string host;				// "127.0.0.1", "localhost", "[::1]"
	std::string path;				// "/" when the redirect has none
	std::string redirect_uri;

	void Close()
	{
		for (int fd : listeners)
			::close(fd);
		listeners.clear();
	}

	Answer Handle(const std::string& request, const std::string& expected_state,
		const ListenerSettings& settings, std::string& logged_line)
	{
		const size_t line_end = request.find("\r\n");
		const std::string line = request.substr(0, line_end);
		const size_t space1 = line.find(' ');
		const size_t space2 = space1 == std::string::npos ? space1 : line.find(' ', space1 + 1);
		if (space2 == std::string::npos || line.find(' ', space2 + 1) != std::string::npos)
		{
			logged_line = Printable(line, 100);
			return Plain(400, "Bad Request", "not an HTTP request line");
		}
		const std::string method = line.substr(0, space1);
		std::string target = line.substr(space1 + 1, space2 - space1 - 1);
		const std::string version = line.substr(space2 + 1);

		// The target in the log with code and state redacted
		target = target.substr(0, target.find('#'));
		const size_t question = target.find('?');
		const std::string target_path = target.substr(0, question);
		const std::string query = question == std::string::npos ? std::string() : target.substr(question + 1);
		logged_line = Printable(method, 20) + " " + Printable(target_path) +
			(question == std::string::npos ? "" : "?" + Printable(RedactParams(query, LogRedactedNames()))) +
			" " + Printable(version, 20);

		if (version != "HTTP/1.1" && version != "HTTP/1.0")
			return Plain(400, "Bad Request", "not HTTP/1.x");

		// Exactly one Host header, naming the redirect's host and port (a
		// page of another site reaching here through DNS rebinding would
		// carry its own name)
		std::vector<std::string> hosts;
		size_t start = line_end + 2;
		while (start < request.size())
		{
			size_t end = request.find("\r\n", start);
			if (end == std::string::npos)
				end = request.size();
			const std::string header = request.substr(start, end - start);
			const size_t colon = header.find(':');
			if (colon != std::string::npos && Lower(header.substr(0, colon)) == "host")
			{
				std::string value = header.substr(colon + 1);
				const size_t first = value.find_first_not_of(" \t");
				const size_t last = value.find_last_not_of(" \t");
				hosts.push_back(first == std::string::npos ? std::string() : value.substr(first, last - first + 1));
			}
			start = end + 2;
		}
		const std::string expected_host = host + ":" + std::to_string(port);
		if (hosts.size() != 1 || Lower(hosts[0]) != expected_host)
			return Plain(400, "Bad Request", "Host header is not " + expected_host);

		if (method != "GET")
			return Plain(405, "Method Not Allowed", "not GET");
		if (target_path != path)
			return Plain(404, "Not Found", "not the redirect path");

		Params params;
		if (!ParseQuery(query, params))
			return Plain(400, "Bad Request", "malformed query");
		RedirectResult result = InterpretRedirect(params, expected_state);
		if (result.outcome == RedirectResult::Outcome::None)
			return Plain(400, "Bad Request", result.message);

		const std::string app = settings.app_name.empty() ? std::string("the application") : settings.app_name;
		Answer answer;
		answer.status = 200;
		answer.reason = "OK";
		answer.html = true;
		answer.final = true;
		if (result.outcome == RedirectResult::Outcome::Code)
		{
			answer.body = Page((settings.app_name.empty() ? std::string() : settings.app_name + ": ") + "signed in",
				"You can close this window and return to " + app + ".");
		}
		else
		{
			std::string text = "The sign-in was not completed (" + result.error;
			if (!result.error_description.empty())
				text += ": " + result.error_description;
			text += "). You can close this window and return to " + app + ".";
			answer.body = Page((settings.app_name.empty() ? std::string() : settings.app_name + ": ") +
				"sign-in not completed", text);
		}
		answer.why = result.message;
		answer.result = std::move(result);
		return answer;
	}
};

LoopbackListener::LoopbackListener()
	: mImpl(new Impl)
{
}

LoopbackListener::~LoopbackListener()
{
	mImpl->Close();
	delete mImpl;
}

bool LoopbackListener::Open(const std::string& redirect, std::string& error, const LogFunction& log)
{
	Close();
	error.clear();
	const Base* base = nullptr;
	if (IsLoopbackRedirect(redirect))
	{
		for (const Base& b : cBases)
			if (redirect.compare(0, std::strlen(b.prefix), b.prefix) == 0)
				base = &b;
	}
	if (!base)
	{
		error = "not a loopback redirect: \"" + Printable(redirect) + "\"";
		return false;
	}

	bool ipv6 = base->ipv6;
	for (int attempt = 0; attempt < cOpenAttempts && mImpl->listeners.empty() && error.empty(); ++attempt)
	{
		int code = 0;
		unsigned short port = 0;
		if (base->ipv4)
		{
			const int fd = OpenSocket(AF_INET, 0, code);
			if (fd < 0)
			{
				error = std::string("cannot listen on 127.0.0.1: ") + std::strerror(code);
				break;
			}
			mImpl->listeners.push_back(fd);
			port = LocalPort(fd);
		}
		if (ipv6)
		{
			const int fd = OpenSocket(AF_INET6, port, code);
			if (fd >= 0)
			{
				mImpl->listeners.push_back(fd);
				if (!port)
					port = LocalPort(fd);
			}
			else if (base->ipv4 && code == EADDRINUSE)
			{
				// The port is taken on ::1: another one for both
				mImpl->Close();
				continue;
			}
			else if (base->ipv4 && (code == EADDRNOTAVAIL || code == EAFNOSUPPORT || code == EPROTONOSUPPORT))
			{
				ipv6 = false;
				if (log)
					log("* no IPv6 loopback (" + std::string(std::strerror(code)) + "): 127.0.0.1 only");
			}
			else
			{
				error = std::string("cannot listen on [::1]: ") + std::strerror(code);
				mImpl->Close();
				break;
			}
		}
		mImpl->port = port;
	}
	if (mImpl->listeners.empty() || !mImpl->port)
	{
		if (error.empty())
			error = "no free port on both 127.0.0.1 and [::1]";
		mImpl->Close();
		return false;
	}

	const std::string path = redirect.substr(std::strlen(base->prefix));
	mImpl->host = base->host;
	mImpl->path = path.empty() ? "/" : path;
	mImpl->redirect_uri = std::string(base->prefix) + ":" + std::to_string(mImpl->port) + path;
	if (log)
	{
		std::string where;
		if (base->ipv4)
			where = "127.0.0.1:" + std::to_string(mImpl->port);
		if (ipv6)
			where += (where.empty() ? "" : " and ") + std::string("[::1]:") + std::to_string(mImpl->port);
		log("* listening on " + where + " for " + mImpl->redirect_uri);
	}
	return true;
}

std::string LoopbackListener::RedirectUri() const
{
	return mImpl->listeners.empty() ? std::string() : mImpl->redirect_uri;
}

unsigned short LoopbackListener::Port() const
{
	return mImpl->listeners.empty() ? 0 : mImpl->port;
}

void LoopbackListener::Close()
{
	mImpl->Close();
}

RedirectResult LoopbackListener::Wait(const std::string& expected_state, const ListenerSettings& settings)
{
	const auto log = [&settings](const std::string& line) { if (settings.log) settings.log(line); };
	RedirectResult result;
	if (mImpl->listeners.empty())
	{
		result.outcome = RedirectResult::Outcome::Failed;
		result.message = "the listener is not open";
		return result;
	}

	std::vector<Client> clients;
	const auto close_client = [&clients](size_t i) {
		::close(clients[i].fd);
		clients.erase(clients.begin() + (long) i);
	};
	const Clock::time_point deadline = Clock::now() + std::chrono::seconds(settings.timeout);

	while (result.outcome == RedirectResult::Outcome::None)
	{
		if (settings.cancel && settings.cancel->load())
		{
			result.outcome = RedirectResult::Outcome::Cancelled;
			result.message = "sign-in cancelled";
			break;
		}
		const Clock::time_point now = Clock::now();
		if (now >= deadline)
		{
			result.outcome = RedirectResult::Outcome::Timeout;
			result.message = "no answer from the browser within " + std::to_string(settings.timeout) + " s";
			break;
		}

		// Connections that took too long to send their request
		for (size_t i = clients.size(); i-- > 0; )
			if (now >= clients[i].deadline)
				close_client(i);

		std::vector<pollfd> fds;
		for (int fd : mImpl->listeners)
			fds.push_back({ fd, POLLIN, 0 });
		for (const Client& client : clients)
			fds.push_back({ client.fd, POLLIN, 0 });
		const long left = (long) std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
		const int ready = ::poll(fds.data(), (nfds_t) fds.size(), (int) std::min<long>(cTickMs, left + 1));
		if (ready < 0)
		{
			if (errno == EINTR)
				continue;
			result.outcome = RedirectResult::Outcome::Failed;
			result.message = std::string("poll: ") + std::strerror(errno);
			break;
		}
		if (ready == 0)
			continue;

		// Requests first (fds after the listeners), from the last so that
		// erasing keeps the indexes valid
		const size_t first_client = mImpl->listeners.size();
		for (size_t i = clients.size(); i-- > 0 && result.outcome == RedirectResult::Outcome::None; )
		{
			if (!(fds[first_client + i].revents & (POLLIN | POLLHUP | POLLERR)))
				continue;
			Client& client = clients[i];
			char buffer[4096];
			const ssize_t n = ::recv(client.fd, buffer, sizeof(buffer), 0);
			if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
				continue;
			if (n <= 0)
			{
				close_client(i);
				continue;
			}
			client.data.append(buffer, (size_t) n);
			const size_t end = client.data.find("\r\n\r\n");
			if (end == std::string::npos || end > cMaxRequest)
			{
				// The limit applies whether or not the end has arrived
				if (client.data.size() > cMaxRequest)
				{
					log("< (request over " + std::to_string(cMaxRequest) + " bytes)");
					log("> 431 Request Header Fields Too Large");
					Respond(client.fd, Plain(431, "Request Header Fields Too Large", "too large"));
					close_client(i);
				}
				continue;
			}

			std::string logged;
			Answer answer = mImpl->Handle(client.data.substr(0, end), expected_state, settings, logged);
			log("< " + logged);
			log("> " + std::to_string(answer.status) + " " + answer.reason + (answer.final ? "" : " (" + answer.why + ")"));
			Respond(client.fd, answer);
			close_client(i);
			if (answer.final)
				result = std::move(answer.result);
		}
		if (result.outcome != RedirectResult::Outcome::None)
			break;

		for (size_t l = 0; l < mImpl->listeners.size(); ++l)
		{
			if (!(fds[l].revents & POLLIN))
				continue;
			const int fd = ::accept4(mImpl->listeners[l], nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
			if (fd < 0)
				continue;
			if (clients.size() >= cMaxClients)
				close_client(0);
			Client client;
			client.fd = fd;
			client.deadline = Clock::now() + std::chrono::seconds(cClientSeconds);
			clients.push_back(std::move(client));
		}
	}

	while (!clients.empty())
		close_client(clients.size() - 1);
	mImpl->Close();
	log("* " + std::string(OutcomeName(result.outcome)) + ": " + result.message + "; listener closed");
	return result;
}

} // namespace oauth2
