# OAuth2 core (libOAuth2)

The OAuth2 sign-in engine of Mulberry, built as a static library with its
test tool. The design and the task list are in `OAUTH2-PLAN.md` at the top
of the repository.

## Rules

- **No dependency on JX or on the rest of Mulberry.** The library is
  compiled without Mulberry's prefix header. It uses libcurl (HTTP), and from
  task 1.2 on libsodium and nlohmann/json (vendored).
- **No product name.** The host gives its identity (`oauth2::Identity`:
  identifier, display name, version). The identifier is set once, by
  `configure --with-app-id=ID` (default `mulberry`), so that a fork under
  another name changes one build setting.
- **Namespace `oauth2`**; public headers in `include/oauth2/`.

## Layout

| Path | Contents |
|---|---|
| `include/oauth2/` | public headers |
| `src/` | library sources (and private headers) |
| `thirdparty/nlohmann/` | vendored nlohmann/json 3.12.0, MIT (`thirdparty/README.md`) |
| `tools/oauth2-tool.cpp` | command-line test tool |
| `tests/unit.cpp` | offline unit tests (`oauth2-unit`) |
| `tests/token-*` | token client tests: mock token endpoint (`token-server.py`), driver (`oauth2-token-driver`), script (`token-tests.sh`) |

## Interface so far

| Header | Contents |
|---|---|
| `Identity.h` | `oauth2::Identity`, `IsValidIdentityId()` |
| `Version.h` | library and libcurl versions |
| `Encoding.h` | base64url without padding; percent and form encoding and decoding; `BuildQuery`, `BuildForm`, `ParseQuery` |
| `Random.h` | `RandomBytes()` (libsodium), `MakeState()`, `ConstantTimeEquals()` |
| `Pkce.h` | `MakePkce()`, `PkceChallengeS256()` (RFC 7636, S256 only), `IsValidPkceVerifier()` |
| `Redact.h` | `Redact()` ("<redacted N bytes>"), `RedactParams()` for query strings and form bodies |
| `Paths.h` | home folder, `~/.config/<id>` (XDG), environment prefix, `DisplayPath()` |
| `Permissions.h` | `CheckPermissions()` and `FixPermissions()` of the user and system files |
| `Provider.h` | `oauth2::Provider`, `AccountSettings`, `WithAccountSettings()` |
| `ProviderConfig.h` | loading, strict checks and merging of the provider files; lookup by id, server name and e-mail domain |
| `Http.h` | `HttpSettings`: user agent, time and size limits, log callback, cancellation |
| `TokenClient.h` | `ExchangeCode()`, `RefreshToken()`, `RequestToken()`; `TokenResponse`, `TokenError`; `Tokens` and `UpdateTokens()` (refresh-token rotation) |

## Provider configuration files

Two files, the second overriding the first (`OAUTH2-PLAN.md`, section 7):

1. the system file, `${sysconfdir}/<id>/oauth2-providers.conf.json`
   (`/usr/local/etc/mulberry/…` with the default prefix), installed by
   `gmake install` from `data/oauth2-providers.conf.json`. In the build tree,
   name it with `$MULBERRY_OAUTH2_SYSTEM_CONFIG` or
   `oauth2-tool --system-config`;
2. the user file, `~/.config/<id>/oauth2-providers.conf.json`
   (`$XDG_CONFIG_HOME/<id>/…` when that variable is set).

Then each account's own client ID, secret and tenant override both
(`WithAccountSettings()`).

### Format

```json
{
  "version": 1,
  "providers": {
    "<id>": { "<key>": <value>, ... }
  }
}
```

- `version` (required): `1`.
- Provider identifiers: `a-z`, `0-9` and `-`, not starting with `-`.
- `"_comment"`: a string, accepted and ignored in any object.

### Provider keys

| Key | Type | Required | Meaning |
|---|---|---|---|
| `name` | string | yes | name shown to the user |
| `enabled` | boolean | no (`true`) | `false` hides the provider (also one of the system file) |
| `hosts` | list of names | no | server names (IMAP, SMTP, POP, CalDAV...) that select the provider; lower case |
| `domains` | list of names | no | e-mail domains that select the provider |
| `authorization_endpoint` | https URL | yes | |
| `token_endpoint` | https URL | yes | |
| `device_authorization_endpoint` | https URL or `""` | no | device code (RFC 8628); absent or `""`: not offered |
| `revocation_endpoint` | https URL or `""` | no | RFC 7009 |
| `redirect_domains` | list of names | no (`[]`) | domains, with their subdomains, that a 307 or 308 redirect of a token request may lead to, besides the endpoint's own host; `[]`: the same host only |
| `scopes` | object | yes | scope lists per group: `always` (added to every request), `mail`, `calendar`, `contacts` |
| `authorization_params` | object of strings | no | extra parameters of the authorization request (`"access_type": "offline"`); the ones the application sets (`client_id`, `scope`, `state`...) are refused |
| `redirect` | string | yes | `http://127.0.0.1`, `http://localhost` or `http://[::1]`, no port (added at run time), optional path |
| `pkce` | boolean | no (`true`) | send a PKCE S256 challenge |
| `sasl` | list | no (`["XOAUTH2"]`) | `"OAUTHBEARER"` and `"XOAUTH2"` in order of preference |
| `client_id` | string | no | |
| `client_secret` | string | no | |
| `tenant` | string | no | replaces `{tenant}` in the endpoints (Microsoft: `common`, `organizations`, `consumers`, a tenant ID or domain); required when an endpoint contains `{tenant}` |

