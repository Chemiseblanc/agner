#!/usr/bin/env bash
set -euo pipefail

preset="default"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${repo_root}/artifacts/${preset}/build"
coverage_dir="${repo_root}/artifacts/${preset}/coverage"
profiles_dir="${coverage_dir}/profiles"
profdata="${coverage_dir}/coverage.profdata"

cmake --preset "${preset}"
cmake --build --preset "${preset}" --target agner_tests

rm -rf "${coverage_dir}"
mkdir -p "${profiles_dir}"

LLVM_PROFILE_FILE="${profiles_dir}/%p.profraw" ctest --preset "${preset}"

llvm-profdata merge -sparse "${profiles_dir}"/*.profraw -o "${profdata}"

llvm-cov report "${build_dir}/agner_tests" \
  -instr-profile="${profdata}" \
  --show-mcdc-summary \
  --ignore-filename-regex=.*/test/.*

llvm-cov show "${build_dir}/agner_tests" \
  -instr-profile="${profdata}" \
  --show-mcdc \
  --ignore-filename-regex=.*/test/.* \
  --format=html \
  -output-dir="${coverage_dir}"
