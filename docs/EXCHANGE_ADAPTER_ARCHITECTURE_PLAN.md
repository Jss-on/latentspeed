# Exchange Adapter Architecture Plan

Status: Draft v1
Owner: Core Execution Team
Scope: latentspeed (engine) only — no trading_core API changes

## 1) Objectives

- Make exchange support adapter‑first so venues (Bybit, Binance, dYdX, Hyperliquid, …) plug in without touching engine core.
- Keep trading_core clean architecture intact: strategies use IStrategyContext; all execution flows through OrderManager → ExecClient; engine remains a black‑box execution backend via ZMQ.
- Preserve the message contract (ExecutionOrder in, ExecutionReport/Fill out) and canonical reason codes.
- Centralize shared logic (REST/WS, auth, rate limiting, symbol/number formatting, error mapping) to eliminate per‑venue duplication.
- Add a venue capability model so the engine validates/shapes orders consistently before calling adapters.

Key non‑goals:
- Do not change trading_core Strategy API, OrderManager, schemas, or launcher.
- Do not bypass OrderManager risk or balance accounting.

## 2) Current State (Summary)

- TradingEngineService binds orders at `tcp://127.0.0.1:5601` and publishes events at `tcp://127.0.0.1:5602`. ExecClient in trading_core connects to these.
- Venue logic lives in concrete clients (e.g., `BybitClient`) called directly by the engine. Each client implements REST/WS, signing, mapping, and error handling ad‑hoc.
- Result: adding a new venue requires duplicating low‑level networking/auth and sprinkling venue details in engine code.

## 3) Target Architecture (Adapter‑First)

High‑level design:

- Introduce an `IExchangeAdapter` interface that accepts normalized, typed orders and returns standard results. Adapters encapsulate venue quirks.
- A `VenueRouter` resolves `ExecutionOrder.venue` to the appropriate adapter from an `ExchangeRegistry`.
- Shared services: `HttpClient` (TLS + pooling), `WsClient` (TLS + heartbeat), `IAuthProvider` (venue signing), `ISymbolMapper`, `InstrumentCatalog`, `IReasonMapper`.
- Engine continues to parse ZMQ messages per the existing contract; converts to normalized types; calls adapter; publishes canonical reports/fills.

### 3.1 Interfaces (C++ sketch)

```cpp
// Core normalized models (header-only DTOs)
struct NormalizedSymbol {
  std::string base;         // e.g., "ETH"
  std::string quote;        // e.g., "USDT"
  std::string settle;       // e.g., "USDT" (empty for spot)
  enum class Kind { spot, perp, option } kind{Kind::spot};
};

struct NormalizedOrder {
  NormalizedSymbol sym;
  std::string side;         // "buy" | "sell"
  std::string type;         // "limit" | "market" | "stop" | "stop_limit"
  std::string tif;          // "gtc" | "ioc" | "fok" | "post_only"
  double size{0.0};
  std::optional<double> price;
  std::optional<double> stop_price;
  bool reduce_only{false};
  std::map<std::string,std::string> params; // venue passthrough
};

struct CancelSpec {
  std::string cl_id_to_cancel;
  std::optional<NormalizedSymbol> sym;      // optional
  std::optional<std::string> exchange_order_id; // optional
};

struct ReplaceSpec {
  std::string cl_id_to_replace;
  std::optional<double> new_price;
  std::optional<double> new_size;
  std::optional<NormalizedSymbol> sym;      // optional
};

struct OrderQuery { std::string cl_id; };

struct ExchangeCapabilities {
  std::string venue; // "bybit", "binance", "dydx", ...
  bool spot{true};
  bool perpetual{false};
  bool options{false};
  bool post_only{true};
  bool reduce_only{true};
  bool hedge_mode{false};
  bool tif_gtc{true}, tif_ioc{true}, tif_fok{true};
  bool cancel_by_clid{true}, cancel_by_exoid{true};
  bool amend_supported{true};
  // … extend as needed
};

struct AdapterConfig {
  std::string api_key;
  std::string api_secret;
  bool testnet{false};
  std::map<std::string,std::string> opts; // e.g., host overrides
};

class IExchangeAdapter {
 public:
  using OrderUpdateFn = std::function<void(const latentspeed::OrderUpdate&)>;
  using FillFn       = std::function<void(const latentspeed::FillData&)>;
  using ErrorFn      = std::function<void(const std::string&)>;

  virtual ~IExchangeAdapter() = default;
  virtual std::string venue() const = 0;
  virtual const ExchangeCapabilities& caps() const = 0;

  virtual bool init(const AdapterConfig&) = 0;
  virtual bool connect() = 0;
  virtual void disconnect() = 0;
  virtual bool healthy() const = 0;

  virtual void on_order_update(OrderUpdateFn) = 0;
  virtual void on_fill(FillFn) = 0;
  virtual void on_error(ErrorFn) = 0;

  virtual latentspeed::OrderResponse place(const NormalizedOrder&) = 0;
  virtual latentspeed::OrderResponse cancel(const CancelSpec&) = 0;
  virtual latentspeed::OrderResponse replace(const ReplaceSpec&) = 0;
  virtual latentspeed::OrderResponse query(const OrderQuery&) = 0;

  virtual std::vector<latentspeed::OpenOrderBrief> list_open(/*optional filters*/) = 0;
};
```

