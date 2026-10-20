# FX Trading Bot

A low-latency foreign exchange trading bot with a C++ trading engine and a Java control plane. It trades **only on OANDA practice (demo) accounts** and never uses real money.

The project is **backend only**. All interaction (configuring bots, starting and stopping them, reviewing performance) happens through an authenticated API. A separate front-end project will consume that API later.

> **Status:** Planning. The decisions below are agreed; implementation starts at Phase 0.

---

## Goals

- **A working, fully automated bot** trading on an OANDA practice account.
- **Multi-user:** each user links their own OANDA demo account and runs their own bot.
- **Secure by default:** login is required for everything, and the system is designed to be hosted publicly.
- **Low latency as a learning goal:** a C++ hot path that is measured, benchmarked and published, with an honest split between what we control and what we don't.
- **A strong portfolio piece:** clear architecture, real metrics and a well-documented API.

### What "low latency" means here

Latency is reported as three separate numbers, because they have very different causes:

| Metric | What it covers | Target |
|---|---|---|
| **Internal tick-to-order** (p50 / p99) | Price message in → parse → state update → strategy → risk checks → order out, inside the engine process | < 50µs initially, < 10µs as a stretch goal |
| **Network round trip to OANDA** | Our host ↔ OANDA | Measure it; choose hosting to minimise it |
| **OANDA order acknowledgement** | OANDA's own processing | Measure it; outside our control |

Notes:
- **Internal latency is the part we control.** Single-digit microseconds is achievable in C++ by keeping allocations, locks, synchronous logging and I/O off the hot path.
- **End-to-end latency is network-bound.** Expect milliseconds from a nearby VPS and tens of milliseconds from a home connection. Being fast internally doesn't make a system HFT; HFT is defined by colocation and direct market access.
- **OANDA's price stream is conflated** (periodic snapshots, not every market tick). Strategies must not assume they see every price change.
- **Latency arbitrage against the broker is out of scope.** It is ineffective on simulated demo fills and against retail terms of service.

---

## Architecture

The system is split into a **data plane** (the latency-sensitive trading path) and a **control plane** (users, auth, configuration, persistence).

```
                          ┌──────────────────────────────────────────┐
   Front end (future) ──▶ │  Control plane (Java / Spring Boot)      │
         REST + JWT       │  - Auth, users, bot config               │
                          │  - EngineLauncher (spawns/supervises)    │
                          │  - Performance API                       │
                          │  - Kafka consumers → TimescaleDB         │
                          └───────┬───────────────────────▲──────────┘
                     gRPC commands│                       │ Kafka events
                   (start/stop/   │                       │ (fills, orders,
                    config/health)▼                       │  P&L, risk trips)
          ┌─────────────────────────────────────────────────────────┐
          │  Engine process — one per user (C++)                    │
          │                                                         │
          │  OANDA price ─▶ parse ─▶ state ─▶ strategy ─▶ risk ─▶  │──▶ OANDA orders
          │  stream            (hot path, single thread)            │
          │                                                         │
          │  async queue ─▶ event publisher thread (Kafka)          │
          └─────────────────────────────────────────────────────────┘

          ┌─────────────────────────────────┐
          │  Market data recorder (C++)     │──▶ TimescaleDB (ticks)
          │  one shared instance            │
          └─────────────────────────────────┘
```

### Components

**Engine (C++), one process per user**
- Connects to that user's OANDA practice account: streams prices and places, amends and cancels orders.
- Runs the hot path on a single thread: parse → update state → strategy → pre-trade risk checks → order.
- Never touches the database, Kafka or synchronous logging on the hot path. Events go onto a lock-free queue and a separate thread publishes them.
- Exposes a gRPC server (its own port or Unix socket) for commands and health checks.

**Control plane (Java / Spring Boot)**
- REST API for the front end: auth, users, bot configuration, start/stop, performance.
- Manages engines through an **`EngineLauncher` interface**:
  ```java
  public interface EngineLauncher {
      EngineHandle start(UserId userId, EngineConfig config);
      void stop(UserId userId);
      EngineStatus status(UserId userId);
  }
  ```
  - `ProcessEngineLauncher` or `DockerEngineLauncher` comes first, for Docker Compose.
  - `KubernetesEngineLauncher` comes later, creating one pod per user via the fabric8 client.
  - The rest of the control plane never knows which implementation is in use.
- Consumes Kafka events and writes them to TimescaleDB.

**Market data recorder (C++), one shared instance**
- Prices are the same for every user, so ticks are recorded once rather than per engine.
- Batches writes to TimescaleDB off the hot path.

### Communication

| Channel | Technology | Used for | Why |
|---|---|---|---|
| Commands | **gRPC** | Start, stop, update config, reset circuit breaker, health | Needs an immediate typed response, including validation errors. The standard gRPC health-check service supports supervision. |
| Events | **Kafka** (KRaft mode, no ZooKeeper) | Fills, order updates, P&L snapshots, risk events | Durable and replayable, with multiple consumers (persistence, live front-end push, analytics). The control plane can restart without losing fills. |

Conventions:
- **One schema source:** all gRPC services and Kafka event schemas are defined in **Protobuf** in `/proto`, generating types for both C++ and Java.
- **Ordering:** Kafka messages are keyed by user or engine ID so each user's events stay in order within a partition.
- **Idempotency:** every event carries a unique ID, and consumers are idempotent (unique constraints in the database) because Kafka can redeliver.
- **Kafka is never on the hot path:** the C++ producer uses librdkafka with a C++ wrapper on a dedicated thread.

