/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

#include "oauth2/ProviderConfig.h"

#include "oauth2/Listener.h"
#include "oauth2/Paths.h"
#include "JsonIndex.h"

#include <nlohmann/json.hpp>

#include <sys/stat.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>

namespace oauth2
{

namespace
{

// Keys kept in file order, so that errors come in reading order
typedef nlohmann::ordered_json Json;

// The configuration files are small; a larger file is a mistake
const size_t cMaxFileSize = 1024 * 1024;

const char* const cComment = "_comment";

// One configuration file
struct Source
{
	std::string name;	// for messages
	std::string text;
	JsonIndex index;
	Json root;
};

struct Problem
{
	size_t offset;
	std::string message;
};

// ---------------------------------------------------------------------------
// Positions and messages
// ---------------------------------------------------------------------------

// 1-based line and column (in characters, counting UTF-8 sequences once)
void LineColumn(const std::string& text, size_t offset, size_t& line, size_t& column)
{
	offset = std::min(offset, text.size());
	line = 1;
	column = 1;
	for (size_t i = 0; i < offset; ++i)
	{
		const unsigned char c = static_cast<unsigned char>(text[i]);
		if (c == '\n')
		{
			++line;
			column = 1;
		}
		else if ((c & 0xC0) != 0x80)
			++column;
	}
}

void SetError(ConfigError& error, const Source& source, size_t offset, const std::string& message)
{
	error.file = source.name;
	LineColumn(source.text, offset, error.line, error.column);
	error.message = message;
}

void SetFileError(ConfigError& error, const std::string& file, const std::string& message)
{
	error.file = file;
	error.line = 0;
	error.column = 0;
	error.message = message;
}

// "[json.exception.parse_error.101] parse error at line 1, column 9: syntax
// error while parsing ..." -> "syntax error while parsing ..." (the position
// is given by the caller)
std::string CleanParseMessage(const std::string& what)
{
	size_t pos = what.find("column ");
	if (pos != std::string::npos)
	{
		pos = what.find(": ", pos);
		if (pos != std::string::npos)
			return what.substr(pos + 2);
	}
	pos = what.find("] ");
	return pos != std::string::npos ? what.substr(pos + 2) : what;
}

// A hint for the usual mistakes in hand-edited JSON
std::string ParseHint(const std::string& text, size_t offset)
{
	if (offset >= text.size())
		return std::string();
	const char c = text[offset];
	if (c == '/' || c == '#')
		return " (JSON has no comments: use a \"_comment\" key)";
	if (c == '\'')
		return " (strings and keys need double quotes)";
	if (c == '}' || c == ']')
	{
		size_t i = offset;
		while (i > 0)
		{
			const char p = text[--i];
			if (p == ' ' || p == '\t' || p == '\n' || p == '\r')
				continue;
			if (p == ',')
				return " (remove the comma before it)";
			break;
		}
	}
	return std::string();
}

// Unescaped segments of a JSON pointer
std::vector<std::string> PointerSegments(const std::string& pointer)
{
	std::vector<std::string> segments;
	size_t start = 1;
	while (start <= pointer.size() && !pointer.empty())
	{
		size_t end = pointer.find('/', start);
		if (end == std::string::npos)
			end = pointer.size();
		std::string segment;
		for (size_t i = start; i < end; ++i)
		{
			if (pointer[i] == '~' && i + 1 < end)
			{
				segment += pointer[i + 1] == '1' ? '/' : '~';
				++i;
			}
			else
				segment += pointer[i];
		}
		segments.push_back(segment);
		start = end + 1;
	}
	return segments;
}

// Where an object is, for messages: "at the top level", "in provider
// \"google\"", "in \"scopes\" of provider \"google\""
std::string DescribeObject(const std::string& pointer)
{
	const std::vector<std::string> s = PointerSegments(pointer);
	if (s.empty())
		return "at the top level";
	if (s.size() == 1)
		return "in \"" + s[0] + "\"";
	if (s.size() == 2 && s[0] == "providers")
		return "in provider \"" + s[1] + "\"";
	if (s.size() >= 3 && s[0] == "providers")
		return "in \"" + s[2] + "\" of provider \"" + s[1] + "\"";
	return "in \"" + s.back() + "\"";
}

std::string JoinNames(const std::vector<std::string>& names)
{
	std::string result;
	for (const std::string& name : names)
		result += (result.empty() ? "" : ", ") + name;
	return result;
}

// ---------------------------------------------------------------------------
// Value checks
// ---------------------------------------------------------------------------

bool HasControlCharacters(const std::string& text)
{
	for (char c : text)
	{
		const unsigned char u = static_cast<unsigned char>(c);
		if (u < 0x20 || u == 0x7F)
			return true;
	}
	return false;
}

bool HasOuterSpace(const std::string& text)
{
	return !text.empty() && (text.front() == ' ' || text.back() == ' ');
}

bool IsProviderId(const std::string& id)
{
	if (id.empty() || id.front() == '-')
		return false;
	for (char c : id)
	{
		if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'))
			return false;
	}
	return true;
}

// Lower-case host or domain name: labels of a-z 0-9 '-', separated by dots
bool IsHostName(const std::string& host)
{
	if (host.empty() || host.size() > 253 || host.front() == '.' || host.back() == '.' ||
		host.find("..") != std::string::npos)
		return false;
	for (char c : host)
	{
		if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.'))
			return false;
	}
	return true;
}

// Characters allowed in the path and query of the URLs we accept
bool IsUrlCharacter(char c)
{
	const unsigned char u = static_cast<unsigned char>(c);
	return u > 0x20 && u < 0x7F && c != '"' && c != '<' && c != '>' && c != '\\' && c != '^' &&
		c != '`' && c != '{' && c != '|' && c != '}' && c != '#';
}

// https://host[:port][/path][?query]; "{tenant}" allowed in the path
bool IsHttpsUrl(std::string url)
{
	static const std::string cScheme = "https://";
	if (url.compare(0, cScheme.size(), cScheme) != 0)
		return false;
	size_t pos;
	while ((pos = url.find("{tenant}")) != std::string::npos)
		url.replace(pos, 8, "tenant");

	const size_t hostStart = cScheme.size();
	size_t hostEnd = hostStart;
	while (hostEnd < url.size() && url[hostEnd] != '/' && url[hostEnd] != ':' && url[hostEnd] != '?')
		++hostEnd;
	if (!IsHostName(url.substr(hostStart, hostEnd - hostStart)))
		return false;
	size_t rest = hostEnd;
	if (rest < url.size() && url[rest] == ':')
	{
		size_t digits = rest + 1;
		while (digits < url.size() && url[digits] >= '0' && url[digits] <= '9')
			++digits;
		if (digits == rest + 1 || digits - rest - 1 > 5)
			return false;
		rest = digits;
	}
	if (rest < url.size() && url[rest] != '/' && url[rest] != '?')
		return false;
	for (size_t i = rest; i < url.size(); ++i)
	{
		if (!IsUrlCharacter(url[i]))
			return false;
	}
	return true;
}

// RFC 6749 section 3.3: scope-token = 1*( %x21 / %x23-5B / %x5D-7E )
bool IsScopeToken(const std::string& scope)
{
	if (scope.empty())
		return false;
	for (char c : scope)
	{
		const unsigned char u = static_cast<unsigned char>(c);
		if (u < 0x21 || u > 0x7E || u == 0x22 || u == 0x5C)
			return false;
	}
	return true;
}

bool IsTenant(const std::string& tenant)
{
	for (char c : tenant)
	{
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
			c == '-' || c == '.'))
			return false;
	}
	return true;
}

