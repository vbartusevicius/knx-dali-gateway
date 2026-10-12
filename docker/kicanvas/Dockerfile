# KiCanvas PCB viewer built from a pinned upstream commit (MIT); the boards
# are mounted at /pcb by compose (read-only).
FROM node:22-alpine AS build
ARG KICANVAS_COMMIT=b031159eb74aaa7eef2b026fd85d35bc05ff2095
ARG KICANVAS_SHA256=033c1ea20ae04605d2c00ff97e1f0ee2035b9b4d1d3b2a4968ff0845292ccb93
RUN apk add --no-cache curl \
    && curl -fsSL -o /tmp/kicanvas.tgz "https://codeload.github.com/theacodes/kicanvas/tar.gz/${KICANVAS_COMMIT}" \
    && echo "${KICANVAS_SHA256}  /tmp/kicanvas.tgz" | sha256sum -c - \
    && mkdir /src && tar xzf /tmp/kicanvas.tgz -C /src --strip-components=1
WORKDIR /src
RUN npm ci && npm run build:no-check

FROM nginx:1.29-alpine
COPY docker/kicanvas/nginx.conf /etc/nginx/conf.d/default.conf
COPY docker/kicanvas/index.html /usr/share/nginx/html/index.html
COPY --from=build /src/build/kicanvas.js /usr/share/nginx/html/kicanvas.js
