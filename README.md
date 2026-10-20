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

---

## Phased plan

Each phase ends with something runnable and demonstrable.

### Phase 0: Foundations
- Monorepo structure as above.
- C++ toolchain: CMake + vcpkg, a "hello world" engine with a GoogleTest target and a Google Benchmark target.
- GitHub Actions CI: build, tests, ASan/UBSan.
- Docker Compose skeleton: TimescaleDB and Kafka (KRaft).
- An empty `/proto` folder with code generation wired up for C++ and Java.
- C++ refresher alongside: RAII, move semantics, smart pointers, value vs reference semantics (vs Java).

**Done when:** `docker compose up` brings up the infrastructure, and CI is green on the C++ skeleton.

### Phase 1: Market data
- Connect to the OANDA practice pricing stream with Boost.Beast (HTTPS + streaming).
- Parse with simdjson into an internal, allocation-free price struct.
- Timestamp at receive and after processing, with an internal latency histogram.

**Done when:** live prices stream into the engine and p50/p99 parse latency is printed.

### Phase 2: Recorder and replay
- Recorder process writes ticks to a TimescaleDB hypertable in batches, off the hot path.
- Retention policy and continuous aggregates for 1s/1m candles.
- Replay harness feeds recorded ticks through the engine deterministically.

**Done when:** a recorded session can be replayed, producing identical results and repeatable benchmarks.

### Phase 3: Engine core and risk
- Single-threaded event loop, plus position and order state.
- Strategy interface.
- Risk module: position ceiling, daily loss circuit breaker, flash-crash guard, spread and stale-data guards, kill switch.
- Persisted halt state.
- Lock-free queue to an event publisher thread, and Kafka producer.

**Done when:** every risk rule has unit tests and replay-driven tests that trip it.

### Phase 4: Order gateway
- Place, amend and cancel orders on the OANDA practice account.
- Reconcile internal state with the broker's view on startup and periodically.
- Fills and order updates published to Kafka.

**Done when:** the engine places and closes a demo trade, and its state matches OANDA's.

### Phase 5: First strategy
- A simple, realistic strategy (momentum or mean reversion on the price stream).
- Cross-rate (triangular) mispricing detector that logs opportunities.
- Validate in replay first, then run live on demo.

**Done when:** the bot trades autonomously on demo with all risk guards active.

### Phase 6: Control plane
- Spring Boot service: users, JWT auth, Argon2, encrypted OANDA tokens.
- `EngineLauncher` interface plus a `ProcessEngineLauncher` or `DockerEngineLauncher` implementation.
- gRPC client for commands and health supervision.
- Kafka consumers persisting events idempotently.
- REST endpoints for bot config (validated against the system ceilings), start/stop/kill, and performance (P&L, win rate, drawdown, latency stats).
- OpenAPI spec published for the future front end.

**Done when:** a new user can register, link a demo account, configure and start a bot, and query its performance, all via the API.

### Phase 7: Observability, hardening and deployment
- Prometheus metrics and Grafana dashboards: latency histograms, P&L, orders, risk events.
- Security review: rate limiting, TLS via reverse proxy, secrets handling, dependency scanning.
- Deploy with Docker Compose to a VPS. Measure the network round trip to OANDA from candidate regions and pick the lowest.
- Publish latency results (internal p50/p99, network RTT, broker acknowledgement) in `docs/`.

**Done when:** the system runs publicly behind login with dashboards and published latency numbers.

### Phase 8: Kubernetes migration
- Single-node k3s on the VPS.
- `KubernetesEngineLauncher` using the fabric8 client: one engine pod per user, created and deleted by the control plane.
- gRPC health checks as liveness and readiness probes; OANDA tokens as Secrets.
- Kafka via the Strimzi operator; TimescaleDB as a StatefulSet with persistent volumes; ingress plus TLS certificates.
- Low-latency tuning: Guaranteed QoS plus the static CPU manager policy to pin engine pods to dedicated cores.

**Done when:** engines run as per-user pods managed entirely by the control plane, with no code changes outside the new launcher.

### Phase 9 (optional): Private live execution trial
This phase tests real-money **execution**, not prices: OANDA's practice stream already carries live market prices. The aim is to measure real fills, slippage, rejections and costs against demo.
 
**Prerequisites**
- Phases 0–7 complete.
- Weeks of stable running on demo.
- Every risk guard proven, both in replay and by real trips on demo.
- Reconciliation working reliably.
**Account and access**
- A separate OANDA live account, funded only with an amount you're fully prepared to lose.
- Confirm negative balance protection with OANDA before funding.
- **Private only:** live mode is never available in the public, multi-user deployment. It runs solely under the owner's private configuration.
**Live-only safeguards** (on top of the percentage-based risk rules)
- Absolute £ daily loss limit.
- Maximum order size in units.
- Maximum orders per minute.
- Duplicate-order detection.
**Enabling live mode**
- Off by default. It requires an explicit config flag plus a separate live API token.
- The engine logs live mode prominently at startup and publishes a risk event.
**Rollout**
1. Shadow mode: generate live decisions and log the orders that *would* have been placed, without sending them.
2. Minimal size: place real orders at the smallest permitted units.
3. Write-up: compare live vs demo slippage, fill rates and costs in `docs/`.

**Done when:** a documented live execution trial exists with real slippage and fill statistics compared against demo.
 
---

## Open questions

- **Position limit definition:** is the 5% ceiling measured on notional or margin, and is it per position or total?
- **First strategy:** momentum, mean reversion, or something else.
- **Instrument list:** which pairs to trade and record initially.
- **Kafka topic design:** names, partition counts and retention.
- **Front-end live updates:** WebSocket or Server-Sent Events, fed from Kafka.
- **Key management:** how the key that encrypts OANDA tokens is itself stored and rotated.
- **Hosting:** provider and region, chosen by measured round trip to OANDA.