// Parameters the code sets itself, which a file may not override
bool IsReservedParam(const std::string& name)
{
	static const std::set<std::string> cReserved = { "response_type", "client_id",
		"redirect_uri", "scope", "state", "code_challenge", "code_challenge_method", "login_hint",
		"nonce" };
	return cReserved.count(name) != 0;
}

// ---------------------------------------------------------------------------
// Schema of one file
// ---------------------------------------------------------------------------

const std::vector<std::string>& ProviderKeys()
{
	static const std::vector<std::string> cKeys = { "name", "enabled", "hosts", "domains",
		"authorization_endpoint", "token_endpoint", "device_authorization_endpoint",
		"revocation_endpoint", "redirect_domains", "scopes", "authorization_params", "redirect", "pkce", "sasl",
		"client_id", "client_secret", "tenant", "client_id_format", "client_check" };
	return cKeys;
}

const std::vector<std::string>& RequiredProviderKeys()
{
	static const std::vector<std::string> cKeys = { "name", "authorization_endpoint",
		"token_endpoint", "redirect", "scopes" };
	return cKeys;
}

const std::vector<std::string>& ScopeGroups()
{
	static const std::vector<std::string> cGroups = { cScopeAlways, cScopeMail, cScopeCalendar,
		cScopeContacts };
	return cGroups;
}

