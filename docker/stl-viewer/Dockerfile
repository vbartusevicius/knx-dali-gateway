# Static STL/STEP listing + three.js viewer; the generated models are
# mounted at /models by compose (read-only). three.js comes from the npm
# registry tarball, pinned by SHA-256.
FROM alpine:3.22 AS three
ARG THREE_VERSION=0.186.1
ARG THREE_SHA256=8cd068708ea44f2c73c944b1cead2ba2f0d5c15c8fc194e5700f4e4f4a033fe7
RUN apk add --no-cache curl \
    && curl -fsSL -o /tmp/three.tgz "https://registry.npmjs.org/three/-/three-${THREE_VERSION}.tgz" \
    && echo "${THREE_SHA256}  /tmp/three.tgz" | sha256sum -c - \
    && mkdir -p /vendor/addons/controls /vendor/addons/loaders \
    && tar xzf /tmp/three.tgz -C /tmp \
    && cp /tmp/package/build/three.module.js /tmp/package/build/three.core.js /vendor/ \
    && cp /tmp/package/examples/jsm/controls/OrbitControls.js /vendor/addons/controls/ \
    && cp /tmp/package/examples/jsm/loaders/STLLoader.js /vendor/addons/loaders/

FROM nginx:1.29-alpine
COPY docker/stl-viewer/nginx.conf /etc/nginx/conf.d/default.conf
COPY docker/stl-viewer/index.html /usr/share/nginx/html/index.html
COPY --from=three /vendor /usr/share/nginx/html/vendor
