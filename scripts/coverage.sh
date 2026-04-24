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
LLVM_PROFILE_FILE="${coverage_dir}/agner-%p.profraw" ctest --preset "${preset}"

llvm-profdata merge -sparse "${coverage_dir}"/*.profraw \
  -o "${coverage_dir}/coverage.profdata"

llvm-cov report "${build_dir}/agner_tests" \
  -instr-profile="${coverage_dir}/coverage.profdata" \
  -ignore-filename-regex='(.*/artifacts/.*|.*/test/.*)' \
  -show-branch-summary \
  -show-mcdc-summary \
  > "${coverage_dir}/coverage.txt"

llvm-cov show "${build_dir}/agner_tests" \
  -instr-profile="${coverage_dir}/coverage.profdata" \
  -ignore-filename-regex='(.*/artifacts/.*|.*/test/.*)' \
  -show-branches=count \
  -show-mcdc \
  -format=html \
  -output-dir="${coverage_dir}/html"

cat "${coverage_dir}/coverage.txt"