class FileChecker
{
public:
	FileChecker(const Source& source, std::vector<Problem>& problems)
		: mSource(source), mProblems(problems)
	{
	}

	void Run()
	{
		const Json& root = mSource.root;
		if (!root.is_object())
		{
			AtValue("", "the file must contain a JSON object");
			return;
		}
		for (auto it = root.begin(); it != root.end(); ++it)
		{
			const std::string& key = it.key();
			const std::string pointer = JsonPointerAppend("", key);
			if (key == cComment)
				Comment(pointer, it.value());
			else if (key == "version")
			{
				if (!it.value().is_number_integer() || it.value().get<long long>() != 1)
					AtValue(pointer, "\"version\" must be 1");
			}
			else if (key == "providers")
				ProvidersObject(pointer, it.value());
			else
				AtKey(pointer, "unknown key \"" + key + "\" at the top level (known keys: version, providers)");
		}
		if (!root.contains("version"))
			AtValue("", "missing \"version\" (it must be 1)");
	}

private:
	const Source& mSource;
	std::vector<Problem>& mProblems;

	void At(const std::map<std::string, size_t>& positions, const std::string& pointer,
		const std::string& message)
	{
		const auto found = positions.find(pointer);
		mProblems.push_back({ found != positions.end() ? found->second : 0, message });
	}
	void AtKey(const std::string& pointer, const std::string& message)
	{
		At(mSource.index.key_pos, pointer, message);
	}
	void AtValue(const std::string& pointer, const std::string& message)
	{
		At(mSource.index.value_pos, pointer, message);
	}

	void Comment(const std::string& pointer, const Json& value)
	{
		if (!value.is_string())
			AtValue(pointer, "\"_comment\" must be a string");
	}

	void ProvidersObject(const std::string& pointer, const Json& providers)
	{
		if (!providers.is_object())
		{
			AtValue(pointer, "\"providers\" must be an object");
			return;
		}
		for (auto it = providers.begin(); it != providers.end(); ++it)
		{
			const std::string& id = it.key();
			const std::string child = JsonPointerAppend(pointer, id);
			if (id == cComment)
			{
				Comment(child, it.value());
				continue;
			}
			if (!IsProviderId(id))
			{
				AtKey(child, "provider identifier \"" + id +
					"\" must use only a-z, 0-9 and '-' (and not start with '-')");
				continue;
			}
			if (!it.value().is_object())
			{
				AtValue(child, "provider \"" + id + "\" must be an object");
				continue;
			}
			ProviderObject(child, id, it.value());
		}
	}

