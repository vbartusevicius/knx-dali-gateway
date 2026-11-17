# KiCad MCP servers for the agent on top of the eda image (KiCad 10):
# mcp-server-kicad (ProductOfAmerica, MIT) and KiCAD-MCP-Server (mixelpixx,
# MIT). Run as stdio servers via `docker run -i`. KiCAD-MCP-Server's autorouter
# tool runs Freerouting 2.5.0 on Java 25, so both stay in this image only.
FROM --platform=linux/amd64 eclipse-temurin:25.0.4.1_1-jre AS jre

FROM eda

COPY --from=jre /opt/java/openjdk /opt/java/openjdk
ENV JAVA_HOME=/opt/java/openjdk \
    PATH=/opt/java/openjdk/bin:$PATH \
    FREEROUTING_JAR=/opt/freerouting.jar
RUN curl -fsSL -o /opt/freerouting.jar \
       https://github.com/freerouting/freerouting/releases/download/v2.5.0/freerouting-2.5.0.jar \
    && echo "f6f51bb02245e8e717f9359bd260cc9c5c0b1bc0acc8b7cb2cd5b8ffeb5de3c7  /opt/freerouting.jar" \
       | sha256sum -c -

RUN apt-get update \
    && apt-get install -y --no-install-recommends git nodejs npm \
    && rm -rf /var/lib/apt/lists/*

RUN python3 -m venv --system-site-packages /opt/mcp \
    && /opt/mcp/bin/pip install --no-cache-dir mcp-server-kicad==0.20.1

RUN git clone --depth 1 --branch v2.8.2 https://github.com/mixelpixx/KiCAD-MCP-Server.git /opt/kicad-mcp-server \
    && cd /opt/kicad-mcp-server \
    && npm ci \
    && npm run build \
    && /opt/mcp/bin/pip install --no-cache-dir -r requirements.txt

ENV PATH=/opt/mcp/bin:$PATH \
    KICAD_PYTHON=/opt/mcp/bin/python \
    KICAD_MCP_PYTHON=/opt/mcp/bin/python

WORKDIR /work
ENTRYPOINT []
