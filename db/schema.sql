-- ScanPay schema. Applied by the database owner; the application connects as
-- the least-privilege role `scanpay_app` (see db/grants.sql).
--
-- Money is always stored as BIGINT cents. No floating point anywhere.

BEGIN;

CREATE TABLE IF NOT EXISTS users (
    id              BIGSERIAL PRIMARY KEY,
    full_name       TEXT        NOT NULL CHECK (length(full_name) BETWEEN 1 AND 100),
    phone           TEXT        NOT NULL UNIQUE CHECK (phone ~ '^\+1[0-9]{10}$'),
    password_hash   TEXT        NOT NULL CHECK (password_hash LIKE '$argon2id$%'),
    role            TEXT        NOT NULL CHECK (role IN ('customer', 'merchant')),
    balance_cents   BIGINT      NOT NULL DEFAULT 0 CHECK (balance_cents >= 0),
    failed_logins   INTEGER     NOT NULL DEFAULT 0 CHECK (failed_logins >= 0),
    locked_until    TIMESTAMPTZ,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS merchants (
    id              BIGSERIAL PRIMARY KEY,
    user_id         BIGINT      NOT NULL UNIQUE REFERENCES users(id),
    display_name    TEXT        NOT NULL UNIQUE CHECK (length(display_name) BETWEEN 1 AND 100),
    active          BOOLEAN     NOT NULL DEFAULT TRUE
);

-- Only a SHA-256 hash of the session token is stored, so a database leak does
-- not hand out live sessions.
CREATE TABLE IF NOT EXISTS sessions (
    token_hash      TEXT        PRIMARY KEY CHECK (token_hash ~ '^[0-9a-f]{64}$'),
    user_id         BIGINT      NOT NULL REFERENCES users(id),
    csrf_token      TEXT        NOT NULL,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT now(),
    last_seen       TIMESTAMPTZ NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS sessions_user_idx ON sessions(user_id);

-- A single-use, short-lived permission to move one exact amount from payer to
-- payee. Bound to the session that created it.
CREATE TABLE IF NOT EXISTS payment_authorizations (
    id              UUID        PRIMARY KEY DEFAULT gen_random_uuid(),
    payer_id        BIGINT      NOT NULL REFERENCES users(id),
    payee_id        BIGINT      NOT NULL REFERENCES users(id),
    session_hash    TEXT        NOT NULL,
    amount_cents    BIGINT      NOT NULL CHECK (amount_cents BETWEEN 1 AND 50000),
    kind            TEXT        NOT NULL CHECK (kind IN ('qr', 'phone')),
    status          TEXT        NOT NULL DEFAULT 'pending' CHECK (status IN ('pending', 'used', 'expired')),
    created_at      TIMESTAMPTZ NOT NULL DEFAULT now(),
    expires_at      TIMESTAMPTZ NOT NULL,
    used_at         TIMESTAMPTZ,
    CHECK (payer_id <> payee_id)
);
CREATE INDEX IF NOT EXISTS payment_authorizations_payer_idx ON payment_authorizations(payer_id);

-- UNIQUE(authorization_id) is the database-level guarantee that one
-- authorization can never produce two transactions.
CREATE TABLE IF NOT EXISTS transactions (
    id                BIGSERIAL   PRIMARY KEY,
    authorization_id  UUID        NOT NULL UNIQUE REFERENCES payment_authorizations(id),
    payer_id          BIGINT      NOT NULL REFERENCES users(id),
    payee_id          BIGINT      NOT NULL REFERENCES users(id),
    amount_cents      BIGINT      NOT NULL CHECK (amount_cents BETWEEN 1 AND 50000),
    kind              TEXT        NOT NULL CHECK (kind IN ('qr', 'phone')),
    created_at        TIMESTAMPTZ NOT NULL DEFAULT now(),
    CHECK (payer_id <> payee_id)
);
CREATE INDEX IF NOT EXISTS transactions_payer_idx ON transactions(payer_id, created_at DESC);
CREATE INDEX IF NOT EXISTS transactions_payee_idx ON transactions(payee_id, created_at DESC);

COMMIT;