	void ProviderObject(const std::string& pointer, const std::string& id, const Json& provider)
	{
		for (auto it = provider.begin(); it != provider.end(); ++it)
		{
			const std::string& key = it.key();
			const std::string child = JsonPointerAppend(pointer, key);
			const Json& value = it.value();
			const std::string what = "\"" + key + "\" of provider \"" + id + "\"";

			if (key == cComment)
				Comment(child, value);
			else if (key == "name")
				Text(child, what, value, false);
			else if (key == "enabled" || key == "pkce")
			{
				if (!value.is_boolean())
					AtValue(child, what + " must be true or false");
			}
			else if (key == "hosts" || key == "domains" || key == "redirect_domains")
				HostList(child, what, value);
			else if (key == "authorization_endpoint" || key == "token_endpoint")
				Url(child, what, value, false);
			else if (key == "device_authorization_endpoint" || key == "revocation_endpoint")
				Url(child, what, value, true);
			else if (key == "scopes")
				Scopes(child, what, value);
			else if (key == "authorization_params")
				Params(child, what, value);
			else if (key == "redirect")
			{
				if (!value.is_string() || !IsLoopbackRedirect(value.get<std::string>()))
					AtValue(child, what + " must be http://127.0.0.1, http://localhost or "
						"http://[::1], without a port, optionally followed by a path");
			}
			else if (key == "sasl")
				Sasl(child, what, value);
			else if (key == "client_id" || key == "client_secret")
				Text(child, what, value, true);
			else if (key == "tenant")
			{
				if (!value.is_string() || !IsTenant(value.get<std::string>()))
					AtValue(child, what + " must be a string of letters, digits, '-' and '.'");
			}
			else if (key == "client_id_format")
			{
				if (!value.is_string() || value.get<std::string>() != cClientIdUuid)
					AtValue(child, what + " must be \"uuid\"");
			}
			else if (key == "client_check")
			{
				if (!value.is_string() || (value.get<std::string>() != cClientCheckToken &&
					value.get<std::string>() != cClientCheckDevice))
					AtValue(child, what + " must be \"token\" or \"device_authorization\"");
			}
			else
				AtKey(child, "unknown key \"" + key + "\" in provider \"" + id + "\" (known keys: " +
					JoinNames(ProviderKeys()) + ")");
		}
	}

	void Text(const std::string& pointer, const std::string& what, const Json& value, bool emptyOk)
	{
		if (!value.is_string())
		{
			AtValue(pointer, what + " must be a string");
			return;
		}
		const std::string text = value.get<std::string>();
		if (text.empty() && !emptyOk)
			AtValue(pointer, what + " must not be empty");
		else if (HasControlCharacters(text))
			AtValue(pointer, what + " contains control characters");
		else if (HasOuterSpace(text))
			AtValue(pointer, what + " starts or ends with a space");
	}

	void Url(const std::string& pointer, const std::string& what, const Json& value, bool emptyOk)
	{
		if (!value.is_string())
		{
			AtValue(pointer, what + " must be a string");
			return;
		}
		const std::string url = value.get<std::string>();
		if (url.empty() && emptyOk)
			return;
		if (!IsHttpsUrl(url))
			AtValue(pointer, what + " must be an https:// URL" +
				std::string(emptyOk ? " (or \"\" for none)" : ""));
	}

	void HostList(const std::string& pointer, const std::string& what, const Json& value)
	{
		if (!value.is_array())
		{
			AtValue(pointer, what + " must be a list of names");
			return;
		}
		std::set<std::string> seen;
		for (size_t i = 0; i < value.size(); ++i)
		{
			const std::string child = pointer + "/" + std::to_string(i);
			if (!value[i].is_string() || !IsHostName(value[i].get<std::string>()))
				AtValue(child, what + ": each entry must be a lower-case name such as \"imap.example.com\"");
			else if (!seen.insert(value[i].get<std::string>()).second)
				AtValue(child, what + ": \"" + value[i].get<std::string>() + "\" is listed twice");
		}
	}

	void ScopeList(const std::string& pointer, const std::string& what, const Json& value)
	{
		if (!value.is_array())
		{
			AtValue(pointer, what + " must be a list of scopes");
			return;
		}
		for (size_t i = 0; i < value.size(); ++i)
		{
			if (!value[i].is_string() || !IsScopeToken(value[i].get<std::string>()))
				AtValue(pointer + "/" + std::to_string(i),
					what + ": each scope must be a non-empty string without spaces");
		}
	}

