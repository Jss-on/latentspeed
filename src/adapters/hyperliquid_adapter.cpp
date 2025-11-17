/**
 * @file hyperliquid_adapter.cpp
 * @brief Skeleton adapter for Hyperliquid implementing IExchangeAdapter (Phase 3 - M1 scaffold).
 */

#include "adapters/hyperliquid_adapter.h"
#include "adapters/hyperliquid_asset_resolver.h"
#include "adapters/hyperliquid_nonce.h"
#include "adapters/hyperliquid_signer.h"
#include "core/net/http_client.h"
#include "core/net/hl_ws_post_client.h"
#include "core/symbol/symbol_mapper.h"
#include "core/util/num_string.h"
#include "adapters/python_hl_signer.h"
#include <spdlog/spdlog.h>
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <cctype>
#include <optional>
#include <sstream>

namespace {
inline std::string to_lower_ascii(std::string_view s) {
    std::string out(s.begin(), s.end());
    for (auto& c : out) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

inline bool starts_with(std::string_view s, std::string_view prefix) {
    return s.rfind(prefix, 0) == 0;
}

inline bool is_hex_char(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

inline bool is_valid_cloid(std::string_view s) {
    if (!starts_with(s, "0x")) return false;
    if (s.size() != 34) return false; // 0x + 32 hex chars
    for (size_t i = 2; i < s.size(); ++i) if (!is_hex_char(s[i])) return false;
    return true;
}

inline std::string map_tif_to_hl(std::optional<std::string> tif_in) {
    if (!tif_in.has_value() || tif_in->empty()) return std::string{"Gtc"};
    std::string t = to_lower_ascii(*tif_in);
    if (t == "gtc") return std::string{"Gtc"};
    if (t == "ioc") return std::string{"Ioc"};
    if (t == "po" || t == "post_only" || t == "postonly" || t == "alo") return std::string{"Alo"};
    return std::string{"Gtc"};
}

inline void parse_base_quote_from_hyphen(const std::string& hyphen, std::string& base, std::string& quote) {
    auto first_dash = hyphen.find('-');
    if (first_dash == std::string::npos) { base = hyphen; quote.clear(); return; }
    base = hyphen.substr(0, first_dash);
    auto second_dash = hyphen.find('-', first_dash + 1);
    if (second_dash == std::string::npos) {
        quote = hyphen.substr(first_dash + 1);
    } else {
        quote = hyphen.substr(first_dash + 1, second_dash - first_dash - 1);
    }
}

struct BaseUrlParts { std::string scheme; std::string host; std::string port; };
inline BaseUrlParts parse_base_url(const std::string& url) {
    BaseUrlParts p{"https","", "443"};
    auto pos = url.find("://");
    std::string rest = url;
    if (pos != std::string::npos) { p.scheme = url.substr(0, pos); rest = url.substr(pos + 3); }
    auto slash = rest.find('/');
    std::string hostport = (slash == std::string::npos) ? rest : rest.substr(0, slash);
    auto colon = hostport.find(':');
    if (colon == std::string::npos) { p.host = hostport; p.port = (p.scheme == "http") ? "80" : "443"; }
    else { p.host = hostport.substr(0, colon); p.port = hostport.substr(colon + 1); }
    return p;
}
}

namespace latentspeed {

HyperliquidAdapter::HyperliquidAdapter() = default;
HyperliquidAdapter::~HyperliquidAdapter() = default;

bool HyperliquidAdapter::initialize(const std::string& api_key,
                                    const std::string& api_secret,
                                    bool testnet) {
    api_key_ = api_key;
    api_secret_ = api_secret;
    testnet_ = testnet;
    cfg_ = HyperliquidConfig::for_network(testnet_);
    resolver_ = std::make_unique<HyperliquidAssetResolver>(cfg_);
    nonce_mgr_ = std::make_unique<HyperliquidNonceManager>();
    // Prefer Python-backed signer bridge for correctness parity with SDK.
    const char* env_py = std::getenv("LATENTSPEED_HL_SIGNER_PYTHON");
    const char* env_script = std::getenv("LATENTSPEED_HL_SIGNER_SCRIPT");
    std::string py = env_py && *env_py ? std::string(env_py) : std::string("python3");
    std::string script = env_script && *env_script ? std::string(env_script) : std::string("tools/hl_signer_bridge.py");
    signer_ = std::make_unique<PythonHyperliquidSigner>(py, script);
    // M1 scaffold: do not attempt network/auth yet. Consider keys present as initialized.
    return true;
}

bool HyperliquidAdapter::connect() {
    // M5: Connect WS post client (optional)
    if (!ws_post_ && cfg_.supports_ws_post) {
        ws_post_ = std::make_unique<latentspeed::netws::HlWsPostClient>();
        connected_ = ws_post_->connect(cfg_.ws_url);
        if (connected_ && cfg_.supports_private_ws) {
            // Install message handler for private streams
            ws_post_->set_message_handler([this](const std::string& channel, const rapidjson::Document& doc){
                try {
                    if (!doc.HasMember("data")) return;
                    const auto& data = doc["data"];
                    if (channel == "orderUpdates" && data.IsArray()) {
                        for (auto& item : data.GetArray()) {
                            if (!item.IsObject()) continue;
                            OrderUpdate upd{};
                            // order block
                            if (item.HasMember("order") && item["order"].IsObject()) {
                                const auto& o = item["order"];
                                // oid
                                if (o.HasMember("oid") && o["oid"].IsUint64()) {
                                    upd.exchange_order_id = std::to_string(o["oid"].GetUint64());
                                }
                                if (o.HasMember("cloid") && o["cloid"].IsString()) {
                                    upd.client_order_id = o["cloid"].GetString();
                                }
                            }
                            if (item.HasMember("status") && item["status"].IsString()) {
                                std::string st = item["status"].GetString();
                                // Normalize HL statuses to engine-expected forms
                                if (st == "open") upd.status = "new";
                                else if (st == "filled") upd.status = "filled";
                                else if (st == "canceled" || st == "cancelled" || st == "marginCanceled" || st == "scheduledCancel") upd.status = "canceled";
                                else if (st == "triggered") upd.status = "accepted";
                                else if (st == "rejected") upd.status = "rejected";
                                else if (st.size() >= 8 && st.rfind("Rejected") == st.size() - 8) { upd.status = "rejected"; upd.reason = st; }
                                else { upd.status = st; }
                            }
                            if (item.HasMember("statusTimestamp") && item["statusTimestamp"].IsUint64()) {
                                upd.timestamp_ms = item["statusTimestamp"].GetUint64();
                            }
                            if (order_update_cb_) order_update_cb_(upd);
                        }
                    } else if (channel == "userEvents" && data.IsObject()) {
                        if (data.HasMember("fills") && data["fills"].IsArray()) {
                            for (auto& f : data["fills"].GetArray()) {
                                if (!f.IsObject()) continue;
                                FillData fill{};
                                // oid
                                if (f.HasMember("oid") && f["oid"].IsUint64()) fill.exchange_order_id = std::to_string(f["oid"].GetUint64());
                                // side
                                if (f.HasMember("side") && f["side"].IsString()) {
                                    std::string s = f["side"].GetString();
                                    fill.side = (s == "B") ? "buy" : "sell";
                                }
                                if (f.HasMember("px") && f["px"].IsString()) fill.price = f["px"].GetString();
                                if (f.HasMember("sz") && f["sz"].IsString()) fill.quantity = f["sz"].GetString();
                                if (f.HasMember("fee") && f["fee"].IsString()) fill.fee = f["fee"].GetString();
                                if (f.HasMember("feeToken") && f["feeToken"].IsString()) fill.fee_currency = f["feeToken"].GetString();
                                if (f.HasMember("time") && f["time"].IsUint64()) fill.timestamp_ms = f["time"].GetUint64();
                                if (f.HasMember("tid") && (f["tid"].IsUint64() || f["tid"].IsInt64())) fill.exec_id = std::to_string(f["tid"].GetUint64());
                                // liquidity from crossed
                                if (f.HasMember("crossed") && f["crossed"].IsBool()) fill.liquidity = f["crossed"].GetBool() ? "taker" : "maker";
                                // symbol mapping: coin string -> hyphen form if possible
                                if (f.HasMember("coin") && f["coin"].IsString()) {
                                    std::string coin = f["coin"].GetString();
                                    std::string sym_hyphen = coin;
                                    if (!coin.empty() && coin[0] == '@') {
                                        int idx = std::atoi(coin.c_str() + 1);
                                        if (auto pair = resolver_ ? resolver_->resolve_spot_pair_by_index(idx) : std::nullopt) {
                                            sym_hyphen = pair->first + std::string("-") + pair->second;
                                        }
                                    }
                                    fill.symbol = sym_hyphen;
                                }
                                if (fill_cb_) fill_cb_(fill);
                            }
                        } else if (data.HasMember("nonUserCancel") && data["nonUserCancel"].IsArray()) {
                            for (auto& c : data["nonUserCancel"].GetArray()) {
                                if (!c.IsObject()) continue;
                                OrderUpdate upd{};
                                if (c.HasMember("oid") && c["oid"].IsUint64()) upd.exchange_order_id = std::to_string(c["oid"].GetUint64());
                                upd.status = "canceled";
                                upd.reason = "nonUserCancel";
                                upd.timestamp_ms = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
                                if (order_update_cb_) order_update_cb_(upd);
                            }
                        }
                    } else if (channel == "userFills" && data.IsObject()) {
                        // Snapshot + stream, same WsFill shape
                        if (data.HasMember("fills") && data["fills"].IsArray()) {
                            for (auto& f : data["fills"].GetArray()) {
                                if (!f.IsObject()) continue;
                                FillData fill{};
                                if (f.HasMember("oid") && f["oid"].IsUint64()) fill.exchange_order_id = std::to_string(f["oid"].GetUint64());
                                if (f.HasMember("side") && f["side"].IsString()) fill.side = (std::string(f["side"].GetString()) == "B") ? "buy" : "sell";
                                if (f.HasMember("px") && f["px"].IsString()) fill.price = f["px"].GetString();
                                if (f.HasMember("sz") && f["sz"].IsString()) fill.quantity = f["sz"].GetString();
                                if (f.HasMember("fee") && f["fee"].IsString()) fill.fee = f["fee"].GetString();
                                if (f.HasMember("feeToken") && f["feeToken"].IsString()) fill.fee_currency = f["feeToken"].GetString();
                                if (f.HasMember("time") && f["time"].IsUint64()) fill.timestamp_ms = f["time"].GetUint64();
                                if (f.HasMember("tid") && (f["tid"].IsUint64() || f["tid"].IsInt64())) fill.exec_id = std::to_string(f["tid"].GetUint64());
                                if (f.HasMember("crossed") && f["crossed"].IsBool()) fill.liquidity = f["crossed"].GetBool() ? "taker" : "maker";
                                if (f.HasMember("coin") && f["coin"].IsString()) {
                                    std::string coin = f["coin"].GetString();
                                    std::string sym_hyphen = coin;
                                    if (!coin.empty() && coin[0] == '@') {
                                        int idx = std::atoi(coin.c_str() + 1);
                                        if (auto pair = resolver_ ? resolver_->resolve_spot_pair_by_index(idx) : std::nullopt) {
                                            sym_hyphen = pair->first + std::string("-") + pair->second;
                                        }
                                    }
                                    fill.symbol = sym_hyphen;
                                }
                                if (fill_cb_) fill_cb_(fill);
                            }
                        }
                    }
                } catch (const std::exception& e) {
                    spdlog::warn("[HL-WS] handler error: {}", e.what());
                }
            });
            // Subscribe to private streams
            const std::string user = util::to_lower_hex_address(api_key_);
            ws_post_->subscribe("orderUpdates", {{"user", user}});
            ws_post_->subscribe("userEvents", {{"user", user}});
            ws_post_->subscribe_with_bool("userFills", {{"user", user}}, {{"aggregateByTime", false}});
        }
        return connected_;
    }
    connected_ = (ws_post_ && ws_post_->is_connected());
    return connected_;
}

void HyperliquidAdapter::disconnect() {
    connected_ = false;
}

bool HyperliquidAdapter::is_connected() const { return connected_; }

OrderResponse HyperliquidAdapter::place_order(const OrderRequest& request) {
    const std::string ot = to_lower_ascii(request.order_type);
    if (ot != "limit") {
        return {false, "hyperliquid: only limit orders supported (market/trigger not yet implemented)", std::nullopt, request.client_order_id, std::nullopt, {}};
    }

    const bool is_spot = (request.category.has_value() && to_lower_ascii(*request.category) == std::string{"spot"});

    DefaultSymbolMapper mapper;
    std::string hyphen = mapper.to_hyphen(request.symbol, !is_spot);
    std::string base, quote; parse_base_quote_from_hyphen(hyphen, base, quote);
    if (base.empty()) {
        return {false, "hyperliquid: unable to parse symbol base/quote", std::nullopt, request.client_order_id, std::nullopt, {}};
    }

    std::optional<HlResolution> res;
    if (is_spot) {
        if (quote.empty()) return {false, "hyperliquid: spot order requires BASE-QUOTE pair", std::nullopt, request.client_order_id, std::nullopt, {}};
        res = resolver_ ? resolver_->resolve_spot(base, quote) : std::nullopt;
    } else {
        res = resolver_ ? resolver_->resolve_perp(base) : std::nullopt;
    }
    if (!res || res->asset < 0) {
        if (resolver_) resolver_->refresh_all();
        if (is_spot) res = resolver_->resolve_spot(base, quote); else res = resolver_->resolve_perp(base);
        if (!res || res->asset < 0) return {false, "hyperliquid: failed to resolve asset id from meta/spotMeta", std::nullopt, request.client_order_id, std::nullopt, {}};
    }

    const bool is_buy = to_lower_ascii(request.side) == std::string{"buy"};
    if (!request.price.has_value() || request.price->empty()) return {false, "hyperliquid: limit order requires price", std::nullopt, request.client_order_id, std::nullopt, {}};
    std::string px = util::trim_trailing_zeros(*request.price);
    std::string sz = util::trim_trailing_zeros(request.quantity);
    std::string tif = map_tif_to_hl(request.time_in_force);

    rapidjson::Document action(rapidjson::kObjectType);
    auto& alloc = action.GetAllocator();
    action.AddMember("type", rapidjson::Value("order", alloc), alloc);
    rapidjson::Value orders(rapidjson::kArrayType);
    rapidjson::Value ord(rapidjson::kObjectType);
    ord.AddMember("a", res->asset, alloc);
    ord.AddMember("b", is_buy, alloc);
    ord.AddMember("p", rapidjson::Value(px.c_str(), alloc), alloc);
    ord.AddMember("s", rapidjson::Value(sz.c_str(), alloc), alloc);
    ord.AddMember("r", request.reduce_only, alloc);
    {
        rapidjson::Value t(rapidjson::kObjectType);
        rapidjson::Value lim(rapidjson::kObjectType);
        lim.AddMember("tif", rapidjson::Value(tif.c_str(), alloc), alloc);
        t.AddMember("limit", lim, alloc);
        ord.AddMember("t", t, alloc);
    }
    if (!request.client_order_id.empty() && is_valid_cloid(request.client_order_id)) {
        ord.AddMember("c", rapidjson::Value(request.client_order_id.c_str(), alloc), alloc);
    }
    orders.PushBack(ord, alloc);
    action.AddMember("orders", orders, alloc);
    action.AddMember("grouping", rapidjson::Value("na", alloc), alloc);

    const uint64_t nonce = nonce_mgr_ ? nonce_mgr_->next() : 0;

    if (!signer_) return {false, "hyperliquid: signer not configured", std::nullopt, request.client_order_id, std::nullopt, {}};
    // Serialize action to string for signing and posting without reordering keys
    rapidjson::StringBuffer sb_act; { rapidjson::Writer<rapidjson::StringBuffer> wr(sb_act); action.Accept(wr); }
    auto sig = signer_->sign_l1_action(to_lower_ascii(api_secret_), sb_act.GetString(), std::nullopt, nonce, std::nullopt, !testnet_);
    if (!sig) return {false, "hyperliquid: signature unavailable (signer error)", std::nullopt, request.client_order_id, std::nullopt, {}};

    // Build full payload
    // Build full payload
    // Build full payload for cancel
    rapidjson::Document payload(rapidjson::kObjectType);
    payload.AddMember("action", action, payload.GetAllocator());
    payload.AddMember("nonce", rapidjson::Value(static_cast<uint64_t>(nonce)), payload.GetAllocator());
    rapidjson::Value sigv(rapidjson::kObjectType);
    sigv.AddMember("r", rapidjson::Value(sig->r.c_str(), payload.GetAllocator()), payload.GetAllocator());
    sigv.AddMember("s", rapidjson::Value(sig->s.c_str(), payload.GetAllocator()), payload.GetAllocator());
    // v expected numeric
    int v_int = std::atoi(sig->v.c_str());
    sigv.AddMember("v", rapidjson::Value(v_int), payload.GetAllocator());
    payload.AddMember("signature", sigv, payload.GetAllocator());

    // Prefer WS post
    rapidjson::StringBuffer sb; { rapidjson::Writer<rapidjson::StringBuffer> wr(sb); payload.Accept(wr); }
    std::string resp_json;
    bool ok_send = false;
    if (ws_post_ && ws_post_->is_connected()) {
        auto resp = ws_post_->post("action", sb.GetString(), std::chrono::milliseconds(2000));
        if (resp) { resp_json = *resp; ok_send = true; }
    } else {
        // HTTP fallback
        try {
            auto parts = parse_base_url(cfg_.rest_base);
            latentspeed::nethttp::HttpClient httpc;
            std::vector<latentspeed::nethttp::Header> hdrs = {{"Content-Type","application/json"}};
            resp_json = httpc.request("POST", parts.scheme, parts.host, parts.port, "/exchange", hdrs, sb.GetString());
            ok_send = true;
        } catch (...) {}
    }
    if (!ok_send) return {false, "hyperliquid: action send failed", std::nullopt, request.client_order_id, std::nullopt, {}};

    // Parse response for exchange_order_id if resting
    rapidjson::Document dr; dr.Parse(resp_json.c_str());
    if (dr.HasParseError()) return {true, "ok", std::nullopt, request.client_order_id, std::nullopt, {}}; // treat as accepted
    // WS post returns {type:"action", payload:{ status, response:{ type:"order", data:{statuses:[...]}} }}
    const rapidjson::Value* payload_root = &dr;
    if (dr.HasMember("payload") && dr["payload"].IsObject()) payload_root = &dr["payload"];
    if (payload_root->IsObject() && payload_root->HasMember("status") && (*payload_root)["status"].IsString()) {
        std::string status = (*payload_root)["status"].GetString();
        if (status != "ok") return {false, status, std::nullopt, request.client_order_id, std::nullopt, {}};
        if (payload_root->HasMember("response") && (*payload_root)["response"].IsObject()) {
            auto& r = (*payload_root)["response"];
            if (r.HasMember("data") && r["data"].IsObject() && r["data"].HasMember("statuses") && r["data"]["statuses"].IsArray()) {
                for (auto& st : r["data"]["statuses"].GetArray()) {
                    if (st.IsObject() && st.HasMember("resting") && st["resting"].IsObject()) {
                        auto& rest = st["resting"];
                        if (rest.HasMember("oid") && rest["oid"].IsUint64()) {
                            return {true, "ok", std::optional<std::string>(std::to_string(rest["oid"].GetUint64())), request.client_order_id, std::optional<std::string>("accepted"), {}};
                        }
                    }
                }
            }
        }
        return {true, "ok", std::nullopt, request.client_order_id, std::optional<std::string>("accepted"), {}};
    }
    // HTTP success shape: {"status":"ok", "response":{...}}
    if (dr.HasMember("status") && dr["status"].IsString()) {
        std::string s = dr["status"].GetString();
        if (s != "ok") return {false, s, std::nullopt, request.client_order_id, std::nullopt, {}};
        if (dr.HasMember("response") && dr["response"].IsObject()) {
            auto& r = dr["response"];
            if (r.HasMember("data") && r["data"].IsObject() && r["data"].HasMember("statuses") && r["data"]["statuses"].IsArray()) {
                for (auto& st : r["data"]["statuses"].GetArray()) {
                    if (st.IsObject() && st.HasMember("resting") && st["resting"].IsObject()) {
                        auto& rest = st["resting"];
                        if (rest.HasMember("oid") && rest["oid"].IsUint64()) {
                            return {true, "ok", std::optional<std::string>(std::to_string(rest["oid"].GetUint64())), request.client_order_id, std::optional<std::string>("accepted"), {}};
                        }
                    }
                }
            }
        }
        return {true, "ok", std::nullopt, request.client_order_id, std::optional<std::string>("accepted"), {}};
    }
    return {true, "ok", std::nullopt, request.client_order_id, std::optional<std::string>("accepted"), {}};
}

OrderResponse HyperliquidAdapter::cancel_order(const std::string& client_order_id,
                                               const std::optional<std::string>& symbol,
                                               const std::optional<std::string>& exchange_order_id) {
    const bool is_spot = false; // category unknown in signature; assume perps unless explicitly wired later
    const bool have_cloid = is_valid_cloid(client_order_id);
    const bool have_oid = exchange_order_id.has_value() && !exchange_order_id->empty();
    if (!have_cloid && !have_oid) return {false, "hyperliquid: cancel requires cloid (0x+32hex) or numeric oid", std::nullopt, client_order_id, std::nullopt, {}};

    int asset = -1;
    if (symbol && !symbol->empty()) {
        DefaultSymbolMapper mapper;
        std::string hyphen = mapper.to_hyphen(*symbol, !is_spot);
        std::string base, quote; parse_base_quote_from_hyphen(hyphen, base, quote);
        auto res = is_spot ? (resolver_ ? resolver_->resolve_spot(base, quote) : std::nullopt)
                           : (resolver_ ? resolver_->resolve_perp(base) : std::nullopt);
        if (res) asset = res->asset;
    }
    if (asset < 0 && !have_cloid) return {false, "hyperliquid: cancel by oid requires asset id (provide symbol)", std::nullopt, client_order_id, std::nullopt, {}};

    rapidjson::Document action(rapidjson::kObjectType);
    auto& alloc = action.GetAllocator();
    if (have_cloid) {
        action.AddMember("type", rapidjson::Value("cancelByCloid", alloc), alloc);
        rapidjson::Value cancels(rapidjson::kArrayType);
        rapidjson::Value cobj(rapidjson::kObjectType);
        if (asset >= 0) cobj.AddMember("asset", asset, alloc);
        cobj.AddMember("cloid", rapidjson::Value(client_order_id.c_str(), alloc), alloc);
        cancels.PushBack(cobj, alloc);
        action.AddMember("cancels", cancels, alloc);
    } else {
        action.AddMember("type", rapidjson::Value("cancel", alloc), alloc);
        rapidjson::Value cancels(rapidjson::kArrayType);
        rapidjson::Value cobj(rapidjson::kObjectType);
        cobj.AddMember("a", asset, alloc);
        // oid: we pass as string to preserve full precision; HL accepts string or uint64 for some endpoints
        cobj.AddMember("o", rapidjson::Value(exchange_order_id->c_str(), alloc), alloc);
        cancels.PushBack(cobj, alloc);
        action.AddMember("cancels", cancels, alloc);
    }

    const uint64_t nonce = nonce_mgr_ ? nonce_mgr_->next() : 0;
    if (!signer_) return {false, "hyperliquid: signer not configured", std::nullopt, client_order_id, std::nullopt, {}};
    rapidjson::StringBuffer sb_act; { rapidjson::Writer<rapidjson::StringBuffer> wr(sb_act); action.Accept(wr); }
    auto sig = signer_->sign_l1_action(to_lower_ascii(api_secret_), sb_act.GetString(), std::nullopt, nonce, std::nullopt, !testnet_);
    if (!sig) return {false, "hyperliquid: signature unavailable (signer error)", std::nullopt, client_order_id, std::nullopt, {}};

    // reuse payload document name from previous context; ensure not redefined
    rapidjson::Document payload(rapidjson::kObjectType);
    payload.AddMember("action", action, payload.GetAllocator());
    payload.AddMember("nonce", rapidjson::Value(static_cast<uint64_t>(nonce)), payload.GetAllocator());
    rapidjson::Value sigv(rapidjson::kObjectType);
    sigv.AddMember("r", rapidjson::Value(sig->r.c_str(), payload.GetAllocator()), payload.GetAllocator());
    sigv.AddMember("s", rapidjson::Value(sig->s.c_str(), payload.GetAllocator()), payload.GetAllocator());
    int v_int = std::atoi(sig->v.c_str());
    sigv.AddMember("v", rapidjson::Value(v_int), payload.GetAllocator());
    payload.AddMember("signature", sigv, payload.GetAllocator());

    rapidjson::StringBuffer sb; { rapidjson::Writer<rapidjson::StringBuffer> wr(sb); payload.Accept(wr); }
    std::string resp_json;
    bool ok_send = false;
    if (ws_post_ && ws_post_->is_connected()) {
        auto resp = ws_post_->post("action", sb.GetString(), std::chrono::milliseconds(2000));
        if (resp) { resp_json = *resp; ok_send = true; }
    } else {
        try {
            auto parts = parse_base_url(cfg_.rest_base);
            latentspeed::nethttp::HttpClient httpc;
            std::vector<latentspeed::nethttp::Header> hdrs = {{"Content-Type","application/json"}};
            resp_json = httpc.request("POST", parts.scheme, parts.host, parts.port, "/exchange", hdrs, sb.GetString());
            ok_send = true;
        } catch (...) {}
    }
    if (!ok_send) return {false, "hyperliquid: cancel send failed", std::nullopt, client_order_id, std::nullopt, {}};
    return {true, "ok", std::nullopt, client_order_id, std::optional<std::string>("canceled"), {}};
}

OrderResponse HyperliquidAdapter::modify_order(const std::string& client_order_id,
                                               const std::optional<std::string>& new_quantity,
                                               const std::optional<std::string>& new_price) {
    if (!new_quantity.has_value() && !new_price.has_value())
        return {false, "hyperliquid: modify requires new_quantity and/or new_price", std::nullopt, client_order_id, std::nullopt, {}};
    return {false, "hyperliquid: modify not yet implemented (requires original order context)", std::nullopt, client_order_id, std::nullopt, {}};
}

OrderResponse HyperliquidAdapter::query_order(const std::string& client_order_id) {
    (void)client_order_id;
    return {false, "hyperliquid: query_order requires /info orderStatus with user address; not wired", std::nullopt, std::nullopt, std::nullopt, {}};
}

void HyperliquidAdapter::set_order_update_callback(OrderUpdateCallback cb) {
    order_update_cb_ = std::move(cb);
}

void HyperliquidAdapter::set_fill_callback(FillCallback cb) {
    fill_cb_ = std::move(cb);
}

void HyperliquidAdapter::set_error_callback(ErrorCallback cb) {
    error_cb_ = std::move(cb);
}

std::vector<OpenOrderBrief> HyperliquidAdapter::list_open_orders(
    const std::optional<std::string>& category,
    const std::optional<std::string>& symbol,
    const std::optional<std::string>& settle_coin,
    const std::optional<std::string>& base_coin) {
    (void)category; (void)symbol; (void)settle_coin; (void)base_coin;
    return {};
}

} // namespace latentspeed
