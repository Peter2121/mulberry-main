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
| `tests/listener-*` | loopback listener tests: driver (`oauth2-listener-driver`), script with curl as the browser (`listener-tests.sh`) |
| `tests/login-tests.sh`, `tests/fake-browser.py` | `oauth2-tool login` and `refresh` against the mock token endpoint over TLS, with a fake xdg-open and browser |
| `tests/google-error-decode.sh` | live-test helper, not run by `make check`: decodes the reason (`authError`) in the address of Google's sign-in error page |

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
| `Provider.h` | `oauth2::Provider`, `AccountSettings`, `WithAccountSettings()`, `CheckClientIdFormat()` |
| `ProviderConfig.h` | loading, strict checks and merging of the provider files; lookup by id, server name and e-mail domain |
| `Http.h` | `HttpSettings`: user agent, time and size limits, certificate authorities, log callback, cancellation |
| `Listener.h` | `LoopbackListener` (the browser's redirect), `InterpretRedirect()`, `IsLoopbackRedirect()` |
| `TokenClient.h` | `ExchangeCode()`, `RefreshToken()`, `RequestToken()`, `ProbeClient()`; `TokenResponse`, `TokenError`; `Tokens` and `UpdateTokens()` (refresh-token rotation) |
| `Authorization.h` | `BuildAuthorizationUrl()`; `MissingScopes()` and `RefreshScopes()` (what the user did not grant) |
| `Browser.h` | `OpenBrowser()`: xdg-open, then `$BROWSER`, without a shell |
| `IdToken.h` | `ParseIdToken()`: the claims of the ID token (e-mail address, audience), signature not checked |

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
| `authorization_params` | object of strings | no | extra parameters of the authorization request (Google: `"access_type": "offline"` and `"prompt": "consent"`, without which Google gives no new refresh token to a client the user already allowed); the ones the application sets (`client_id`, `scope`, `state`, `login_hint`...) are refused |
| `redirect` | string | yes | `http://127.0.0.1`, `http://localhost` or `http://[::1]`, no port (added at run time), optional path |
| `pkce` | boolean | no (`true`) | send a PKCE S256 challenge |
| `sasl` | list | no (`["XOAUTH2"]`) | `"OAUTHBEARER"` and `"XOAUTH2"` in order of preference |
| `client_id` | string | no | |
| `client_secret` | string | no | |
| `tenant` | string | no | replaces `{tenant}` in the endpoints (Microsoft: `common`, `organizations`, `consumers`, a tenant ID or domain); required when an endpoint contains `{tenant}` |
| `client_id_format` | `"uuid"` | no | form of every client ID of the provider, checked offline before a sign-in (Microsoft: `xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx`); absent: any |
| `client_check` | `"token"` or `"device_authorization"` | no (`"token"`) | how a wrong client ID is detected before the browser opens: an exchange with an invalid code, or a device code request (Microsoft, whose token endpoint checks the code before the client); `"device_authorization"` needs `device_authorization_endpoint` |

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

## Loopback listener

`LoopbackListener::Open(provider.redirect)` listens on an ephemeral port:
on 127.0.0.1 for `http://127.0.0.1`, on [::1] for `http://[::1]`, and on
both with the same port for `http://localhost` (127.0.0.1 only if the
machine has no IPv6 loopback). `RedirectUri()` (`http://localhost:PORT/cb`)
then goes into the authorization request and the code exchange.

`Wait(state, settings)` serves HTTP/1.1 requests until the redirect:

- **The redirect:** `GET` on the redirect's path (`/` without one), with
  exactly one `Host` header naming the redirect's host and port, and
  the expected `state` (compared in constant time) with a `code` or an
  `error`. `code`, `state`, `error`, `error_description` and `error_uri`
  may appear only once. It gets a short page with the application name, then
  every socket is closed.
- **Anything else** gets 400, 404, 405 or 431, and the listener keeps
  waiting: the favicon, another path or method, a wrong or missing state, a
  foreign `Host` (DNS rebinding), a request over 8 KiB.
- **Limits:** 5 minutes in all (`ListenerSettings::timeout`), cancellation
  from another thread, 10 s for a connection to send its request, 16
  connections at once (an idle one does not block the others).
- **Pages:** `Cache-Control: no-store`, `Referrer-Policy: no-referrer`, a
  `Content-Security-Policy` that allows no loading, nothing but escaped text.
- **Log:** request lines with `code` and `state` redacted, and why a request
  was refused.

The sockets are close-on-exec, so the browser started by the application
does not inherit them.

## Browser sign-in

The pieces of the sign-in, which `oauth2-tool login` puts together (and
Mulberry later, task 3.4):

1. `LoopbackListener::Open(provider.redirect)`; keep `RedirectUri()`.
2. `CheckClientIdFormat()` (offline), then `ProbeClient()`, so that a wrong
   client ID or secret is reported before the user goes to the browser.
   There, a wrong client ID only gives the provider's error page, which
   never comes back to the listener. By the provider's `client_check`:
   an exchange with an invalid code (`invalid_client`,
   `unauthorized_client` for a wrong client), or a device code request
   whose code is not used (Microsoft: AADSTS50059 or AADSTS700016 for an
   unknown client ID; the secret is not checked).
3. `MakePkce()`, `MakeState()`, then `BuildAuthorizationUrl()`: endpoint
   with `response_type=code`, `client_id`, `redirect_uri`, `scope`,
   `state`, the S256 challenge, `login_hint`, then the provider's
   `authorization_params` (which `AuthorizationRequest::overrides` can
   change or remove).
4. `OpenBrowser(url)`: `xdg-open URL`, then each entry of `$BROWSER`
   (`:`-separated, `%s` for the URL), started with `posix_spawnp()`, in
   their own process group (Ctrl-C does not close the browser). Only
   `http`/`https` URLs without spaces are opened. Show the URL in any case,
   for a browser on another display.
5. `Wait(state)`, then `ExchangeCode(code, verifier, redirect_uri)`.
6. `MissingScopes()`: the scopes the user unticked on the consent screen;
   `ParseIdToken()`: who signed in. The ID token's signature is not
   checked: it comes from the token endpoint over TLS (OpenID Connect Core,
   section 3.1.3.7).
7. A refresh asks for `RefreshScopes()`: the scopes of the sign-in minus the
   refused ones (Google answers `invalid_scope` otherwise).

## oauth2-tool

```sh
oauth2-tool [options] providers        # check the files and list the providers
oauth2-tool [options] lookup NAME      # provider of a server name or e-mail address
oauth2-tool [options] login ACCOUNT    # sign in in the browser, keep the refresh token
oauth2-tool [options] refresh ACCOUNT  # new access token from the kept refresh token
oauth2-tool --version
```

Options: `--system-config FILE`, `--user-config FILE`, `--no-user-config`,
`--fix-permissions`. Exit status: 0 success, 1 nothing found, no system
file, or the sign-in failed; 2 usage or configuration error. Client
secrets are shown as `<redacted N bytes>`.

`login` and `refresh` find the provider from the account's domain, or
`--provider ID` (a Google Workspace or Microsoft 365 domain). Options:
`--client-id`, `--tenant` (instead of the configured ones), `--services
mail,calendar,contacts` (default: every group of the provider),
`--prompt VALUE` (`""` sends none), `--no-browser`, `--no-client-check`
(skips both checks of step 2 of "Browser sign-in"),
`--timeout S` (wait for the browser, default 300), `--token-file FILE`,
`--ca-file FILE` (tests), `--verbose` (log on stderr, secrets redacted),
`--show-token`. Ctrl-C cancels.

`login` prints the token type, expiry, requested, granted and refused
scopes, and the address of the ID token. The refresh token goes to
`~/.config/<app>/oauth2-tool/PROVIDER-ACCOUNT.json` (folder 0700, file
0600, written through a temporary file), for this tool only: Mulberry keeps
its tokens in the keyring. `refresh` refuses a file issued to another client
ID, saves a rotated token at once, and checks the file's permissions like the
provider file's.

## Building and testing

The library and `oauth2-tool` are built with the rest of Mulberry (from
`Libraries/Makefile`), with or without the HTML viewer. Alone, after
`configure`:

```sh
gmake -C Libraries/OAuth2          # libOAuth2.a and oauth2-tool
gmake -C Libraries/OAuth2 check    # unit and mock-server tests, no network, no account
Libraries/OAuth2/oauth2-tool --version
```

`check` needs Python 3 for the mock token endpoint (127.0.0.1 only),
`openssl` for its TLS cases (skipped without it: the TLS case of the token
tests, and the login tests), and curl for the listener tests; their details
are in `obj/tests/token/`, `obj/tests/listener/` and `obj/tests/login/`. `oauth2-tool` and the test programs stay in
the build tree; they are not installed (decision D7 of the plan).