### 3.2 Shared Services

- `HttpClient`: TLS/SNI, connection pooling, keep‑alive, retry/backoff, token‑bucket rate limiting.
- `WsClient`: TLS/SNI, backoff, ping/pong watchdog, topic subscription, backpressure, frame parsing helpers.
- `IAuthProvider`: venue‑specific signing (HMAC‑SHA256/512, Ed25519, EIP‑712, etc.).
- `ISymbolMapper`: ccxt‑style ⇄ venue symbol mapping; compact/hyphen conversions.
- Tick/lot enforcement remains the responsibility of trading_core's OrderManager. The engine does not re-enforce it to avoid divergence. (Instrument catalogs are not required here.)
- `IReasonMapper`: venue error/status → {status_normalized, reason_code_canonical, reason_text}.

### 3.3 Engine Flow (unchanged externally)

1) Receive `ExecutionOrder` (ZMQ PUSH) from trading_core ExecClient.
2) Parse into typed DTOs per the existing message contract (place/cancel/replace).
3) Convert DTOs → `NormalizedOrder|CancelSpec|ReplaceSpec` using `ISymbolMapper` + defaults.
4) Validate against `ExchangeCapabilities` (e.g., post_only valid only for limit; reduce_only only for perps).
5) Route to adapter; publish `ExecutionReport` (accepted/rejected) then later WS updates/fills via `IReasonMapper` and symbol normalization.

## 4) Adapter Development Model

Structure for a new venue (template):

```
latentspeed/
  adapters/
    bybit/
      bybit_adapter.h/.cpp   // wraps current BybitClient first
    binance/
      binance_adapter.h/.cpp // wraps current BinanceClient first
    dydx/
      dydx_adapter.h/.cpp    // new impl
    hyperliquid/
      hyperliquid_adapter.h/.cpp
  core/
    http_client.h/.cpp
    ws_client.h/.cpp
    auth/
      hmac_sha256_auth.h/.cpp
      ed25519_auth.h/.cpp
      eip712_auth.h/.cpp
    symbol/
      symbol_mapper.h/.cpp
    instrument_catalog.h/.cpp
    reason_mapper.h/.cpp     // data‑driven tables per venue
  engine/
    exchange_registry.h/.cpp
    venue_router.h/.cpp
```

Adapter checklist:
- Implement `caps()` truthfully.
- Map `NormalizedOrder` to venue payloads (including triggers/stop params when needed).
- Parse WS private order/execution streams → `OrderUpdate` / `FillData`.
- Use `InstrumentCatalog` for number formatting (tick/lot) and pre‑validation.
- Use `IReasonMapper` for canonicalization.
- Implement rehydrate/backfill hooks for post‑reconnect consistency.

## 5) Phased Implementation Plan

### Phase 1 — Introduce Abstractions, Wrap Existing Venues

- Add `IExchangeAdapter`, `ExchangeRegistry`, `VenueRouter` (minimal skeletons).
- Create `BybitAdapter` and `BinanceAdapter` that internally delegate to current clients.
- Keep `TradingEngineService` logic but swap direct client calls for `VenueRouter`.
- No functional change; feature‑flag path if needed.

Deliverables:
- New interfaces and router wired in.
- Engine builds and runs with Bybit/Binance through adapters.
- Message contract unchanged; ports configurable.

Acceptance:
- Place/cancel/replace/query behave as before on Bybit/Binance.
- Reports/fills identical (modulo field ordering in JSON logs).

### Phase 2 — Extract Shared Net/Auth + Typed Parsing

- Extract `HttpClient`, `WsClient` (stub), and `IAuthProvider` (Bybit pilot) from existing clients.
- Add `ISymbolMapper` for symbol normalization. Tick/lot validation continues in trading_core.
- Replace stringly `parse_execution_order_hft` with typed DTO → `NormalizedOrder` pipeline.

