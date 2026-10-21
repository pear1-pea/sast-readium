# Testing

Tests live in `tests/` and are built by the top-level CMake target.

## Layout

- `tests/unit/` — unit tests, one file per component (e.g. `test_zoom_controller.cpp`,
  `test_rag_p0.cpp` …)
- `tests/integration/` — integration tests (e.g. `test_pdf_viewer.cpp`)
- `tests/smoke_test.cpp` — smoke test

## Running

```bash
ctest --test-dir build --output-on-failure
```

Run a single test binary directly while iterating, for example:

```bash
./build/tests/unit/test_pdf_continuous_blueprint
```

## Rules

- When changing code under `app/` or `tests/`, add or update tests for the behavior change
  in the same change when practical.
- Do not land source changes without running the appropriate validation: configure, build,
  `ctest`, and `pre-commit run --all-files`.

## Related docs

- [Build](build.md) — presets and build commands
- [Conventions](conventions.md) — code and logging conventions
