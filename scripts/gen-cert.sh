#!/usr/bin/env bash
# Self-signed TLS certificate for running ScanPay natively over HTTPS.
#
#   ./scripts/gen-cert.sh                    # localhost only
#   ./scripts/gen-cert.sh IP:192.168.1.20    # also valid for a LAN address (phone demo)
#
# Writes certs/cert.pem and certs/key.pem (git-ignored).
set -euo pipefail
cd "$(dirname "$0")/.."

san="DNS:localhost,IP:127.0.0.1"
if [[ $# -gt 0 ]]; then san="$san,$1"; fi

mkdir -p certs
umask 077
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes -days 30 \
  -subj "/CN=ScanPay local demo" -addext "subjectAltName=$san" \
  -keyout certs/key.pem -out certs/cert.pem 2>/dev/null
echo "Wrote certs/cert.pem and certs/key.pem for: $san"
