# Multi-arch native EDA image (amd64 & arm64): Ubuntu 24.04 + KiCad 10 PPA
# (including kicad-packages3d for photorealistic 3D board renders), SKiDL,
# kinet2pcb, OR-Tools CP-SAT, libngspice0, and fastroute 0.1.14 (GPL-3.0).
FROM ubuntu:24.04

ARG TARGETARCH
ARG KICAD_VERSION=10.0.8~ubuntu24.04.1
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends software-properties-common gpg-agent \
    && add-apt-repository -y ppa:kicad/kicad-10.0-releases \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
       kicad=${KICAD_VERSION} kicad-footprints=${KICAD_VERSION} \
       kicad-symbols=${KICAD_VERSION} kicad-templates=${KICAD_VERSION} \
       kicad-packages3d=${KICAD_VERSION} \
       libngspice0 python3-pip python3-venv curl ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# The venv shares the system site-packages so that `python3` sees both the
# distro's pcbnew bindings and the pip-installed SKiDL/kinet2pcb/pytest.
RUN python3 -m venv --system-site-packages /opt/eda \
    && /opt/eda/bin/pip install --no-cache-dir \
       skidl==2.3.0 \
       kinet2pcb==1.1.4 \
       pyyaml==6.0.3 \
       pytest==8.4.2 \
       pillow==11.3.0 \
       shapely==2.1.1 \
       ortools==9.15.6755

# SKiDL and KiCad 3D renderer discover stock libraries through these variables.
ENV KICAD10_SYMBOL_DIR=/usr/share/kicad/symbols \
    KICAD_SYMBOL_DIR=/usr/share/kicad/symbols \
    KICAD10_FOOTPRINT_DIR=/usr/share/kicad/footprints \
    KICAD10_3DMODEL_DIR=/usr/share/kicad/3dmodels \
    KICAD9_SYMBOL_DIR=/usr/share/kicad/symbols \
    KICAD9_FOOTPRINT_DIR=/usr/share/kicad/footprints \
    KICAD9_3DMODEL_DIR=/usr/share/kicad/3dmodels \
    PATH=/opt/eda/bin:$PATH

RUN mkdir -p /root/.config/kicad/10.0 \
    && cp /usr/share/kicad/template/fp-lib-table /root/.config/kicad/10.0/fp-lib-table \
    && cp /usr/share/kicad/template/fp-lib-table /root/.config/kicad/fp-lib-table \
    && cp /usr/share/kicad/template/sym-lib-table /root/.config/kicad/10.0/sym-lib-table 2>/dev/null || true \
    # the repository mounts at /work; register its custom footprint library
    && sed -i '$i\  (lib (name gateway_fp)(type KiCad)(uri /work/hardware/circuits/lib/gateway_fp.pretty)(options "")(descr "project footprints"))' \
       /root/.config/kicad/10.0/fp-lib-table /root/.config/kicad/fp-lib-table

# kinet2pcb 1.1.4 doesn't strip quotes from the KiCad 10 fp-lib-table
# (which uses quoted attribute values); remove quotes so library lookup works
COPY docker/eda/fix_fp_lib_table.py /tmp/fix_fp_lib_table.py
RUN python3 /tmp/fix_fp_lib_table.py && rm /tmp/fix_fp_lib_table.py

ARG FASTROUTE_VERSION=0.1.14
RUN case "$TARGETARCH" in \
       amd64) arch=x64; sum=079a109f76d84836e7d7a54519e75759dcab9baacc9c58b4224b027b852e49c6 ;; \
       arm64) arch=arm64; sum=ce4e488a4de2c1782e40f577a29025c79bc59da8f28bb11974425b3d1dddc01f ;; \
       *) echo "unsupported architecture: $TARGETARCH" >&2; exit 1 ;; \
    esac \
    && curl -fsSL -o /tmp/fastroute.tar.gz \
       https://github.com/parisxmas/fastroute/releases/download/v${FASTROUTE_VERSION}/fastroute-${FASTROUTE_VERSION}-linux-${arch}.tar.gz \
    && echo "${sum}  /tmp/fastroute.tar.gz" | sha256sum -c - \
    && tar -xzf /tmp/fastroute.tar.gz -C /tmp \
    && install -m 755 /tmp/fastroute-${FASTROUTE_VERSION}-linux-${arch}/fastroute /usr/local/bin/fastroute \
    && install -d /usr/share/doc/fastroute \
    && install -m 644 /tmp/fastroute-${FASTROUTE_VERSION}-linux-${arch}/LICENSE /tmp/fastroute-${FASTROUTE_VERSION}-linux-${arch}/NOTICE /usr/share/doc/fastroute/ \
    && rm -rf /tmp/fastroute*

WORKDIR /work
ENTRYPOINT ["/bin/bash", "-c"]
