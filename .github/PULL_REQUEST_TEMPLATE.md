## What changed

<!-- One paragraph. Link the issue (Fixes #123). -->

## Why

## How it was tested

- [ ] `make test` passes (VM, bytecode, embedded executables, native)
- [ ] Sanitized build passes (`make CXXFLAGS="-O1 -g -fsanitize=address,undefined" && make test`) — required for runtime, VM or native-backend changes
- [ ] New behaviour has a test (`tests/features_*.tq` + `.expected`, `tests/smoke.sh`, or `tests/unit_tests.tq`)
- [ ] Documentation updated (`LANGUAGE.md` / `STDLIB.md` / `docs/*.md`) and `CHANGELOG.md` has an entry
- [ ] `src/embedded_runtime.inc` regenerated if `src/tacqor.hpp` or `src/values.hpp` changed

## Before / after

<!-- A program whose behaviour changed, with its output before and after, when applicable. -->
