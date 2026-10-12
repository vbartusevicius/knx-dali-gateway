#!/bin/sh
# Fail if the committed generated header does not match a fresh regeneration.
# Applies the same self-referential-macro filter as generate.sh.
set -e
cd "$(dirname "$0")/../.."
out="$(mktemp -d)"
trap 'rm -rf "$out"; rm -f knxprod/src/KnxDaliGateway.h' EXIT
docker compose --profile knxprod run --rm producer create src/KnxDaliGateway.xml >/dev/null 2>&1
grep -v 'getGroupObject(Ko' knxprod/src/KnxDaliGateway.h > "$out/filtered.h"
if ! cmp -s "$out/filtered.h" gateway/src/knx/generated/KnxDaliGateway.h; then
    diff -u gateway/src/knx/generated/KnxDaliGateway.h "$out/filtered.h" >&2 || true
    echo "check.sh: committed gateway/src/knx/generated/KnxDaliGateway.h is stale; run scripts/generate.sh" >&2
    exit 1
fi