### Merging and checks

- Each key of the user file replaces the system file's value; `scopes` and
  `authorization_params` merge key by key; lists are replaced whole. A
  provider only in the user file must have all the required keys.
- Strict: no comments, trailing commas or single quotes; duplicate keys,
  unknown keys and wrong types are errors. The first error in reading order
  is reported as `file:line:column: message`, then the application exits.
- A server name or domain listed by two enabled providers is an error.
- A missing user file is normal. Without the system file OAuth2 is
  unavailable, but the user file is still checked.
- Permissions are checked before reading, with warnings only: the user file
  must be `0600` (or `0400`), owned by the user, in a folder only the user
  can write, and not a symbolic link out of the home folder; the system file
  must belong to root and be writable by nobody else.

## Token client

`ExchangeCode()` and `RefreshToken()` POST a form to the token endpoint
(`TokenEndpointOf(provider)`), with `client_id`, and `client_secret` when
there is one. They block; call them off the UI thread.

- **HTTP:** HTTPS only, certificate and host name verified, no cookies, no
  `.netrc`, `Accept: application/json`, neutral `User-Agent`. Limits: 15 s
  to connect, 30 s in all, 1 MiB of answer (`HttpSettings`).
- **Redirects** (a precaution: no provider is known to redirect its token
  endpoint): a redirect carries the form, and so the secrets, to its
  target. Only 307 and 308 are followed, since 301, 302 and 303 would turn
  the request into a GET without the form; the target must be https, on the
  endpoint's own host or in the provider's `redirect_domains`; at most 3
  redirects. Each one is checked before anything is sent to it, and logged.
- **Answer:** `access_token` (required), `token_type` (Bearer only; absent
  means Bearer), `expires_in` (a number, or a number in a string),
  `refresh_token`, `scope`, `id_token`, and Google's
  `refresh_token_expires_in`. A failed parse returns nothing.
- **Errors** (`TokenError`): `kind` says what failed (configuration,
  network, timeout, cancelled, HTTP status, malformed answer, OAuth2 error).
  For OAuth2 errors: `error`, the first line of `error_description`,
  `error_uri`, Google's `error_subtype` or Microsoft's `suberror`, the
  `AADSTS` code (from `error_codes`, else from the description), Microsoft's
  `correlation_id`. `RequiresSignIn()` is `invalid_grant`;
  `IsClientProblem()` is `invalid_client` or `unauthorized_client`. The
  message never quotes the answer body, so no token can leak through it.
- **Rotation:** `UpdateTokens()` keeps the old refresh token when the answer
  has none, and replaces it when the answer has a new one; it returns
  `true` when the new token must be saved.
- **Log:** the `HttpSettings::log` callback gets `curl -v` style lines, with
  form values, JSON values, `Authorization` and cookie headers redacted.
  An answer that is not JSON is logged as its size only.

## oauth2-tool

```sh
oauth2-tool [options] providers        # check the files and list the providers
oauth2-tool [options] lookup NAME      # provider of a server name or e-mail address
oauth2-tool --version
```

Options: `--system-config FILE`, `--user-config FILE`, `--no-user-config`,
`--fix-permissions`. Exit status: 0 success, 1 nothing found (or no system
file), 2 usage or configuration error. Client secrets are shown as
`<redacted N bytes>`.

## Building and testing

The library and `oauth2-tool` are built with the rest of Mulberry (from
`Libraries/Makefile`), with or without the HTML viewer. Alone, after
`configure`:

```sh
gmake -C Libraries/OAuth2          # libOAuth2.a and oauth2-tool
gmake -C Libraries/OAuth2 check    # unit and mock-server tests, no network, no account
Libraries/OAuth2/oauth2-tool --version
```

`check` needs Python 3 for the mock token endpoint (127.0.0.1 only) and
`openssl` for its TLS case (skipped without it); the details of the token
tests are in `obj/tests/token/`. `oauth2-tool` and the test programs stay in
the build tree; they are not installed (decision D7 of the plan).