	void Scopes(const std::string& pointer, const std::string& what, const Json& value)
	{
		if (!value.is_object())
		{
			AtValue(pointer, what + " must be an object");
			return;
		}
		for (auto it = value.begin(); it != value.end(); ++it)
		{
			const std::string child = JsonPointerAppend(pointer, it.key());
			const std::vector<std::string>& groups = ScopeGroups();
			if (it.key() == cComment)
				Comment(child, it.value());
			else if (std::find(groups.begin(), groups.end(), it.key()) == groups.end())
				AtKey(child, "unknown scope group \"" + it.key() + "\" " + DescribeObject(pointer) +
					" (known groups: " + JoinNames(groups) + ")");
			else
				ScopeList(child, "\"" + it.key() + "\" " + DescribeObject(pointer), it.value());
		}
	}

	void Params(const std::string& pointer, const std::string& what, const Json& value)
	{
		if (!value.is_object())
		{
			AtValue(pointer, what + " must be an object");
			return;
		}
		for (auto it = value.begin(); it != value.end(); ++it)
		{
			const std::string child = JsonPointerAppend(pointer, it.key());
			if (it.key() == cComment)
			{
				Comment(child, it.value());
				continue;
			}
			bool nameOk = !it.key().empty();
			for (char c : it.key())
			{
				if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
					nameOk = false;
			}
			if (!nameOk)
				AtKey(child, "parameter name \"" + it.key() + "\" " + DescribeObject(pointer) +
					" must use only a-z, 0-9 and '_'");
			else if (IsReservedParam(it.key()))
				AtKey(child, "parameter \"" + it.key() + "\" " + DescribeObject(pointer) +
					" is set by the application and cannot be configured");
			else if (!it.value().is_string() || HasControlCharacters(it.value().get<std::string>()))
				AtValue(child, "parameter \"" + it.key() + "\" " + DescribeObject(pointer) +
					" must be a string");
		}
	}

	void Sasl(const std::string& pointer, const std::string& what, const Json& value)
	{
		if (!value.is_array() || value.empty())
		{
			AtValue(pointer, what + " must be a non-empty list of \"OAUTHBEARER\" and \"XOAUTH2\"");
			return;
		}
		std::set<std::string> seen;
		for (size_t i = 0; i < value.size(); ++i)
		{
			const std::string child = pointer + "/" + std::to_string(i);
			const std::string mechanism = value[i].is_string() ? value[i].get<std::string>() : "";
			if (mechanism != "OAUTHBEARER" && mechanism != "XOAUTH2")
				AtValue(child, what + ": each entry must be \"OAUTHBEARER\" or \"XOAUTH2\"");
			else if (!seen.insert(mechanism).second)
				AtValue(child, what + ": \"" + mechanism + "\" is listed twice");
		}
	}
};

// Reads, parses and checks one file's text; false with the error set
bool ParseSource(Source& source, ConfigError& error)
{
	if (source.text.size() > cMaxFileSize)
	{
		SetFileError(error, source.name, "the file is larger than 1 MiB");
		return false;
	}
	try
	{
		source.root = Json::parse(source.text);
	}
	catch (const Json::parse_error& e)
	{
		const size_t offset = std::min(e.byte > 0 ? e.byte - 1 : 0, source.text.size());
		SetError(error, source, offset, CleanParseMessage(e.what()) + ParseHint(source.text, offset));
		return false;
	}
	if (!IndexJson(source.text, source.index))
	{
		SetFileError(error, source.name, "internal error: the JSON text cannot be indexed");
		return false;
	}
	if (source.index.has_duplicate)
	{
		SetError(error, source, source.index.duplicate_pos, "duplicate key \"" +
			source.index.duplicate_key + "\" " + DescribeObject(source.index.duplicate_object));
		return false;
	}

	std::vector<Problem> problems;
	FileChecker(source, problems).Run();
	if (!problems.empty())
	{
		const auto first = std::min_element(problems.begin(), problems.end(),
			[](const Problem& a, const Problem& b) { return a.offset < b.offset; });
		std::string message = first->message;
		if (problems.size() > 1)
			message += " (and " + std::to_string(problems.size() - 1) + " more " +
				(problems.size() == 2 ? "error" : "errors") + ")";
		SetError(error, source, first->offset, message);
		return false;
	}
	return true;
}

