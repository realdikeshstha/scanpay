# ScanPay — Secure QR Payments in C++

**Team Aegis** — Kaden Jones, Jackson Williams, Dikesh Shrestha, Sumit Singh
CSC 327 Final Project. A classroom payment simulation: fictional accounts, no real money.

ScanPay lets customers pay a business by scanning the business's **reusable printed QR code**, or send money to
another user by **phone number**. Every payment goes through a **single-use, 2-minute authorization** bound to the
customer's session, the payee and the exact amount, so a payment can never be charged twice.

- C++20 + [Crow](https://github.com/CrowCpp/Crow) web server, PostgreSQL via libpqxx
- Mustache templates (auto-escaped), plain HTML/CSS/JS, [jsQR](https://github.com/cozmo/jsQR) camera scanning
- libsodium: Argon2id password hashing, HMAC-signed QR codes, CSPRNG tokens
- CMake + CTest, Cppcheck, AddressSanitizer / UndefinedBehaviorSanitizer, OWASP ZAP

---

## 1. Quick start (Docker — recommended)

Requirements: Docker with Compose (Docker Desktop, or `brew install colima docker docker-compose && colima start`).

```bash
./scripts/make-env.sh            # writes .env with random secrets (never committed)
docker compose up --build        # or: docker-compose up --build
```

Open **https://localhost:8443** and accept the self-signed certificate warning
(the certificate is generated inside the container on first start).

The first build downloads and compiles everything (a few minutes). The build also runs the unit tests.

Reset all demo data (balances, lockouts, payments): `docker compose down -v && docker compose up`.

### Test accounts (fictional)

| Role     | Name                | Phone (login)  | Password           | Starting balance |
|----------|---------------------|----------------|--------------------|------------------|
| Customer | Alice Carter        | +15555550101   | `alice-demo-2026`  | $250.00          |
| Customer | Ben Ortiz           | +15555550102   | `ben-demo-2026`    | $100.00          |
| Customer | Chloe Nguyen        | +15555550103   | `chloe-demo-2026`  | $40.00           |
| Merchant | Golden Eagle Coffee | +15555550201   | `coffee-demo-2026` | $0.00            |
| Merchant | Hattiesburg Books   | +15555550202   | `books-demo-2026`  | $0.00            |

These are demo credentials for fictional seed data created by [`tools/seed.cpp`](tools/seed.cpp); only their
Argon2id hashes are stored.

### Merchant QR codes

Sign in as a merchant and open **My QR** (`/merchant/qr`) to display or print the code. The page also shows the QR
text, which can be pasted on the customer's **Scan** page when no camera is available. The payloads are also printed in
the app's startup log (`docker compose logs app`).

### Scanning from a phone

The camera API requires HTTPS. To scan with a phone on the same Wi-Fi, set your computer's LAN IP in `.env`, for
example `SCANPAY_CERT_SAN=IP:192.168.1.20`, then `docker compose down && docker compose up` (the certificate is
regenerated on start) and browse to `https://192.168.1.20:8443` from the phone.

---

## 2. Running the tests

```bash
docker compose --profile test run --rm tests     # unit + integration tests against a real Postgres
```

| Suite | What it covers |
|-------|----------------|
| `unit` ([`tests/unit`](tests/unit)) | amount parsing, phone validation, QR signing/tampering, Argon2id, CSRF compare, rate limiter |
| `integration` ([`tests/integration`](tests/integration)) | payments, concurrency, rollback, expiry, sessions, lockout, SQL injection, and HTTP-level CSRF / access control / XSS / headers through the Crow router |

The integration suite maps directly to the proposal's success criteria:

| Success criterion | Test |
|-------------------|------|
| Payments update both balances without exposing private info | `a QR payment moves the exact amount...`, `users cannot read other users' receipts` |
| Changed or fake QR codes are rejected | `tampered, forged and URL QR codes are rejected` |
| Users cannot access another person's records | `users cannot read other users' receipts`, `customers cannot reach merchant-only pages` |
| Repeating one payment 20 times creates one transaction | `submitting one authorization 20 times concurrently creates exactly one transaction` |
| No overspending under concurrent payments | `concurrent payments cannot spend more than the balance` |
| Failed payments leave no partial state | `a failure in the middle of a transfer rolls everything back` |
| Login protection / blocked attacks | `five wrong passwords lock the account...`, `...rate limited`, `state-changing requests without the session CSRF token are blocked` |

---

## 3. Native build (macOS / Linux)

Dependencies:

| Package | macOS (Homebrew) | Debian/Ubuntu |
|---------|------------------|---------------|
| C++20 compiler, CMake ≥ 3.25 | Xcode CLT, `cmake` | `build-essential cmake` |
| PostgreSQL 16 (server + client) | `postgresql@16` | `postgresql` |
| libpqxx ≥ 7.9 | `libpqxx` | `libpqxx-dev` |
| libsodium | `libsodium` | `libsodium-dev` |
| Asio (standalone) | `asio` | `libasio-dev` |
| OpenSSL | `openssl@3` | `libssl-dev` |
| Cppcheck (optional) | `cppcheck` | `cppcheck` |

Crow v1.3.5, Nayuki QR-Code-generator v1.8.0 and doctest v2.4.11 are fetched automatically by CMake;
jsQR 1.4.0 is vendored in [`static/vendor`](static/vendor).

```bash
brew install cmake postgresql@16 libpqxx libsodium asio openssl@3 cppcheck
brew services start postgresql@16

export SCANPAY_DB_APP_PASSWORD=$(openssl rand -hex 16)
./scripts/init-db.sh                        # creates role scanpay_app, databases scanpay + scanpay_test

cmake -S . -B build && cmake --build build -j

export DATABASE_URL="postgresql://scanpay_app:${SCANPAY_DB_APP_PASSWORD}@localhost/scanpay"
export SCANPAY_QR_KEY=$(openssl rand -hex 32)
./build/scanpay_seed                        # demo accounts + prints merchant QR payloads

./scripts/gen-cert.sh
SCANPAY_TLS_CERT=certs/cert.pem SCANPAY_TLS_KEY=certs/key.pem ./build/scanpay   # https://localhost:8443
```

Run from the repository root (templates and static files are loaded relative to it).
For quick local HTTP debugging only: omit the TLS variables and set `SCANPAY_SECURE_COOKIES=0`.

Tests, sanitizers and static analysis:

```bash
export SCANPAY_TEST_DATABASE_URL="postgresql://localhost/scanpay_test"          # owner connection (tests truncate it)
export SCANPAY_TEST_APP_DATABASE_URL="postgresql://scanpay_app:${SCANPAY_DB_APP_PASSWORD}@localhost/scanpay_test"
(cd build && ctest --output-on-failure)

cmake -S . -B build-asan -DSCANPAY_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-asan -j && (cd build-asan && ctest --output-on-failure)     # ASan + UBSan

cmake --build build --target cppcheck
```

Without `SCANPAY_TEST_DATABASE_URL` the integration suite reports as skipped.

### Configuration

All secrets come from environment variables (see [`.env.example`](.env.example)); none are in the source code.

| Variable | Required | Meaning |
|----------|----------|---------|
| `DATABASE_URL` | yes | libpq connection string for the `scanpay_app` role |
| `SCANPAY_QR_KEY` | yes | 64 hex chars; HMAC key for merchant QR codes. The app refuses to start with a missing or placeholder key |
| `SCANPAY_TLS_CERT`, `SCANPAY_TLS_KEY` | for HTTPS | PEM certificate chain and key; TLS 1.2+ only |
| `SCANPAY_PORT`, `SCANPAY_BIND` | no | default `8443`, `0.0.0.0` |
| `SCANPAY_SECURITY_LOG` | no | JSON-lines security log file (also written to stderr) |
| `SCANPAY_DB_POOL` | no | database connections, default 8 |
| `SCANPAY_SECURE_COOKIES` | no | `0` only for plain-HTTP local debugging |

---

## 4. How a payment works

```
Scan QR ──► POST /pay/review     server verifies the QR's HMAC tag, shows the verified merchant name
        ──► POST /pay/authorize  amount validated (1..50000 cents); authorization row created:
                                 payer + payee + amount + session hash, expires in 120 s
        ──► GET  /authorizations/<id>   "Pay $X to <merchant>?"
        ──► POST /pay/confirm    ONE database transaction:
                                   lock authorization (FOR UPDATE) → check owner, session, status, expiry
                                   lock both accounts in id order (no deadlocks) → check funds
                                   debit, credit, INSERT transaction, mark authorization used → COMMIT
                                 any error → automatic ROLLBACK
        ──► /receipts/<id>       repeat confirms return the same receipt
```

The amount debited, credited and printed on the receipt is always the server-stored authorization amount, never a
client-supplied value. `transactions.authorization_id` is `UNIQUE`, so even a logic bug could not record one
authorization twice.

---

## 5. Security features and where they live

| OWASP Top 10 (2025) | Threat | Defense | Code |
|---------------------|--------|---------|------|
| A01 Broken Access Control | Reading another user's receipt / history | Ownership is part of every query (`payer_id = $me OR payee_id = $me`); unknown and foreign receipts both return 404 and are logged | [`src/payments/history.cpp`](src/payments/history.cpp), [`src/web/routes.cpp`](src/web/routes.cpp) `get_receipt` |
| A01 | Customers using merchant pages | Role check in the request guard | [`src/web/guard.cpp`](src/web/guard.cpp) `guarded` |
| A01 / CSRF | Cross-site form posts | Per-session synchronizer token on every POST (constant-time compare); double-submit token on the login form; `SameSite=Strict` cookies; `form-action 'self'` | [`src/web/guard.cpp`](src/web/guard.cpp), [`src/auth/csrf.cpp`](src/auth/csrf.cpp), `post_login` in [`routes.cpp`](src/web/routes.cpp) |
| A02 Security Misconfiguration | Verbose errors, clickjacking, MIME sniffing | Generic error pages (details only in server log); CSP, `X-Frame-Options`, `nosniff`, `Referrer-Policy`, HSTS; DB not exposed outside Docker; container read-only, non-root, no capabilities | [`src/web/guard.cpp`](src/web/guard.cpp) `apply_security_headers`, [`docker-compose.yml`](docker-compose.yml) |
| A04 Cryptographic Failures | Sniffed traffic, stolen DB | HTTPS with TLS ≥ 1.2; Argon2id password hashes; only SHA-256 hashes of session tokens stored; QR key kept outside DB and source | [`src/main.cpp`](src/main.cpp), [`src/auth/password.cpp`](src/auth/password.cpp), [`src/auth/session.cpp`](src/auth/session.cpp) |
| A05 Injection (SQL) | `' OR 1=1 --` in any field | Every query is parameterized through `db::exec` (`$1`, `$2` …) — SQL text and data travel separately | [`src/db/pool.h`](src/db/pool.h) |
| A05 Injection (XSS) | `<script>` in names / inputs | Mustache `{{ }}` HTML-escapes all output (no `{{{ }}}` anywhere); JS uses `textContent` only; CSP forbids inline scripts | [`templates/`](templates), [`static/js/scan.js`](static/js/scan.js) |
| A06 Insecure Design | Duplicate charges, overspending, edited QR | Single-use 2-minute authorization; row locks + funds check inside one transaction; HMAC-signed QR; merchant name confirmation | [`src/payments/transfer.cpp`](src/payments/transfer.cpp), [`src/qr/qr_token.cpp`](src/qr/qr_token.cpp) |
| A07 Authentication Failures | Password guessing, session theft | 5 failures → 15-minute lock; 20 attempts / 10 min per IP; generic failure message; timing-equalized unknown accounts; 30-min idle + 12-h absolute session expiry; new session on login; server-side logout; `__Host-` `Secure; HttpOnly; SameSite=Strict` cookie | [`src/auth/login.cpp`](src/auth/login.cpp), [`src/auth/session.cpp`](src/auth/session.cpp) |
| A08 Integrity Failures | Forged QR codes | HMAC-SHA512-256 tag over `scanpay:v1:<merchant_id>`; strict parser; URL payloads rejected | [`src/qr/qr_token.cpp`](src/qr/qr_token.cpp) |
| A09 Logging & Alerting | Undetected attacks | JSON security log: login failures, lockouts, CSRF/QR rejections, access denials, replays — never passwords, tokens or full phone numbers | [`src/security_log.cpp`](src/security_log.cpp) |
| A10 Mishandling of Exceptional Conditions | Errors leaving money half-moved or leaking internals | Every handler runs inside the guard's exception boundary (generic 500 page); transfers use libpqxx transactions that roll back on any exception (tested with an injected crash after the debit) | [`src/web/guard.cpp`](src/web/guard.cpp), [`src/payments/transfer.cpp`](src/payments/transfer.cpp) |
| Input validation | Malformed / oversized input | Amounts parsed to integer cents with no floats (`$0.01`–`$500.00`, ≤ 2 decimals); `+1` + 10-digit phones; UUID format; 16 KB body limit; DB `CHECK` constraints as a second layer | [`src/money.cpp`](src/money.cpp), [`src/validate.cpp`](src/validate.cpp), [`db/schema.sql`](db/schema.sql) |
| Database security | App compromise rewriting the ledger | App connects as `scanpay_app`: no DDL, no DELETE on users / authorizations / transactions | [`db/grants.sql`](db/grants.sql) |
| Memory safety (C++) | Overflows, UB | Standard containers + RAII only, checked `from_chars` conversions, `-fstack-protector-strong`, `_FORTIFY_SOURCE`, ASan/UBSan test runs, Cppcheck | [`CMakeLists.txt`](CMakeLists.txt) |

Security events can be watched live with `docker compose exec app tail -f /app/logs/security.log`.

### Security demonstrations (all against your own local instance)

1. **Brute force:** sign in as Chloe (`+15555550103`) with a wrong password five times; the sixth attempt with the
   correct password `chloe-demo-2026` still fails. The log shows `account_locked`.
2. **Tampered QR:** copy a merchant's QR text from `/merchant/qr`, change `scanpay:v1:1:` to `scanpay:v1:2:` and paste
   it on the Scan page → "not a valid ScanPay merchant code", logged as `qr_rejected` / `bad_signature`.
3. **Replay:** open the browser devtools on the confirm page and resubmit the confirm form, or run the 20× concurrency test;
   only one transaction exists.
4. **XSS:** enter `"><script>alert(1)</script>` as an amount → it is shown back as inert escaped text.
5. **Broken access control:** pay something as Alice and note the receipt URL (e.g. `/receipts/1`); sign in as Ben and
   open the same URL → 404, logged as `access_denied`.
6. **CSRF:** remove the hidden `csrf_token` field in devtools and submit any form → 403.

### OWASP ZAP baseline scan (own instance only)

```bash
docker run --rm --network scanpay_default ghcr.io/zaproxy/zaproxy:stable \
  zap-baseline.py -t https://app:8443/login -r zap-report.html
```

Only scan your own local ScanPay instance. Never scan USM systems, public sites or other students' projects.

---

## 6. Project layout

```
src/
  main.cpp                 server startup, TLS
  config.*                 environment configuration (secrets)
  crypto.*                 libsodium helpers (random tokens, SHA-256, constant-time compare)
  money.*  validate.*      input validation, integer-cent money
  security_log.*           JSON security event log
  db/pool.*                connection pool + parameterized query helper
  auth/                    password (Argon2id), session, csrf, rate_limit, login (lockout)
  qr/                      qr_token (HMAC-signed payloads), qr_svg (QR rendering)
  payments/                authorization, transfer (atomic confirm), history (receipts, masking)
  web/                     guard (session/CSRF/role/headers), routes, views, cookies, services
templates/                 Mustache pages
static/                    CSS, scan.js, countdown.js, vendored jsQR
db/                        schema.sql, grants.sql, init/ (Docker initialization)
tools/seed.cpp             demo account seeding
tests/unit, tests/integration
scripts/                   make-env.sh, init-db.sh, gen-cert.sh
```

---

## 7. Known limitations and remaining risks

- **The printed QR carries no sale price.** The customer types the amount, so a customer could pay less than owed.
  ScanPay shows merchants every incoming amount in their history; the merchant must check it before handing over goods.
- **A valid QR can be physically replaced** by another merchant's valid QR. Customers must check the verified merchant
  name on the review screen before paying.
- **Stolen database:** an attacker would get fictional names, phones, balances, history and Argon2id hashes
  (offline guessing of weak passwords remains possible). Other fields are not encrypted at the application level.
  Session tokens are stored only as hashes, and the QR key is not in the database.
- Rate limits are in memory, so they reset when the server restarts and are per server instance.
- The certificate is self-signed for local demos; a real deployment would use a CA-issued certificate.
- Out of scope by design: real money, banks, SMS verification, refunds, public registration, mobile apps.
