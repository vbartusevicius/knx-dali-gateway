#!/bin/sh
# Build knxprod/build/KnxDaliGateway.knxprod (schema project/20: imports into
# ETS 5.7 and ETS 6) and verify it against the firmware header.
#   ETS_CV_DIR=knxprod/ETS knxprod/scripts/package.sh       signed via ETS DLLs/Wine
#   knxprod/scripts/package.sh                             unsigned (structure only)
# ETS libraries are mounted read-only, copied into the disposable Wine prefix,
# and never enter an image. ETS_SIGNING_KEY_FILE remains an optional alternative.
set -eu
cd "$(dirname "$0")/../.."
run="docker compose --profile knxprod run --rm -T"
out=knxprod/build
mkdir -p "$out"
trap 'rm -f knxprod/src/KnxDaliGateway.debug.xml knxprod/src/KnxDaliGateway.appl.debug.xml knxprod/src/KnxDaliGateway.h' EXIT

$run producer create -d src/KnxDaliGateway.xml >"$out/producer.log"
# OpenKNXproducer writes no Hash attribute; knx-rs-prod only fills an existing one.
$run --entrypoint python3 producer scripts/prepare_xml.py \
    src/KnxDaliGateway.debug.xml build/KnxDaliGateway.xml

if [ -n "${ETS_CV_DIR:-}" ]; then
    ets="$(cd "$ETS_CV_DIR" && pwd)"
    $run -v "$ets:/opt/ets:ro" producer-wine
    exit 0
fi

signing="" signed=""
if [ -n "${ETS_SIGNING_KEY_FILE:-}" ]; then
    key="$(cd "$(dirname "$ETS_SIGNING_KEY_FILE")" && pwd)/$(basename "$ETS_SIGNING_KEY_FILE")"
    signing="-v $key:/run/secrets/ets-signing-key:ro"
    signed="--signed"
fi
# shellcheck disable=SC2086
$run $signing --entrypoint knx-rs-prod producer \
    build/KnxDaliGateway.xml -o build/KnxDaliGateway.knxprod \
    ${signed:+--key /run/secrets/ets-signing-key --knx-master /opt/knx/knx_master-20.xml}

$run --entrypoint python3 producer scripts/verify_knxprod.py \
    build/KnxDaliGateway.knxprod ../gateway/src/knx/generated/KnxDaliGateway.h $signed
