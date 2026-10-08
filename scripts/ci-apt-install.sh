#!/bin/bash
#
# ci-apt-install.sh — Bounded, retrying apt-get install for CI runners
#
# Usage:
#   ./scripts/ci-apt-install.sh <package> [<package> ...]
#
# Hosted runners occasionally lose the Azure Ubuntu mirror mid-download and
# apt-get then blocks with no output until the 6-hour job limit cancels the
# run. Each apt-get call here is wrapped in `timeout` with socket-level
# timeouts, and the whole update+install sequence is retried.
#
# Environment:
#   APT_ATTEMPTS         number of attempts (default 3)
#   APT_STEP_TIMEOUT     seconds allowed per apt-get call (default 300)
#
set -euo pipefail

if [ "$#" -eq 0 ]; then
  echo "usage: $0 <package> [<package> ...]" >&2
  exit 2
fi

attempts="${APT_ATTEMPTS:-3}"
step_timeout="${APT_STEP_TIMEOUT:-300}"

apt_opts=(
  -o Acquire::Retries=3
  -o Acquire::http::Timeout=30
  -o Acquire::https::Timeout=30
  -o DPkg::Lock::Timeout=120
)

for attempt in $(seq 1 "$attempts"); do
  echo "::group::apt attempt ${attempt}/${attempts}"
  if sudo timeout "$step_timeout" apt-get "${apt_opts[@]}" update \
    && sudo DEBIAN_FRONTEND=noninteractive timeout "$step_timeout" \
      apt-get "${apt_opts[@]}" install -y "$@"; then
    echo "::endgroup::"
    exit 0
  fi
  echo "::endgroup::"
  if [ "$attempt" -lt "$attempts" ]; then
    echo "::warning::apt attempt ${attempt} failed or timed out; retrying"
    sleep $((attempt * 15))
  fi
done

echo "::error::apt-get failed after ${attempts} attempts: $*"
exit 1
