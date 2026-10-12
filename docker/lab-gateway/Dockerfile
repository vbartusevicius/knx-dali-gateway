# Lab gateway image: builds the PlatformIO `lab` environment (the real
# application on the thelsing LinuxPlatform, KNXnet/IP mask 0x57B0).
FROM python:3.13.9-slim-trixie

RUN apt-get update \
    && apt-get install -y --no-install-recommends g++ make git ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && pip install --no-cache-dir platformio==6.2.0

WORKDIR /gateway
COPY gateway/ /gateway/

# Fetch dependencies and build the lab binary at image-build time so container
# startup needs no network.
RUN pio run -e lab

ENV DALI_SIM_HOST=dali-sim \
    DALI_SIM_PORT=5555 \
    KNX_FLASH_FILE=/tmp/lab-flash.bin \
    DALI_LEVELS_FILE=/tmp/lab-levels.txt

CMD ["/gateway/.pio/build/lab/program"]
