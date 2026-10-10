#!/bin/sh
# Regenerate the knxprod header from the ETS XML and install it into the
# firmware tree. OpenKNXproducer emits a self-referential accessor macro for
# every expanded per-channel KO name (same name as its number define); those
# broken lines are filtered out, keeping the committed header compilable.
# The producer writes <input>.h next to the XML, so generation runs in a temp
# copy of this directory and only the filtered header is copied back.
set -e
cd "$(dirname "$0")/.."
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
cp compose.yaml Dockerfile "$out/"
cp -R src "$out/src"
docker compose -f "$out/compose.yaml" run --rm producer create src/KnxDaliGateway.xml
mkdir -p ../gateway/src/knx/generated
grep -v 'getGroupObject(Ko' "$out/src/KnxDaliGateway.h" > ../gateway/src/knx/generated/KnxDaliGateway.h
