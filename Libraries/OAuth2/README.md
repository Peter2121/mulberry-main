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
gmake -C Libraries/OAuth2 check    # unit tests, no network, no account
Libraries/OAuth2/oauth2-tool --version
```

`oauth2-tool` stays in the build tree; it is not installed (decision D7 of
the plan).
