#!/bin/sh
# Regenerate the knxprod header from the ETS XML and install it into the
# firmware tree. OpenKNXproducer emits a self-referential accessor macro for
# every expanded per-channel KO name (same name as its number define); those
# broken lines are filtered out, keeping the committed header compilable.
# The producer writes <input>.h next to the XML; the filtered copy is moved
# into gateway/ and the raw output removed.
set -e
cd "$(dirname "$0")/../.."
docker compose --profile knxprod run --rm producer create src/KnxDaliGateway.xml
mkdir -p gateway/src/knx/generated
grep -v 'getGroupObject(Ko' knxprod/src/KnxDaliGateway.h > gateway/src/knx/generated/KnxDaliGateway.h
rm knxprod/src/KnxDaliGateway.h