---

## Risk management

Each limit has a hard-coded **system ceiling**. Users can configure stricter values through the API, never looser ones. Halt states are **persisted**, so a crash and restart never silently clears them.

### Maximum position size: 5%
- Hard-coded ceiling of 5%.
- **To decide:** 5% of what (notional exposure vs equity, or margin used), and whether it applies per position or across all open positions.

### Daily loss circuit breaker: -3%
- **When it's checked:** P&L (realised plus unrealised, against start-of-day equity) is checked on **every tick and every fill**, not on a timer. Marking positions to market costs almost nothing.
- **Day boundary:** 17:00 New York time, the standard FX rollover.
- **What happens on trip:** halt, cancel resting orders, close open positions, publish a risk event, and stay halted until a user manually restarts via the API.

### Flash-crash guard
- Triggers when the latest price deviates from its 30-second rolling average by more than **N rolling standard deviations**, with a fixed floor of around 0.3–0.5% for major pairs. A fixed 4% threshold would effectively never fire on major FX pairs.
- Thresholds are per instrument.
- **What happens on trip:** pause new orders for 2 minutes and cancel resting orders.

### Additional guards
- **Spread widening:** pause when the spread exceeds X times its recent normal. This often fires before a price spike.
- **Stale data:** pause when no price update has arrived for N seconds.
- **Kill switch:** an API command that halts and flattens immediately.

---

## Data storage

**TimescaleDB**, a PostgreSQL extension, is used from the start. It keeps the same SQL, JDBC, Spring Data and Flyway setup as plain Postgres.

| Data | Storage | Retention |
|---|---|---|
| Raw ticks | Hypertable, compressed | 30–90 days (retention policy) |
| 1s / 1m candles | Continuous aggregates | Indefinite |
| Users, bot configs | Regular tables | Indefinite |
| Orders, fills, P&L snapshots, risk events | Regular tables (the audit trail behind the performance API) | Indefinite |

Notes:
- **Extension setup:** `CREATE EXTENSION timescaledb;` lives in the first Flyway migration.
- **Older history:** deeper backtesting history comes from OANDA's historical candles API rather than storing years of ticks.
- **Hosting:** most managed Postgres services, including AWS RDS, don't support the extension. Self-host it in Docker or Kubernetes, or use Timescale's hosted service.
- **Licensing:** compression and continuous aggregates are under the Timescale License, which is free for self-hosting this project.

---

## Security

- **Login is required for every endpoint.** JWT access and refresh tokens.
- **Password hashing:** Argon2.
- **OANDA API tokens:** encrypted at rest and never logged. In Kubernetes they are delivered to engine pods as Secrets.
- **Network:** TLS terminated at a reverse proxy (Caddy or nginx), plus rate limiting on auth and command endpoints.
- **Tenant isolation:** engines are isolated per user, and the control plane enforces that a user can only command their own engine.
- **CI checks:** dependency and vulnerability scanning.

---

## Tech stack

| Area | Choice |
|---|---|
| Engine | C++20 |
| Build and packages | CMake + vcpkg |
| Networking (engine) | Boost.Asio, Boost.Beast (HTTPS and streaming to OANDA) |
| JSON | simdjson (hot path), nlohmann/json (convenience) |
| Logging (engine) | Quill (asynchronous, low latency) |
| Testing and benchmarking | GoogleTest, Google Benchmark |
| Debugging | AddressSanitizer and UndefinedBehaviorSanitizer in CI |
| Control plane | Java, Spring Boot |
| Commands | gRPC + Protobuf |
| Events | Kafka (KRaft); librdkafka with a C++ wrapper in the engine |
| Database | TimescaleDB (PostgreSQL) + Flyway |
| Auth | JWT, Argon2 |
| Observability | Prometheus metrics (latency histograms, P&L, order counts), Grafana |
| Containers | Docker, Docker Compose, then Kubernetes (k3s) |
| CI | GitHub Actions |
| Broker | OANDA practice account (v20 API) |

---

## Repository layout (proposed)

```
/
├── README.md
├── proto/                    # Shared Protobuf: gRPC services + Kafka event schemas
├── engine/                   # C++ trading engine
│   ├── CMakeLists.txt
│   ├── vcpkg.json
│   ├── src/
│   │   ├── marketdata/       # OANDA stream client, parsing, normalisation
│   │   ├── gateway/          # OANDA order client, reconciliation
│   │   ├── core/             # Event loop, state, positions
│   │   ├── strategy/         # Strategy interface + implementations
│   │   ├── risk/             # Limits, circuit breaker, guards, kill switch
│   │   ├── events/           # Lock-free queue, Kafka publisher
│   │   └── rpc/              # gRPC server
│   ├── tests/
│   └── bench/
├── recorder/                 # C++ market data recorder (shares engine libraries)
├── control-plane/            # Java / Spring Boot
│   └── src/main/resources/db/migration/   # Flyway migrations
├── deploy/
│   ├── compose/              # docker-compose.yml for local dev
│   └── k8s/                  # Kubernetes manifests (Phase 8)
└── docs/                     # Architecture notes, latency results, API spec
```