// ---------------------------------------------------------------------------
// Merging
// ---------------------------------------------------------------------------

struct Merged
{
	std::string id;
	Json object;
	// Which source last mentioned the provider, and each key
	const Source* origin = nullptr;
	std::map<std::string, const Source*> key_origin;
};

void MergeProviders(const Source& source, std::vector<Merged>& merged)
{
	if (!source.root.contains("providers"))
		return;
	const Json& providers = source.root.at("providers");
	for (auto it = providers.begin(); it != providers.end(); ++it)
	{
		if (it.key() == cComment)
			continue;
		auto found = std::find_if(merged.begin(), merged.end(),
			[&](const Merged& m) { return m.id == it.key(); });
		if (found == merged.end())
		{
			merged.push_back(Merged());
			found = merged.end() - 1;
			found->id = it.key();
			found->object = Json::object();
		}
		found->origin = &source;
		for (auto key = it.value().begin(); key != it.value().end(); ++key)
		{
			if (key.key() == cComment)
				continue;
			found->key_origin[key.key()] = &source;
			Json& target = found->object[key.key()];
			// Objects merge key by key; everything else is replaced
			if ((key.key() == "scopes" || key.key() == "authorization_params") && target.is_object())
			{
				for (auto sub = key.value().begin(); sub != key.value().end(); ++sub)
				{
					if (sub.key() != cComment)
						target[sub.key()] = sub.value();
				}
			}
			else
			{
				target = key.value();
				if (target.is_object())
					target.erase(cComment);
			}
		}
	}
}

std::vector<std::string> StringList(const Json& value)
{
	std::vector<std::string> result;
	for (const Json& entry : value)
		result.push_back(entry.get<std::string>());
	return result;
}

Provider ToProvider(const Merged& merged)
{
	const Json& o = merged.object;
	Provider p;
	p.id = merged.id;
	p.name = o.value("name", std::string());
	p.enabled = o.value("enabled", true);
	if (o.contains("hosts"))
		p.hosts = StringList(o.at("hosts"));
	if (o.contains("domains"))
		p.domains = StringList(o.at("domains"));
	p.authorization_endpoint = o.value("authorization_endpoint", std::string());
	p.token_endpoint = o.value("token_endpoint", std::string());
	p.device_authorization_endpoint = o.value("device_authorization_endpoint", std::string());
	p.revocation_endpoint = o.value("revocation_endpoint", std::string());
	if (o.contains("redirect_domains"))
		p.redirect_domains = StringList(o.at("redirect_domains"));
	if (o.contains("scopes"))
	{
		for (auto it = o.at("scopes").begin(); it != o.at("scopes").end(); ++it)
			p.scopes[it.key()] = StringList(it.value());
	}
	if (o.contains("authorization_params"))
	{
		for (auto it = o.at("authorization_params").begin(); it != o.at("authorization_params").end(); ++it)
			p.authorization_params[it.key()] = it.value().get<std::string>();
	}
	p.redirect = o.value("redirect", std::string());
	p.pkce = o.value("pkce", true);
	if (o.contains("sasl"))
		p.sasl = StringList(o.at("sasl"));
	else
		p.sasl = { "XOAUTH2" };
	p.client_id = o.value("client_id", std::string());
	p.client_secret = o.value("client_secret", std::string());
	p.tenant = o.value("tenant", std::string());
	p.client_id_format = o.value("client_id_format", std::string());
	p.client_check = o.value("client_check", std::string(cClientCheckToken));
	return p;
}

