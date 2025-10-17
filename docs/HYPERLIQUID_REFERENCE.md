# Hyperliquid Exchange Reference (Adapter + Low‑Latency Guide)

This document is a practical reference for building and operating the Hyperliquid exchange adapter with a focus on fast, deterministic execution. It complements the Exchange Adapter Architecture Plan and keeps us aligned with trading_core clean architecture.

Scope
- Targets Perpetuals and Spot trading via HTTP and WebSocket.
- Focuses on order/cancel/modify flows, streaming updates, and latency posture.
- Leaves tick/lot enforcement to trading_core. The adapter should format values correctly to avoid venue rejections but must not duplicate core enforcement.

Credentials & Env
- Hyperliquid uses a user/subaccount wallet address and an agent wallet private key instead of API keys.
- The engine’s credential resolver accepts either CLI or env:
  - `LATENTSPEED_HYPERLIQUID_USER_ADDRESS` (0x… lowercased)
  - `LATENTSPEED_HYPERLIQUID_PRIVATE_KEY` (hex)
  - `LATENTSPEED_HYPERLIQUID_USE_TESTNET=1|0`
- CLI `--api-key` maps to the wallet address; `--api-secret` maps to the private key.

Base Endpoints
- REST (Mainnet): `https://api.hyperliquid.xyz`
- REST (Testnet): `https://api.hyperliquid-testnet.xyz`
- WebSocket (Mainnet): `wss://api.hyperliquid.xyz/ws`
- WebSocket (Testnet): `wss://api.hyperliquid-testnet.xyz/ws`
- Info (read): `POST /info`
- Trading (signed actions): `POST /exchange`
- WS “post” (send Info/Action via WS): wrap payloads in `{ method: "post", id, request: { type: "info"|"action", payload } }`

Symbols, Assets, Naming
- Perps `asset` is an integer index from `meta.universe` (via `/info`), e.g. BTC may be 0 on mainnet.
- Spot `asset` is `10000 + index` where `index` is the spot pair index in `spotMeta.universe` (e.g. `PURR/USDC` → 10000 if it’s index 0).
- Builder‑deployed perps: `asset = 100000 + perp_dex_index * 10000 + index_in_meta` (e.g., `test:ABC` on testnet → 110000).
- Coin strings differ: perps use coin name (e.g., `BTC`), spot often uses `@<index>` (e.g., `@107`). UIs can remap names (e.g., `UBTC/USDC` vs `BTC/USDC`). Always resolve assets from `meta`/`spotMeta`.

Precision, Tick & Lot (formatting only)
- Perps prices: up to 5 significant figures, and no more than `6 - szDecimals` decimal places. Integer prices always allowed.
- Spot prices: up to 5 significant figures, and no more than `8 - szDecimals` decimal places.
- Sizes must be rounded to the asset’s `szDecimals`.
- Do not left‑pad/keep trailing zeros in signed numbers. Use canonical string formatting (see Signing).
- Note: trading_core enforces step/tick; adapter should format to venue’s accepted shape to avoid rejections.

Trading Actions (HTTP or WS post)
- Place Order
  - Body: `{ action: { type: "order", orders: [{ a,b,p,s,r,t,c? }], grouping: "na"|"normalTpsl"|"positionTpsl", builder? }, nonce, signature, vaultAddress?, expiresAfter? }`
  - Keys: `a` asset (int), `b` isBuy (bool), `p` price (string), `s` size (string), `r` reduceOnly (bool), `c` cloid (128‑bit hex), `t` type.
  - Type `t`: `{"limit": {"tif": "Alo"|"Ioc"|"Gtc"}}` or `{"trigger": {"isMarket": bool, "triggerPx": string, "tpsl": "tp"|"sl"}}`.
