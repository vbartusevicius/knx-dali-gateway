FROM debian:trixie-slim@sha256:a29215f6a35e51e22adffa17f89e9d2ef06214e64a2bad10d765c46aea49f11f

ARG OKP_VERSION=4.3.12
ARG OKP_SHA256=c73d3a5181edc41551637a1ad31935b8a23f88694bd115f10127ead060e76266
ARG TARGETARCH

RUN apt-get update \
    && apt-get install -y --no-install-recommends ca-certificates curl unzip libicu76 \
    && rm -rf /var/lib/apt/lists/*

# The release zip ships self-contained single-file builds per RID; the zip's
# Linux dirs are named x64/arm64 while TARGETARCH is amd64/arm64.
RUN curl -fsSL -o /tmp/okp.zip \
        "https://github.com/OpenKNX/OpenKNXproducer/releases/download/v${OKP_VERSION}/OpenKNXproducer-${OKP_VERSION}.zip" \
    && echo "${OKP_SHA256}  /tmp/okp.zip" | sha256sum -c - \
    && arch="$(echo "$TARGETARCH" | sed 's/^amd64$/x64/')" \
    && unzip -o /tmp/okp.zip "tools/Linux/${arch}/OpenKNXproducer" -d /opt/okp \
    && chmod +x "/opt/okp/tools/Linux/${arch}/OpenKNXproducer" \
    && ln -s "/opt/okp/tools/Linux/${arch}/OpenKNXproducer" /usr/local/bin/OpenKNXproducer \
    && rm /tmp/okp.zip

WORKDIR /work
ENTRYPOINT ["OpenKNXproducer"]
