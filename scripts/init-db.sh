#!/usr/bin/env bash
# Native (non-Docker) database setup for local development and tests.
#
#   SCANPAY_DB_APP_PASSWORD=... ./scripts/init-db.sh            # creates scanpay + scanpay_test
#   SCANPAY_DB_APP_PASSWORD=... ./scripts/init-db.sh --reset    # drops and recreates them
#
# Uses whatever superuser `psql` connects as by default (PGHOST/PGUSER/... apply).
set -euo pipefail

cd "$(dirname "$0")/.."
: "${SCANPAY_DB_APP_PASSWORD:?Set SCANPAY_DB_APP_PASSWORD (see .env.example)}"

reset=false
[[ "${1:-}" == "--reset" ]] && reset=true

psql -v ON_ERROR_STOP=1 -d postgres -v app_password="$SCANPAY_DB_APP_PASSWORD" <<'SQL'
SELECT format('CREATE ROLE scanpay_app LOGIN PASSWORD %L', :'app_password')
WHERE NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname = 'scanpay_app')
\gexec
SQL

for db in scanpay scanpay_test; do
  if $reset; then
    psql -v ON_ERROR_STOP=1 -d postgres -c "DROP DATABASE IF EXISTS $db WITH (FORCE)"
  fi
  if ! psql -d postgres -tAc "SELECT 1 FROM pg_database WHERE datname = '$db'" | grep -q 1; then
    psql -v ON_ERROR_STOP=1 -d postgres -c "CREATE DATABASE $db"
  fi
  psql -v ON_ERROR_STOP=1 -d "$db" -q -f db/schema.sql
  psql -v ON_ERROR_STOP=1 -d "$db" -q -f db/grants.sql
  echo "Database $db ready."
done
