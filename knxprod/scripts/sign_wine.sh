#!/bin/sh
set -eu
for name in Knx.Ets.XmlSigning.dll Knx.Ets.Xml.ObjectModel.dll Knx.Ets.Xml.RegistrationRelevanceInformation.dll; do
    if [ ! -f "/opt/ets/$name" ]; then
        echo "Missing ETS library: $name" >&2
        exit 1
    fi
done
ets="$WINEPREFIX/drive_c/Program Files (x86)/ETS5"
mkdir -p "$ets"
cp /opt/ets/*.dll "$ets/"
timeout 240 xvfb-run -a wine /opt/okp/tools/Windows/OpenKNXproducer-x64.exe \
    knxprod -N build/KnxDaliGateway.xml -o build/KnxDaliGateway.knxprod
python3 scripts/verify_knxprod.py build/KnxDaliGateway.knxprod \
    ../gateway/src/knx/generated/KnxDaliGateway.h --signed