- Cancel: `{ action: { type: "cancel", cancels: [{ a, o /*oid*/ }] }, nonce, signature }`.
- Cancel by cloid: `{ action: { type: "cancelByCloid", cancels: [{ asset, cloid }] }, ... }`.
- Modify: `{ action: { type: "modify", oid|cloid, order: { a,b,p,s,r,t,c? } }, ... }`.
- Batch modify: `{ action: { type: "batchModify", modifies: [{ oid|cloid, order }] }, ... }`.
- Dead man’s switch: `{ action: { type: "scheduleCancel", time? } }` to schedule or clear.
- Nonce invalidation (noop): `{ action: { type: "noop" } }` — marks nonce as used; handy to cancel in‑flight batches.
- Extras (optional): `updateLeverage`, `updateIsolatedMargin`, `twapOrder`, `twapCancel`, transfers/approvals.

Info (HTTP or WS post)
- Open orders: `{ type: "openOrders", user }` or `{ type: "frontendOpenOrders", user }`.
- Order status by `oid`/`cloid`: `{ type: "orderStatus", user, oid }`.
- Fills: `{ type: "userFills", user, aggregateByTime? }` or time‑bounded variant.
- L2 book: `{ type: "l2Book", coin, nSigFigs?, mantissa? }` (≤20 levels per side).
- Candles: `{ type: "candleSnapshot", req: { coin, interval, startTime, endTime? } }`.

WebSocket Streaming
- Subscribe: `{ method: "subscribe", subscription: { type: <feed>, ... } }`.
- Key feeds for adapter reconciliation:
  - `orderUpdates` (WsOrder[]) — order state transitions with timestamps and cloid.
  - `userEvents` — fills/funding/liquidations/nonUserCancel.
  - `userFills` — snapshot + stream with `isSnapshot` marker.
- Market data: `trades`, `l2Book`, `bbo`, `candle`, `allMids`.
- Post over WS: wrap Info/Action payloads in `{ method: "post", id, request: { type, payload } }` and correlate by `id`.
- Heartbeat: send `{ method: "ping" }` if quiet >60s; expect `{ channel: "pong" }`.

Signing, API Wallets, Nonces
- Use API (agent) wallets approved by the master account to sign; `vaultAddress` routes actions to subaccounts/vaults if needed.
- Nonces are per signer; maintain an atomic millisecond counter per process (unique, monotonic, within (T−2d, T+1d)).
- Batch orders every ~100ms; separate ALO and IOC batches for prioritization.
- Two signing schemes exist; trading actions use user‑signed action scheme. Replicate the Python SDK exactly.
- Critical formatting:
  - Remove trailing zeros in numeric strings before signing.
  - Keep msgpack field order identical to SDK.
  - Lowercase any address fields before signing/sending.
- API wallet pruning can occur on deregistration/expiry/empty‑funds — prefer fresh agent addresses over reusing old ones.

Rate Limits
- IP REST: 1200 weight/min. Actions weight = `1 + floor(batch_len/40)`.
- Info weights: `l2Book/allMids/orderStatus` ≈ 2; most others ≈ 20; `userRole` = 60. Some info endpoints scale weight with response size.
- WS limits: ≤100 connections, ≤1000 subs, ≤10 distinct users across user subscriptions, ≤2000 messages/min, ≤100 inflight posts.
- Address‑based: initial 10k action buffer; thereafter ≈ 1 action per 1 USDC of lifetime traded volume. Cancels have a higher ceiling to allow unwind.
- Reserve capacity: `reserveRequestWeight` action buys more request weight (costs USDC from perps balance).

Error Responses and Status Mapping
- Batched actions return per‑element statuses; pre‑validation errors can reject the whole batch (duplicate across elements for callbacks).
- Common errors to map to canonical reasons:
  - `Tick`, `MinTradeNtl` / `MinTradeSpotNtl`, `PerpMargin`, `ReduceOnly`, `BadAloPx`, `IocCancel`, `BadTriggerPx`, `MarketOrderNoLiquidity`, `InsufficientSpotBalance`, `Oracle`, `PerpMaxPosition`, plus open‑interest‑cap variants.
