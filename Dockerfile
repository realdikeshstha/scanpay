# ---- build: compile the app, seed tool and tests ---------------------------
FROM debian:trixie-slim AS build
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      build-essential cmake git ca-certificates pkg-config \
      libpqxx-dev libsodium-dev libasio-dev libssl-dev \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt .
COPY src src
COPY tools tools
COPY tests tests
COPY templates templates
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSCANPAY_BUILD_TESTS=ON \
 && cmake --build build -j"$(nproc)" \
 && ./build/unit_tests

# ---- runtime: minimal image, non-root user ----------------------------------
FROM debian:trixie-slim AS runtime
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      libpqxx-7.10 libsodium23 libssl3t64 openssl ca-certificates curl \
 && rm -rf /var/lib/apt/lists/* \
 && useradd --system --uid 10001 --home-dir /app --shell /usr/sbin/nologin scanpay
WORKDIR /app
COPY --from=build /src/build/scanpay /src/build/scanpay_seed /app/bin/
COPY templates templates
COPY static static
COPY docker/entrypoint.sh /app/entrypoint.sh
RUN chmod 0555 /app/entrypoint.sh \
 && mkdir -p /app/certs /app/logs \
 && chown scanpay:scanpay /app/certs /app/logs
USER scanpay
EXPOSE 8443
HEALTHCHECK --interval=10s --timeout=3s --start-period=10s \
  CMD curl -fsk https://localhost:8443/healthz || exit 1
ENTRYPOINT ["/app/entrypoint.sh"]
