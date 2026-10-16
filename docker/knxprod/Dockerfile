# ETS product toolchain: OpenKNXproducer expands knxprod/src into the monolithic
# product XML and the firmware header; knx-rs-prod (pure Rust, GPL-3.0) hashes,
# signs and packages it into the .knxprod. The signing key is never part of the
# image; it is mounted at run time (knxprod/scripts/package.sh).
FROM rust:1.98.0-slim-trixie AS knx-rs-prod
ARG KNX_RS_VERSION=0.10.1
ARG KNX_RS_SHA256=aac044d295b5fc65a7e708a151e3c8a98ad1f8673e2b4e8713a3b6f5066aee4d
RUN apt-get update && apt-get install -y --no-install-recommends curl ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && curl -fsSL -o /tmp/knx-rs.tgz \
       "https://github.com/metaneutrons/knx-rs/releases/download/v${KNX_RS_VERSION}/knx-rs-v${KNX_RS_VERSION}.tar.gz" \
    && echo "${KNX_RS_SHA256}  /tmp/knx-rs.tgz" | sha256sum -c - \
    && mkdir /src && tar xzf /tmp/knx-rs.tgz -C /src --strip-components=1
WORKDIR /src
RUN cargo build --release --locked -p knx-rs-prod

FROM debian:trixie-slim@sha256:a29215f6a35e51e22adffa17f89e9d2ef06214e64a2bad10d765c46aea49f11f

ARG OKP_VERSION=4.3.12
ARG OKP_SHA256=c73d3a5181edc41551637a1ad31935b8a23f88694bd115f10127ead060e76266
ARG TARGETARCH

RUN apt-get update \
    && apt-get install -y --no-install-recommends ca-certificates curl unzip libicu76 python3 python3-pytest \
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

RUN mkdir -p /opt/knx \
    && curl -fsSL -o /opt/knx/knx_master-20.xml https://update.knx.org/data/XML/project-20/knx_master.xml \
    && grep -q "<MasterData" /opt/knx/knx_master-20.xml

COPY --from=knx-rs-prod /src/target/release/knx-rs-prod /usr/local/bin/knx-rs-prod

WORKDIR /work
ENTRYPOINT ["OpenKNXproducer"]
