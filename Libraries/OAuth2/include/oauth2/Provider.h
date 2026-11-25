/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

// An OAuth2 provider, as described by the provider configuration files
// (OAUTH2-PLAN.md, section 7; the keys are listed in README.md)

#ifndef OAUTH2_PROVIDER_H
#define OAUTH2_PROVIDER_H

#include "oauth2/Encoding.h"

#include <map>
#include <string>
#include <vector>

namespace oauth2
{

// Scope groups of the "scopes" object.  "always" is added to every request
// (refresh tokens, identity); the others per service.
extern const char* const cScopeAlways;		// "always"
extern const char* const cScopeMail;		// "mail"
extern const char* const cScopeCalendar;	// "calendar"
extern const char* const cScopeContacts;	// "contacts"

struct Provider
{
	std::string id;			// "google": the key in the "providers" object
	std::string name;		// "Google"
	bool enabled = true;

	// Server names (IMAP, SMTP, POP, CalDAV...) and e-mail domains that
	// select this provider automatically; lower case
	std::vector<std::string> hosts;
	std::vector<std::string> domains;

	// https:// URLs; "{tenant}" is replaced by the tenant (Microsoft).
	// device_authorization_endpoint and revocation_endpoint may be empty.
	std::string authorization_endpoint;
	std::string token_endpoint;
	std::string device_authorization_endpoint;
	std::string revocation_endpoint;

	// Scope group -> scopes
	std::map<std::string, std::vector<std::string>> scopes;

	// Extra parameters of the authorization request ("access_type":
	// "offline" for Google)
	std::map<std::string, std::string> authorization_params;

	// Loopback redirect without port ("http://127.0.0.1"); the listener's
	// port is added at run time
	std::string redirect;
	bool pkce = true;

	// SASL mechanisms in order of preference ("OAUTHBEARER", "XOAUTH2")
	std::vector<std::string> sasl;

	std::string client_id;
	std::string client_secret;
	std::string tenant;

	bool SupportsDeviceCode() const { return !device_authorization_endpoint.empty(); }

	// The endpoint with "{tenant}" replaced
	std::string ResolveEndpoint(const std::string& endpoint) const;

	// The "always" scopes followed by those of each group asked for, without
	// repetition, in order
	std::vector<std::string> ScopesFor(const std::vector<std::string>& groups) const;
};

// Level 3 of the configuration: the account's own settings.  Empty fields
// keep the provider's value.
struct AccountSettings
{
	std::string client_id;
	std::string client_secret;
	std::string tenant;
};

Provider WithAccountSettings(const Provider& provider, const AccountSettings& settings);

} // namespace oauth2

#endif
