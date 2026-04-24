#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
preset="default"
build_dir="${repo_root}/artifacts/${preset}/build"
coverage_dir="${repo_root}/artifacts/${preset}/coverage"

rm -rf "${coverage_dir}"
mkdir -p "${coverage_dir}"
if [[ -d "${build_dir}" ]]; then
  find "${build_dir}" -name '*.gcda' -delete
fi

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

llvm-cov export "${build_dir}/agner_tests" \
  -instr-profile="${coverage_dir}/coverage.profdata" \
  -ignore-filename-regex='(.*/artifacts/.*|.*/test/.*)' \
  --unify-instantiations \
  > "${coverage_dir}/coverage.json"

"${repo_root}/scripts/coverage_logical.py" "${coverage_dir}/coverage.json" \
  > "${coverage_dir}/coverage-logical.txt"

llvm-cov show "${build_dir}/agner_tests" \
  -instr-profile="${coverage_dir}/coverage.profdata" \
  -ignore-filename-regex='(.*/artifacts/.*|.*/test/.*)' \
  -show-branches=count \
  -show-mcdc \
  -format=html \
  -output-dir="${coverage_dir}/html"

cat "${coverage_dir}/coverage.txt"
printf '\nLogical coverage with duplicate template instantiations collapsed:\n'
cat "${coverage_dir}/coverage-logical.txt"
