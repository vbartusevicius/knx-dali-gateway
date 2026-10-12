# Shared Python image for the DALI simulator and the pytest suite.
FROM ghcr.io/astral-sh/uv:0.12.22-python3.13-trixie-slim

WORKDIR /app
COPY simulation/pyproject.toml simulation/uv.lock ./
RUN uv sync --frozen --no-install-project
COPY simulation/src ./src
COPY simulation/tests ./tests
RUN uv sync --frozen

ENTRYPOINT ["uv", "run", "--frozen", "--no-sync"]
