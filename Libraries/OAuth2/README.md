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
| `src/` | library sources |
| `tools/oauth2-tool.cpp` | command-line test tool |
| `tests/unit.cpp` | offline unit tests (`oauth2-unit`) |

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
