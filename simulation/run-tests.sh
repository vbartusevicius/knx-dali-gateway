#!/bin/sh
# SPDX-FileCopyrightText: 2026 KNX-DALI gateway contributors
# SPDX-License-Identifier: MIT
#
# Docker integration lab entry point: a clean stack per run — the gateway
# starts unprogrammed (prog mode on) exactly once, so a previous run's
# programmed device must not linger. Usage:
#   simulation/run-tests.sh [pytest args...]
set -u
cd "$(dirname "$0")/.."
COMPOSE="docker compose -f simulation/compose.yaml"

$COMPOSE down --remove-orphans >/dev/null 2>&1
$COMPOSE build gateway-lab dali-sim tests
rc=$?
if [ $rc -eq 0 ]; then
    $COMPOSE run --rm tests python -m pytest -v -s tests/ "$@"
    rc=$?
fi
$COMPOSE down --remove-orphans >/dev/null 2>&1
exit $rc
