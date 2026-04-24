#!/usr/bin/env python3
"""Report coverage after collapsing duplicate template instantiations.

LLVM source-based coverage reports every instrumented template instantiation.
That is useful for debugging, but it makes header-only libraries look uncovered
when one instantiation executes a source branch and another valid instantiation
does not. This script reports a source-location view: a line or branch is
covered when any instantiation at the same source location covers it.
"""

from __future__ import annotations

import json
import sys
from dataclasses import dataclass
from pathlib import Path


@dataclass
class Totals:
  lines: int = 0
  covered_lines: int = 0
  branches: int = 0
  covered_branches: int = 0
  mcdc: int = 0
  covered_mcdc: int = 0


def percent(covered: int, total: int) -> str:
  if total == 0:
    return "-"
  return f"{covered / total * 100:6.2f}%"


def branch_key(branch: list[object]) -> tuple[object, ...]:
  return tuple(branch[:4])


def mcdc_key(record: list[object]) -> tuple[object, ...]:
  return tuple(record[:4])


def compatible_except(left: tuple[object, ...], right: tuple[object, ...],
                      index: int) -> bool:
  for pos, (left_value, right_value) in enumerate(zip(left, right)):
    if pos == index:
      if left_value is None or right_value is None or left_value == right_value:
        return False
      continue
    if left_value is None or right_value is None:
      continue
    if left_value != right_value:
      return False
  return True


def covered_mcdc_conditions(records: list[list[object]]) -> tuple[int, int]:
  condition_count = max((len(record[9]) for record in records), default=0)
  vectors: list[tuple[tuple[object, ...], object]] = []
  for record in records:
    for vector in record[10]:
      if not vector.get("executed", False):
        continue
      vectors.append((tuple(vector["conditions"]), vector["result"]))

  covered = 0
  for index in range(condition_count):
    condition_covered = False
    for left_index, (left_conditions, left_result) in enumerate(vectors):
      for right_conditions, right_result in vectors[left_index + 1:]:
        if left_result == right_result:
          continue
        if compatible_except(left_conditions, right_conditions, index):
          condition_covered = True
          break
      if condition_covered:
        break
    if condition_covered:
      covered += 1
  return covered, condition_count


def file_totals(file_data: dict[str, object]) -> Totals:
  executable_lines: set[int] = set()
  covered_lines: set[int] = set()
  for segment in file_data["segments"]:
    line = int(segment[0])
    count = int(segment[2])
    has_count = bool(segment[3])
    if not has_count:
      continue
    executable_lines.add(line)
    if count > 0:
      covered_lines.add(line)

  branches: dict[tuple[object, ...], list[int]] = {}
  for branch in file_data["branches"]:
    counts = branches.setdefault(branch_key(branch), [0, 0])
    counts[0] += int(branch[4])
    counts[1] += int(branch[5])

  mcdc_records: dict[tuple[object, ...], list[list[object]]] = {}
  for record in file_data["mcdc_records"]:
    mcdc_records.setdefault(mcdc_key(record), []).append(record)

  covered_mcdc = 0
  mcdc = 0
  for records in mcdc_records.values():
    covered, total = covered_mcdc_conditions(records)
    covered_mcdc += covered
    mcdc += total

  return Totals(
      lines=len(executable_lines),
      covered_lines=len(covered_lines),
      branches=len(branches),
      covered_branches=sum(1 for true_count, false_count in branches.values()
                           if true_count > 0 or false_count > 0),
      mcdc=mcdc,
      covered_mcdc=covered_mcdc,
  )


def main() -> int:
  if len(sys.argv) != 2:
    print("usage: coverage_logical.py <llvm-cov-export.json>", file=sys.stderr)
    return 2

  data = json.loads(Path(sys.argv[1]).read_text())
  rows: list[tuple[str, Totals]] = []
  total = Totals()

  for file_data in data["data"][0]["files"]:
    filename = Path(file_data["filename"]).as_posix()
    if "/include/agner/" not in filename:
      continue
    display_name = filename.split("/include/agner/", 1)[1]
    totals = file_totals(file_data)
    rows.append((display_name, totals))
    total.lines += totals.lines
    total.covered_lines += totals.covered_lines
    total.branches += totals.branches
    total.covered_branches += totals.covered_branches
    total.mcdc += totals.mcdc
    total.covered_mcdc += totals.covered_mcdc

  print("Filename                         Lines  Missed Lines   Cover  "
        "Branches  Missed Branches   Cover  MC/DC Conditions  "
        "Missed Conditions   Cover")
  print("-" * 150)
  for name, totals in rows:
    print(f"{name:<30} {totals.lines:6d} "
          f"{totals.lines - totals.covered_lines:13d} "
          f"{percent(totals.covered_lines, totals.lines):>7} "
          f"{totals.branches:9d} "
          f"{totals.branches - totals.covered_branches:16d} "
          f"{percent(totals.covered_branches, totals.branches):>7} "
          f"{totals.mcdc:17d} "
          f"{totals.mcdc - totals.covered_mcdc:18d} "
          f"{percent(totals.covered_mcdc, totals.mcdc):>7}")
  print("-" * 150)
  print(f"{'TOTAL':<30} {total.lines:6d} "
        f"{total.lines - total.covered_lines:13d} "
        f"{percent(total.covered_lines, total.lines):>7} "
        f"{total.branches:9d} "
        f"{total.branches - total.covered_branches:16d} "
        f"{percent(total.covered_branches, total.branches):>7} "
        f"{total.mcdc:17d} "
        f"{total.mcdc - total.covered_mcdc:18d} "
        f"{percent(total.covered_mcdc, total.mcdc):>7}")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
