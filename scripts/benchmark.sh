#!/usr/bin/env bash
set -euo pipefail

preset="release"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${repo_root}/artifacts/${preset}/build"
results="${build_dir}/benchmark_results.json"

cmake --preset "${preset}"
cmake --build --preset "${preset}" --target agner_benchmarks

"${build_dir}/agner_benchmarks" \
  --benchmark_out="${results}" \
  --benchmark_out_format=json
