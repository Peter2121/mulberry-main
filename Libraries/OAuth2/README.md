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
