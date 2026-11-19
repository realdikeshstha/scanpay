#!/usr/bin/env bash
# Writes .env with freshly generated random secrets (refuses to overwrite).
set -euo pipefail
cd "$(dirname "$0")/.."

if [[ -f .env ]]; then
  echo ".env already exists; delete it first if you want new secrets." >&2
  exit 1
fi

umask 077
cat > .env <<EOF
POSTGRES_PASSWORD=$(openssl rand -hex 16)
SCANPAY_DB_APP_PASSWORD=$(openssl rand -hex 16)
SCANPAY_QR_KEY=$(openssl rand -hex 32)
SCANPAY_CERT_SAN=
EOF
echo "Wrote .env with random secrets (mode 600)."