// Position of a provider (or one of its keys) in the source that last set it
void SetMergedError(ConfigError& error, const Merged& merged, const std::string& key,
	const std::string& message)
{
	const Source* source = merged.origin;
	std::string pointer = JsonPointerAppend("/providers", merged.id);
	const auto keyOrigin = merged.key_origin.find(key);
	if (!key.empty() && keyOrigin != merged.key_origin.end())
	{
		source = keyOrigin->second;
		pointer = JsonPointerAppend(pointer, key);
	}
	const auto found = source->index.key_pos.find(pointer);
	SetError(error, *source, found != source->index.key_pos.end() ? found->second : 0, message);
}

// Checks of the merged result; false with the error set
bool CheckMerged(const std::vector<Merged>& merged, ConfigError& error)
{
	std::map<std::string, std::string> hostOwner;
	std::map<std::string, std::string> domainOwner;
	for (const Merged& m : merged)
	{
		if (!m.object.value("enabled", true))
			continue;
		for (const std::string& key : RequiredProviderKeys())
		{
			if (!m.object.contains(key))
			{
				SetMergedError(error, m, "", "provider \"" + m.id + "\" lacks the required key \"" +
					key + "\" (required: " + JoinNames(RequiredProviderKeys()) + ")");
				return false;
			}
		}
		const Provider p = ToProvider(m);
		const bool usesTenant = p.authorization_endpoint.find("{tenant}") != std::string::npos ||
			p.token_endpoint.find("{tenant}") != std::string::npos ||
			p.device_authorization_endpoint.find("{tenant}") != std::string::npos ||
			p.revocation_endpoint.find("{tenant}") != std::string::npos;
		if (usesTenant && p.tenant.empty())
		{
			SetMergedError(error, m, "", "provider \"" + m.id +
				"\" has endpoints with {tenant} but no \"tenant\"");
			return false;
		}
		if (p.client_check == cClientCheckDevice && p.device_authorization_endpoint.empty())
		{
			SetMergedError(error, m, "client_check", "provider \"" + m.id +
				"\" checks its client with \"device_authorization\" but has no "
				"\"device_authorization_endpoint\"");
			return false;
		}
		for (const std::string& host : p.hosts)
		{
			const auto owner = hostOwner.emplace(host, m.id);
			if (!owner.second)
			{
				SetMergedError(error, m, "hosts", "server \"" + host + "\" is listed by providers \"" +
					owner.first->second + "\" and \"" + m.id + "\" (disable one with \"enabled\": false)");
				return false;
			}
		}
		for (const std::string& domain : p.domains)
		{
			const auto owner = domainOwner.emplace(domain, m.id);
			if (!owner.second)
			{
				SetMergedError(error, m, "domains", "domain \"" + domain + "\" is listed by providers \"" +
					owner.first->second + "\" and \"" + m.id + "\" (disable one with \"enabled\": false)");
				return false;
			}
		}
	}
	return true;
}

std::string Lower(std::string text)
{
	for (char& c : text)
	{
		if (c >= 'A' && c <= 'Z')
			c = static_cast<char>(c - 'A' + 'a');
	}
	return text;
}

