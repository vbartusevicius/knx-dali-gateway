# KiCad MCP servers for the agent on top of the eda image (KiCad 10, Java 25,
# Freerouting 2.5.0): mcp-server-kicad (ProductOfAmerica, MIT) and
# KiCAD-MCP-Server (mixelpixx, MIT). Run as stdio servers via `docker run -i`.
FROM eda

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
