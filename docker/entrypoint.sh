#!/bin/sh
# Container entrypoint: create a self-signed TLS certificate if none is
# mounted, seed the fictional demo accounts (idempotent), then start ScanPay.
set -eu

if [ -n "${SCANPAY_TLS_CERT:-}" ] && [ ! -f "$SCANPAY_TLS_CERT" ]; then
  san="DNS:localhost,IP:127.0.0.1"
  if [ -n "${SCANPAY_CERT_SAN:-}" ]; then san="$san,$SCANPAY_CERT_SAN"; fi
  umask 077
  openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes -days 30 \
    -subj "/CN=ScanPay local demo" -addext "subjectAltName=$san" \
    -keyout "$SCANPAY_TLS_KEY" -out "$SCANPAY_TLS_CERT" 2>/dev/null
  echo "Generated self-signed certificate for: $san"
fi

attempt=0
until /app/bin/scanpay_seed; do
  attempt=$((attempt + 1))
  if [ "$attempt" -ge 15 ]; then
    echo "database not reachable; giving up" >&2
    exit 1
  fi
  sleep 2
done

exec /app/bin/scanpay
