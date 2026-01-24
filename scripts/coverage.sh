#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
preset="default"
build_dir="${repo_root}/artifacts/${preset}/build"
coverage_dir="${repo_root}/artifacts/${preset}/coverage"

rm -rf "${coverage_dir}"
mkdir -p "${coverage_dir}"

cmake --preset "${preset}"
cmake --build --preset "${preset}" --target agner_tests
ctest --preset "${preset}"

gcovr \
  --root "${repo_root}" \
  --gcov-executable "llvm-cov gcov" \
  --delete \
  --decisions \
  --merge-lines \
  --exclude-function-lines \
  --exclude-throw-branches \
  --exclude-unreachable-branches \
  --exclude ".*artifacts/.*" \
  --exclude ".*test/.*" \
  --print-summary \
  --txt "${coverage_dir}/coverage.txt" \
  --html-details "${coverage_dir}/index.html"
