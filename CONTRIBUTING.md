# Contributing

Thanks for your interest. This project is a safety-adjacent hobby firmware (see the disclaimer in [README.md](README.md)), so changes to alert behavior need extra care.

## Dev setup

1. Install [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/) (CI uses `platformio==6.2.0`).
2. Clone the repo, then `cp include/secrets.example.h include/secrets.h` (the file is gitignored; never commit it).

## Tests

```
pio test -e native      # logic tests on the host (Unity), no board needed
pio run -e esp32dev     # firmware build
```

Behavior that touches real hardware or the network is covered by the checklist in [docs/manual-tests.md](docs/manual-tests.md) (in Italian).

## Style

- Pure logic lives in `lib/alert_core` and must not include Arduino or ESP-IDF headers, so it stays testable on the host. Hardware, network and NVS code stay in `src/`.
- Write the test first for logic changes (TDD): add a failing Unity test under `test/`, then the code.
- C++17, 2-space indentation (see `.editorconfig`), no new compiler warnings (`-Wall -Wextra`).
- Pin-related changes go in `include/config.h` only.
- Update both README.md and README.it.md when behavior changes.

## Commits and PRs

Short imperative subject, optionally prefixed (`fix:`, `feat:`, `docs:`, `build:`, `test:`). Open a PR against `main` and fill in the template.