- Order status strings: `open`, `filled`, `canceled`, `triggered`, `rejected`, `marginCanceled`, `scheduledCancel`, `tickRejected`, etc. Ensure ReasonMapper translates these to trading_core enums.

Latency Playbook (Recommended)
- Prefer WS `post` for trading+info to avoid REST RTT; maintain a single, long‑lived connection.
- Batch placement every ~100ms; keep ALO and IOC separate; coalesce cancels.
- Use `noop` to invalidate pending nonces when racing cancels/modifies.
- Reconcile via `orderUpdates` and `userEvents`; avoid redundant polls; use `orderStatus` for gap repair only.
- Keep signer‑local nonce counter; avoid cross‑process collision by using one agent wallet per process/subaccount.
- Production setup (optional for ultra‑low latency): run a non‑validating node with fast peers, or co‑locate with a reliable node; consider building local book from node outputs.

Adapter Integration Notes (Clean Architecture)
- All orders flow via `OrderManager` and the adapter; strategies interact only through `IStrategyContext`.
- Use a per‑venue `SymbolMapper` to resolve `asset` from `meta`/`spotMeta` and normalize `coin` forms.
- Use a per‑venue `ReasonMapper` table to translate HL errors/statuses to canonical codes.
- Keep adapter non‑blocking and idempotent; prefer cadence workers over per‑tick work where possible.
- Typed config via Pydantic `config_class`; no hardcoded URLs/keys; use env vars.
- Support capability flags: `supports_ws_post`, `supports_private_ws`, `supports_modify_by_cloid`.

Risks & Pitfalls (Avoid)
- Nonce misuse: duplicates, out‑of‑window nonces, or shared counters across processes.
- API wallet pruning/reuse: deregistration or expired agents can lead to replay exposure or signature mismatches.
- Wrong signing scheme or msgpack ordering; numeric formatting with trailing zeros; uppercase addresses.
- Asset mapping errors: confusing perps coin vs spot `@index`; builder‑perp asset math; UI name remaps.
- Tick/lot and price sig‑fig violations causing `Tick` or `minTrade` rejections.
- Post‑only immediate match (`BadAloPx`) and IOC no‑liquidity (`IocCancel`) — separate batches and validate bbo if needed.
- Treating pre‑validation errors like per‑item errors — entire batch may be rejected; duplicate for callback fan‑out.
- Hitting IP or address‑based limits; forgetting `reserveRequestWeight` or batching; sending redundant cancels after a definitive WS update.
- Over‑subscribing WS (user feeds across many users) — 10 distinct user cap; remember 60s heartbeat.
- Using agent wallet address to query account state (must use actual master/subaccount address).
- New accounts need activation fee (1 quote token) for first tx; plan funding before go‑live.

Minimal Adapter Checklist
- Network/base URL from config; no hardcodes.
- User (master/subaccount) address and separate API wallet per process.
- Nonce manager (monotonic ms, per signer); dead‑man switch path configured.
- HTTP and WS clients; WS `post` path enabled with request correlation and backoff.
- Symbol/asset resolver from `meta`/`spotMeta`; cache with TTL and invalidate on errors.
- ReasonMapper table; mapping for status strings and error codes.
- Number formatter (price/size) matching SDK rules; remove trailing zeros.
- Secure key management via env vars; no secrets in code.
- Testnet profile: separate URLs, assets, signing params.

References
- API root: For Developers → API: https://hyperliquid.gitbook.io/hyperliquid-docs/for-developers/api
- Exchange endpoint (actions): `/exchange`
- Info endpoint (queries): `/info`
- WebSocket, subscriptions, WS post, heartbeat, error responses, rate limits
- Python SDK (canonical signing behaviors): https://github.com/hyperliquid-dex/hyperliquid-python-sdk
