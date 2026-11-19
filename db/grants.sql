-- Least-privilege grants for the application role. Run after schema.sql by the
-- database owner. The role itself is created by db/init/00-init.sh or
-- scripts/init-db.sh so its password never lives in a SQL file.
--
-- The app can read and write rows, but cannot change the schema, cannot delete
-- users, authorizations or transactions (the payment ledger is append-only),
-- and can only delete sessions (logout / expiry).

REVOKE ALL ON ALL TABLES IN SCHEMA public FROM scanpay_app;
REVOKE CREATE ON SCHEMA public FROM PUBLIC;
GRANT USAGE ON SCHEMA public TO scanpay_app;

GRANT SELECT, UPDATE         ON users                  TO scanpay_app;
GRANT INSERT (full_name, phone, password_hash, role, balance_cents) ON users TO scanpay_app;
GRANT SELECT, INSERT         ON merchants              TO scanpay_app;
GRANT SELECT, INSERT, UPDATE, DELETE ON sessions       TO scanpay_app;
GRANT SELECT, INSERT, UPDATE ON payment_authorizations TO scanpay_app;
GRANT SELECT, INSERT         ON transactions           TO scanpay_app;

GRANT USAGE, SELECT ON ALL SEQUENCES IN SCHEMA public TO scanpay_app;