Deliverables:
- Shared components in `core/`.
- Bybit adapter updated to support HttpClient + AuthProvider for REST behind `LATENTSPEED_USE_HTTP_CLIENT` (default off).
- Engine uses `DefaultSymbolMapper` and `DefaultReasonMapper`. No tick/lot enforcement in engine.

Acceptance:
- No behavior regression in live smoke tests.
- Unit tests for symbol mapping and reason mapping hit common edge cases.

Flags:
- `LATENTSPEED_USE_HTTP_CLIENT=0|1` (default 0) — pilot Bybit REST via HttpClient.

### Phase 3 — Add dYdX + Hyperliquid Adapters

- Implement `dydx_adapter` using Ed25519/EIP‑712 signing and private WS streams.
- Implement `hyperliquid_adapter` with HMAC auth and perps support.
- Add Adapter TCK (Test Compatibility Kit) with sandbox fixtures.

Deliverables:
- Two new adapters with minimal viable feature set (market/limit; cancel; replace/amend if supported; query; rehydrate; fills).

Acceptance:
- TCK green for both new adapters.
- E2E: engine accepts ExecutionOrder and publishes normalized reports/fills for each venue.

## 6) Message Contract & Canonicalization

- Continue to follow `trading_core/docs/Message Contract for Execution & Backtest.md`.
- Maintain canonical reason codes: `ok, invalid_params, risk_blocked, venue_reject, insufficient_balance, post_only_violation, min_size, price_out_of_bounds, rate_limited, network_error, expired`.
- Status normalization: `accepted, rejected, canceled, replaced`.
- Ensure symbols in outbound fills/reports use hyphen format for trading_core (e.g., `ETH-USDT`), keeping consistency with existing engine behavior.

## 7) Configuration

- Engine config extended with a `venues` map (optional in Phase 1):
  - `venues: { bybit: { api_key, api_secret, testnet, host?, ws_host? }, dydx: { ... }, ... }`
- Preserve environment overrides (e.g., `LATENTSPEED_BYBIT_API_KEY`).
- Make ZMQ ports explicit/configurable to avoid clashes with market data endpoints; defaults remain `5601/5602`.

## 8) Testing Strategy

- Unit: `ISymbolMapper`, `IReasonMapper`, `InstrumentCatalog`, DTO parsing, capability validation.
- Adapter TCK: recorded fixtures for private WS → `OrderUpdate`/`FillData` mapping, plus REST stubs for place/cancel/replace/query.
- Integration: spin engine with each adapter on testnet/sandbox and send a minimal order matrix.
- Regression: diff JSON of reports/fills vs. current engine for Bybit/Binance.

## 9) Risks & Mitigations

- Regression risk while moving to shared net/auth: mitigate via Phase 1 wrappers + Phase 2 incremental swaps + regression diffing.
- Symbol normalization inconsistencies: centralize in `ISymbolMapper` + add table‑driven symbol tests.
- Reason code drift: data‑driven mapping tables and tests per venue.
- Backpressure/WS drops: standardize `WsClient` heartbeat, backoff, and cursor‑based backfill.

## 10) Rollout & Backward Compatibility

- Phase 1 keeps behavior stable; adapter wrappers call existing clients.
- Add a feature flag `use_adapter_router=true` (default on after bake‑in).
- Retire direct client path once Phase 2 lands and regression is clean.

## 11) Open Questions

- dYdX auth: finalize Ed25519 vs. EIP‑712 shape for private WS/REST.
- Position/hedge mode abstraction: unify via `ExchangeCapabilities` + `InstrumentCatalog`.
- Multi‑account support per venue in a single engine instance.

## 12) Milestone Checklist

Phase 1
- [ ] IExchangeAdapter, ExchangeRegistry, VenueRouter skeletons
- [ ] BybitAdapter/BinanceAdapter wrapping existing clients
- [ ] Engine uses router; unit/integration smoke green

Phase 2
- [ ] HttpClient, WsClient, IAuthProvider
- [ ] ISymbolMapper, InstrumentCatalog, IReasonMapper
- [ ] Typed DTO parsing; behavior parity

Phase 3
- [ ] dYdX Adapter + TCK
- [ ] Hyperliquid Adapter + TCK
- [ ] E2E validations and docs

## 13) Appendix: Example End‑to‑End (Place → Ack → Fill)

1) trading_core ExecClient sends `ExecutionOrder` (place, cex, perpetual, details.ccxt‑style symbol).
2) Engine parses → `NormalizedOrder` via `ISymbolMapper`.
3) `VenueRouter` → `BybitAdapter::place()` → REST create.
4) Engine publishes `ExecutionReport{status=accepted, reason_code=ok}`.
5) Private WS `execution` event → adapter → engine callback → publish `Fill` with normalized hyphen symbol.
