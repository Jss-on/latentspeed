/**
 * @file hyperliquid_asset_resolver.cpp
 */

#include "adapters/hyperliquid_asset_resolver.h"
#include "core/net/http_client.h"
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <cctype>
#include <sstream>

namespace latentspeed {

using latentspeed::nethttp::HttpClient;

namespace {

static inline std::string to_upper_ascii(std::string_view s) {
    std::string out(s.begin(), s.end());
    for (auto& c : out) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return out;
}

static void parse_base_url(const std::string& url, std::string& scheme, std::string& host, std::string& port) {
    scheme = "https"; host = url; port = "443";
    std::string::size_type p = url.find("://");
    std::string rest = url;
    if (p != std::string::npos) {
        scheme = url.substr(0, p);
        rest = url.substr(p + 3);
    }
    auto slash = rest.find('/');
    std::string hostport = (slash == std::string::npos) ? rest : rest.substr(0, slash);
    auto colon = hostport.find(':');
    if (colon == std::string::npos) {
        host = hostport;
        port = (scheme == "http") ? "80" : "443";
    } else {
        host = hostport.substr(0, colon);
        port = hostport.substr(colon + 1);
    }
}

} // namespace

HyperliquidAssetResolver::HyperliquidAssetResolver(HyperliquidConfig cfg)
    : cfg_(std::move(cfg))
{
    parse_base_url(cfg_.rest_base, scheme_, host_, port_);
}

bool HyperliquidAssetResolver::refresh_all() {
    bool a = ensure_meta();
    bool b = ensure_spot_meta();
    return a && b;
}

bool HyperliquidAssetResolver::ensure_meta() {
    const auto now = std::chrono::steady_clock::now();
    if (!perp_coin_to_res_.empty() && (now - meta_time_) < ttl_) return true;
    auto json = post_info("meta");
    if (!json) return false;
    perp_coin_to_res_.clear();
    bool ok = parse_perp_meta_json(*json);
    if (ok) meta_time_ = now;
    return ok;
}

bool HyperliquidAssetResolver::ensure_spot_meta() {
    const auto now = std::chrono::steady_clock::now();
    if (!spot_index_to_tokens_.empty() && !token_name_to_id_.empty() && (now - spot_meta_time_) < ttl_) return true;
    auto json = post_info("spotMeta");
    if (!json) return false;
    spot_index_to_tokens_.clear();
    token_name_to_id_.clear();
    token_id_to_name_.clear();
    bool ok = parse_spot_meta_json(*json);
    if (ok) spot_meta_time_ = now;
    return ok;
}

std::optional<HlResolution> HyperliquidAssetResolver::resolve_perp(std::string_view coin) {
    if (!ensure_meta()) return std::nullopt;
    std::string key = to_upper_ascii(coin);
    auto it = perp_coin_to_res_.find(key);
    if (it == perp_coin_to_res_.end()) return std::nullopt;
    return it->second;
}

std::optional<HlResolution> HyperliquidAssetResolver::resolve_spot(std::string_view base, std::string_view quote) {
    if (!ensure_spot_meta()) return std::nullopt;
    std::string b = to_upper_ascii(base);
    std::string q = to_upper_ascii(quote);
    auto itb = token_name_to_id_.find(b);
    auto itq = token_name_to_id_.find(q);
    if (itb == token_name_to_id_.end() || itq == token_name_to_id_.end()) return std::nullopt;
    const int base_id = itb->second;
    const int quote_id = itq->second;
    for (const auto& kv : spot_index_to_tokens_) {
        const int idx = kv.first;
        const auto& pair = kv.second;
        if (pair.first == base_id && pair.second == quote_id) {
            return HlResolution{ .asset = 10000 + idx, .sz_decimals = -1 };
        }
    }
    return std::nullopt;
}

std::optional<std::pair<std::string,std::string>> HyperliquidAssetResolver::resolve_spot_pair_by_index(int index) {
    if (!ensure_spot_meta()) return std::nullopt;
    auto it = spot_index_to_tokens_.find(index);
    if (it == spot_index_to_tokens_.end()) return std::nullopt;
    int base_id = it->second.first;
    int quote_id = it->second.second;
    auto ib = token_id_to_name_.find(base_id);
    auto iq = token_id_to_name_.find(quote_id);
    if (ib == token_id_to_name_.end() || iq == token_id_to_name_.end()) return std::nullopt;
    return std::make_pair(ib->second, iq->second);
}

std::optional<std::string> HyperliquidAssetResolver::post_info(const std::string& type) {
    try {
        HttpClient http;
        const std::string target = "/info";
        const std::string body = std::string("{\"type\":\"") + type + "\"}";
        std::vector<nethttp::Header> headers = { {"Content-Type", "application/json"} };
        return http.request("POST", scheme_, host_, port_, target, headers, body);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

bool HyperliquidAssetResolver::parse_perp_meta_json(const std::string& json) {
    rapidjson::Document d;
    d.Parse(json.c_str());
    if (d.HasParseError()) return false;
    // Look for top-level object with array field that has coin names and szDecimals.
    const rapidjson::Value* arr = nullptr;
    if (d.IsObject()) {
        if (d.HasMember("universe") && d["universe"].IsArray()) {
            arr = &d["universe"];
        } else if (d.HasMember("data") && d["data"].IsObject() && d["data"].HasMember("universe") && d["data"]["universe"].IsArray()) {
            arr = &d["data"]["universe"];
        }
    } else if (d.IsArray()) {
        arr = &d;
    }
    if (!arr) return false;
    int idx = 0;
    for (auto& v : arr->GetArray()) {
        // Accept either object with {"name": <coin>, "szDecimals": <int>} or string coin names
        std::string coin;
        int szd = -1;
        if (v.IsObject()) {
            if (v.HasMember("name") && v["name"].IsString()) coin = v["name"].GetString();
            else if (v.HasMember("coin") && v["coin"].IsString()) coin = v["coin"].GetString();
            if (v.HasMember("szDecimals") && v["szDecimals"].IsInt()) szd = v["szDecimals"].GetInt();
        } else if (v.IsString()) {
            coin = v.GetString();
        }
        if (!coin.empty()) {
            perp_coin_to_res_[to_upper_ascii(coin)] = HlResolution{ .asset = idx, .sz_decimals = szd };
        }
        ++idx;
    }
    return !perp_coin_to_res_.empty();
}

bool HyperliquidAssetResolver::parse_spot_meta_json(const std::string& json) {
    rapidjson::Document d;
    d.Parse(json.c_str());
    if (d.HasParseError()) return false;

    const rapidjson::Value* root = &d;
    if (d.IsObject() && d.HasMember("data") && d["data"].IsObject()) root = &d["data"];

    // tokens: array of objects with at least name and id (or index)
    if (root->IsObject() && root->HasMember("tokens") && (*root)["tokens"].IsArray()) {
        for (auto& t : (*root)["tokens"].GetArray()) {
            if (!t.IsObject()) continue;
            if (!t.HasMember("name") || !t["name"].IsString()) continue;
            const std::string name = t["name"].GetString();
            int id = -1;
            if (t.HasMember("token") && t["token"].IsInt()) id = t["token"].GetInt();
            else if (t.HasMember("id") && t["id"].IsInt()) id = t["id"].GetInt();
            else if (t.HasMember("index") && t["index"].IsInt()) id = t["index"].GetInt();
            if (id >= 0) {
                token_name_to_id_[to_upper_ascii(name)] = id;
                token_id_to_name_[id] = name;
            }
        }
    }

    // universe: array of pairs, each item has tokens: [base, quote]
    if (root->IsObject() && root->HasMember("universe") && (*root)["universe"].IsArray()) {
        int idx = 0;
        for (auto& u : (*root)["universe"].GetArray()) {
            if (u.IsObject() && u.HasMember("tokens") && u["tokens"].IsArray() && u["tokens"].Size() >= 2 &&
                u["tokens"][0u].IsInt() && u["tokens"][1u].IsInt()) {
                int base = u["tokens"][0u].GetInt();
                int quote = u["tokens"][1u].GetInt();
                spot_index_to_tokens_[idx] = {base, quote};
            } else if (u.IsArray() && u.Size() >= 2 && u[0u].IsInt() && u[1u].IsInt()) {
                int base = u[0u].GetInt();
                int quote = u[1u].GetInt();
                spot_index_to_tokens_[idx] = {base, quote};
            }
            ++idx;
        }
    }
    return !spot_index_to_tokens_.empty() && !token_name_to_id_.empty();
}

} // namespace latentspeed
