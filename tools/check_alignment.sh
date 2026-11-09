#!/usr/bin/env bash
# The local gate before a push. It runs the same static checks the
# alignment-static job runs in CI, and nothing else: python3 only, no
# Windows binary, no build. Run it from anywhere; it finds the repo.

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(dirname "$here")"
cd "$repo"

run() {
    echo "== $*"
    "$@"
}

# The checkers, with their own tests first, so a checker bug is caught
# here and not on a red CI run.
run python3 tools/test_check_sources_listed.py
run python3 tools/check_sources_listed.py
run python3 tools/test_check_parity_ledger.py
run python3 tools/check_parity_ledger.py
run python3 tools/test_check_command_coverage.py
run python3 tools/check_command_coverage.py
run python3 tools/test_check_test_inventory.py
run python3 tools/check_shared_guards.py
run python3 tools/check_mac_sources.py
run python3 tools/test_check_capture_policy.py
run python3 tools/check_capture_policy.py

# Advisory only, same as in CI.
python3 tools/check_paired_changes.py --base "${1:-HEAD~1}" --head HEAD || true

echo "alignment: every static gate is green"
