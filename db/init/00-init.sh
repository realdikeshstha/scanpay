#!/usr/bin/env bash
# Runs once inside the official postgres image (docker-entrypoint-initdb.d).
# Creates the least-privilege application role, then the `scanpay` database
# (used by the app) and `scanpay_test` (used by the integration tests).
set -euo pipefail

: "${SCANPAY_DB_APP_PASSWORD:?SCANPAY_DB_APP_PASSWORD must be set}"

psql -v ON_ERROR_STOP=1 --username "$POSTGRES_USER" --dbname "$POSTGRES_DB" \
     -v app_password="$SCANPAY_DB_APP_PASSWORD" <<'SQL'
SELECT format('CREATE ROLE scanpay_app LOGIN PASSWORD %L', :'app_password')
WHERE NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname = 'scanpay_app')
\gexec
SQL

psql -v ON_ERROR_STOP=1 --username "$POSTGRES_USER" --dbname "$POSTGRES_DB" -c "CREATE DATABASE scanpay_test"

for db in "$POSTGRES_DB" scanpay_test; do
  psql -v ON_ERROR_STOP=1 --username "$POSTGRES_USER" --dbname "$db" -q -f /scanpay-db/schema.sql
  psql -v ON_ERROR_STOP=1 --username "$POSTGRES_USER" --dbname "$db" -q -f /scanpay-db/grants.sql
done
