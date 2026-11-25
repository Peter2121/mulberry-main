# Third-party code of the OAuth2 core

## nlohmann/json 3.12.0

- **File:** `nlohmann/json.hpp`, the single-header form, unchanged.
- **Licence:** MIT, `nlohmann/LICENSE.MIT` (installed with the other
  licences, task 9.2 of `OAUTH2-PLAN.md`).
- **Source:** release v3.12.0 of <https://github.com/nlohmann/json>
  (2025-04-11): the release asset
  <https://github.com/nlohmann/json/releases/download/v3.12.0/json.hpp>,
  identical to `single_include/nlohmann/json.hpp` at tag `v3.12.0`.
- **SHA-256:**
  - `json.hpp`: `aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63`
  - `LICENSE.MIT`: `46a65cffd1ea955132d95a8dd921640714a8d6b537d2e4e482d31145ae95b603`

Rules (decision D1 of the plan):

- included only by the `.cpp` files that parse or write JSON, never by a
  public header in `include/oauth2/`, to limit compile time and keep the
  library's interface free of it;
- its exceptions are caught inside the library;
- compiled with `JSON_USE_IMPLICIT_CONVERSIONS=0` (see `../Makefile.in`):
  values are read with explicit `get<T>()`, so a wrong type in a file is an
  error, not a silent conversion.

To update: replace both files from a newer release, check that the release
asset and the tag's `single_include` file are identical, update the version
and the checksums here, and run `gmake check`.