// Reads a file: false with found=false if it does not exist, false with the
// error set if it cannot be read
bool ReadFile(const std::string& path, std::string& text, bool& found, ConfigError& error)
{
	found = false;
	struct stat info;
	if (stat(path.c_str(), &info) != 0)
	{
		if (errno == ENOENT || errno == ENOTDIR)
			return false;
		SetFileError(error, path, std::strerror(errno));
		return false;
	}
	found = true;
	if (!S_ISREG(info.st_mode))
	{
		SetFileError(error, path, "not a regular file");
		return false;
	}
	if (static_cast<unsigned long long>(info.st_size) > cMaxFileSize)
	{
		SetFileError(error, path, "the file is larger than 1 MiB");
		return false;
	}
	std::FILE* file = std::fopen(path.c_str(), "rb");
	if (!file)
	{
		SetFileError(error, path, std::strerror(errno));
		return false;
	}
	text.clear();
	char buffer[8192];
	size_t count;
	while ((count = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
	{
		text.append(buffer, count);
		if (text.size() > cMaxFileSize)
			break;
	}
	const bool failed = std::ferror(file) != 0;
	std::fclose(file);
	if (failed)
	{
		SetFileError(error, path, "read error");
		return false;
	}
	return true;
}

} // namespace

std::string ConfigError::ToString() const
{
	std::string result = DisplayPath(file);
	if (line > 0)
		result += ":" + std::to_string(line) + ":" + std::to_string(column);
	return result + ": " + message;
}

std::string SystemConfigVariable(const Identity& identity)
{
	return EnvironmentPrefix(identity) + "_OAUTH2_SYSTEM_CONFIG";
}

ConfigFiles DefaultConfigFiles(const Identity& identity, const std::string& sysconfdir)
{
	ConfigFiles files;
	const char* variable = std::getenv(SystemConfigVariable(identity).c_str());
	if (variable && *variable)
	{
		files.system_file = variable;
		files.system_file_explicit = true;
	}
	else if (!sysconfdir.empty())
		files.system_file = sysconfdir + "/" + identity.id + "/" + cProviderFileName;
	const std::string user = UserConfigDirectory(identity);
	if (!user.empty())
		files.user_file = user + "/" + cProviderFileName;
	return files;
}

bool ProviderConfig::Load(const ConfigFiles& files, ConfigError& error)
{
	std::string systemText;
	std::string userText;
	bool systemFound = false;
	bool userFound = false;
	if (!files.system_file.empty() &&
		!ReadFile(files.system_file, systemText, systemFound, error) && systemFound)
		return false;
	if (!files.user_file.empty() && !ReadFile(files.user_file, userText, userFound, error) && userFound)
		return false;
	const bool ok = LoadTexts(systemFound ? files.system_file : std::string(), systemText,
		userFound ? files.user_file : std::string(), userText, error);
	mSystemFile = files.system_file;
	return ok;
}

bool ProviderConfig::LoadTexts(const std::string& system_name, const std::string& system_text,
	const std::string& user_name, const std::string& user_text, ConfigError& error)
{
	mProviders.clear();
	mSystemFound = false;
	mSystemFile = system_name;

	Source system;
	Source user;
	system.name = system_name;
	system.text = system_text;
	user.name = user_name;
	user.text = user_text;
	if (!system_name.empty() && !ParseSource(system, error))
		return false;
	if (!user_name.empty() && !ParseSource(user, error))
		return false;

	// Without the system file, OAuth2 is unavailable; the user file has
	// been checked all the same
	if (system_name.empty())
		return true;

	std::vector<Merged> merged;
	MergeProviders(system, merged);
	if (!user_name.empty())
		MergeProviders(user, merged);
	if (!CheckMerged(merged, error))
		return false;

	for (const Merged& m : merged)
	{
		Provider provider = ToProvider(m);
		if (provider.enabled)
			mProviders.push_back(provider);
	}
	mSystemFound = true;
	return true;
}

const Provider* ProviderConfig::Find(const std::string& id) const
{
	for (const Provider& provider : mProviders)
	{
		if (provider.id == id)
			return &provider;
	}
	return nullptr;
}

const Provider* ProviderConfig::FindByHost(const std::string& host) const
{
	std::string name = Lower(host);
	while (!name.empty() && name.back() == '.')
		name.pop_back();
	for (const Provider& provider : mProviders)
	{
		if (std::find(provider.hosts.begin(), provider.hosts.end(), name) != provider.hosts.end())
			return &provider;
	}
	return nullptr;
}

const Provider* ProviderConfig::FindByEmail(const std::string& address) const
{
	const size_t at = address.rfind('@');
	if (at == std::string::npos || at + 1 >= address.size())
		return nullptr;
	std::string domain = Lower(address.substr(at + 1));
	while (!domain.empty() && domain.back() == '.')
		domain.pop_back();
	for (const Provider& provider : mProviders)
	{
		if (std::find(provider.domains.begin(), provider.domains.end(), domain) != provider.domains.end())
			return &provider;
	}
	return nullptr;
}

} // namespace oauth2
