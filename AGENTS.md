# AGENTS.md

## Purpose
- This file guides agentic coding assistants working in this repository.
- Follow these commands and style conventions unless the user overrides them.

## Repository Overview
- C++20 header-only actor framework.
- Public headers live in `include/agner`.
- Tests live in `test` and use GoogleTest.
- Build system uses CMake with presets.

## Build Commands
- Configure default build:
  - `cmake --preset default`
- Build everything with the default preset:
  - `cmake --build --preset default`
- Build only the test binary:
  - `cmake --build --preset default --target agner_tests`
- The default build output directory:
  - `artifacts/default/build`
- Install prefix (if needed):
  - `artifacts/default/install`

## Test Commands
- Run all tests (after building):
  - `ctest --test-dir artifacts/default/build --output-on-failure`
- Run all tests via gtest directly:
  - `artifacts/default/build/agner_tests`

### Run a Single Test
- Using gtest filtering (fastest, no CTest discovery needed):
  - `artifacts/default/build/agner_tests --gtest_filter=Actor.SpawnAndReceive`
- Using CTest regex (matches CTest-discovered names):
  - `ctest --test-dir artifacts/default/build -R Actor.SpawnAndReceive`

## Coverage (MCDC)
- Coverage preset uses Clang and MCDC instrumentation.
- Run coverage helper script (preferred workflow):
  - `scripts/coverage.sh`
- Maintain 100% line, and decision coverage for changes.
- Coverage output directory:
  - `artifacts/default/coverage`

## Lint / Format
- No lint or formatting tools are configured in the repo.
- Do not add new tooling unless requested.
- If formatting changes are needed, follow existing style by hand.

## Code Style Guidelines

### Formatting
- Indentation uses two spaces; no tabs.
- Opening braces stay on the same line.
- Keep blank lines between logical blocks.
- Prefer one statement per line.
- Wrap long lines where it improves readability.

### Includes
- Use `#pragma once` in headers.
- Standard library includes first.
- Leave a blank line between standard and project headers.
- Project headers use quotes and relative paths.

### Naming
- Namespaces use lowercase (e.g., `agner`).
- Types use PascalCase (e.g., `DeterministicScheduler`).
- Functions and methods use snake_case (e.g., `schedule_after`).
- Variables use snake_case.
- Member variables use trailing underscore (e.g., `ready_`).
- Template parameters use PascalCase or descriptive names (e.g., `SchedulerType`).

### Types and APIs
- Use `using` for aliases and type traits.
- Use `std::optional` for optional values.
- Use `std::variant` for message unions.
- Prefer explicit types for public APIs; `auto` for locals when clear.
- Mark functions `noexcept` when they cannot throw.

### Error Handling
- Use exceptions for exceptional flow in coroutine tasks.
- Preserve existing error patterns (`ExitReason`, `ExitSignal`, `DownSignal`).
- Use `static_assert` for template constraints.
- Use `assert` for internal invariants when appropriate.

### Coroutines and Scheduling
- Coroutines return `task<T>` or `task<void>`.
- Use `co_await` / `co_return` for coroutine flow.
- Use scheduler `schedule` and `schedule_after` consistently.
- Avoid blocking in deterministic scheduler tests.

### Tests
- Tests use GoogleTest (`TEST(Suite, Name)` style).
- Keep test data in an anonymous namespace when local.
- Prefer literal chrono durations (`1ms`) with `std::chrono_literals`.
- Tests include Summary and Description comments:
  - Summary: one-line description of behavior.
  - Description: short paragraph describing setup and assertion.
- Assertions use `EXPECT_*` or `ASSERT_*` appropriately.

### File Organization
- Public API headers stay under `include/agner`.
- Test helpers live in `test` alongside tests.
- Keep deterministic scheduler in `test/deterministic_scheduler.hpp`.

## Cursor / Copilot Rules
- No Cursor rules found (`.cursor/rules/` or `.cursorrules`).
- No Copilot instructions found (`.github/copilot-instructions.md`).

## Notes for Agents
- Keep changes minimal and focused.
- Update tests if behavior changes.
- Do not introduce new third-party dependencies without approval.
