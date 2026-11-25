/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

// Redaction of secrets in log lines (OAUTH2-PLAN.md, section 3: "Tokens
// never appear in logs").  Tokens, authorization codes, PKCE verifiers and
// client secrets are replaced by "<redacted N bytes>".

#ifndef OAUTH2_REDACT_H
#define OAUTH2_REDACT_H

#include <string>
#include <vector>

namespace oauth2
{

// "<redacted N bytes>" ("<redacted 1 byte>"); the secret's length only
std::string Redact(const std::string& secret);

// The field names whose values are secrets in OAuth2 requests, redirects and
// responses: access_token, refresh_token, id_token, code, code_verifier,
// client_secret, device_code, assertion, password
const std::vector<std::string>& SecretFieldNames();

// Copy of a query string or form body ("a=1&code=xyz") with the values of
// the named fields replaced by Redact() of the value as it appears (still
// encoded).  Names are compared after percent-decoding, so "c%6Fde" is
// caught too.  Everything else is kept byte for byte.
std::string RedactParams(const std::string& query,
	const std::vector<std::string>& names = SecretFieldNames());

} // namespace oauth2

#endif
