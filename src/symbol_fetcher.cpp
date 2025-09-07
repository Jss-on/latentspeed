/**
 * @file symbol_fetcher.cpp
 * @brief Implementation of dynamic symbol fetching from cryptocurrency exchanges
 * @author jessiondiwangan@gmail.com
 * @date 2025
 */

#include "symbol_fetcher.h"
#include <algorithm>
#include <sstream>
#include <iostream>
#include <regex>
#include <set>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

namespace latentspeed {

// Static regex pattern for leveraged tokens
const std::regex BaseSymbolFetcher::leveraged_pattern_(R"((?:[35](?:L|S)$|UP$|DOWN$))", std::regex::icase);

//=============================================================================
// HttpClient Implementation
//=============================================================================

HttpClient::HttpClient() = default;
HttpClient::~HttpClient() = default;

std::optional<std::string> HttpClient::get(const std::string& host,
                                           const std::string& port,
                                           const std::string& target,
                                           const std::map<std::string, std::string>& params,
                                           std::chrono::seconds timeout) {
    try {
        // Build full target with query parameters
        std::string full_target = target;
        if (!params.empty()) {
            full_target += "?" + build_query_string(params);
        }

        // Create I/O context
        net::io_context ioc;

        // Create SSL context
        ssl::context ctx(ssl::context::tlsv12_client);
        ctx.set_default_verify_paths();
        ctx.set_verify_mode(ssl::verify_none);  // Disable certificate verification for now

        // Create and configure SSL stream
        beast::ssl_stream<beast::tcp_stream> stream(ioc, ctx);
        
        // Set SNI hostname for proper SSL handshake
        if (!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str())) {
            spdlog::warn("[HttpClient] Failed to set SNI hostname for {}", host);
        }

        // Set a timeout on the operation
        beast::get_lowest_layer(stream).expires_after(timeout);

        // Look up the domain name
        tcp::resolver resolver(ioc);
        auto const results = resolver.resolve(host, port);

        // Make the connection on the IP address we get from a lookup
        beast::get_lowest_layer(stream).connect(results);

        // Perform the SSL handshake
        stream.handshake(ssl::stream_base::client);

        // Set up an HTTP GET request message
        http::request<http::string_body> req{http::verb::get, full_target, 11};
        req.set(http::field::host, host);
        req.set(http::field::user_agent, "LatentSpeed/1.0");
        req.set(http::field::accept, "application/json");

        // Send the HTTP request to the remote host
        http::write(stream, req);

        // This buffer is used for reading and must be persistent
        beast::flat_buffer buffer;

        // Declare a container to hold the response
        http::response<http::string_body> res;

        // Receive the HTTP response
        http::read(stream, buffer, res);

        // Check for HTTP errors
        if (res.result() != http::status::ok) {
            spdlog::error("[HttpClient] HTTP request failed: {} {}", 
                         static_cast<int>(res.result()), res.reason());
            return std::nullopt;
        }

        // Gracefully close the stream
        beast::error_code ec;
        stream.shutdown(ec);

        return res.body();

    } catch (const std::exception& e) {
        spdlog::error("[HttpClient] Request failed for {}:{}{}: {}", host, port, target, e.what());
        return std::nullopt;
    }
}

std::string HttpClient::build_query_string(const std::map<std::string, std::string>& params) {
    std::ostringstream oss;
    bool first = true;
    for (const auto& [key, value] : params) {
        if (!first) {
            oss << "&";
        }
        oss << key << "=" << value;
        first = false;
    }
    return oss.str();
}

//=============================================================================
// BaseSymbolFetcher Implementation
//=============================================================================

BaseSymbolFetcher::BaseSymbolFetcher(const std::string& exchange_name)
    : exchange_name_(exchange_name)
    , http_client_(std::make_unique<HttpClient>()) {
}

bool BaseSymbolFetcher::is_leveraged_token(const std::string& base_asset) const {
    return std::regex_search(base_asset, leveraged_pattern_);
}

std::string BaseSymbolFetcher::normalize_symbol(const std::string& base_asset, const std::string& quote_asset) const {
    return base_asset + "-" + quote_asset;
}

double BaseSymbolFetcher::parse_float(const std::string& value) const {
    try {
        return std::stod(value);
    } catch (const std::exception&) {
        return 0.0;
    }
}

//=============================================================================
// BybitSymbolFetcher Implementation
//=============================================================================

BybitSymbolFetcher::BybitSymbolFetcher()
    : BaseSymbolFetcher("bybit") {
}

std::vector<SymbolInfo> BybitSymbolFetcher::fetch_top_symbols(const FetcherConfig& config) {
    spdlog::info("[BybitSymbolFetcher] Fetching top {} symbols (quote: {})", 
                 config.top_n, config.quote_currency);

    try {
        // Fetch instruments and tickers from Bybit API
        auto instruments = fetch_spot_instruments();
        auto tickers = fetch_spot_tickers();

        if (instruments.empty() || tickers.empty()) {
            spdlog::error("[BybitSymbolFetcher] Failed to fetch data from Bybit API");
            return {};
        }

        // Process and filter symbols
        auto symbols = process_symbols(instruments, tickers, config);
        
        spdlog::info("[BybitSymbolFetcher] Successfully fetched {} symbols", symbols.size());
        return symbols;

    } catch (const std::exception& e) {
        spdlog::error("[BybitSymbolFetcher] Error fetching symbols: {}", e.what());
        return {};
    }
}

std::map<std::string, std::map<std::string, std::string>> BybitSymbolFetcher::fetch_spot_instruments() {
    std::map<std::string, std::map<std::string, std::string>> instruments;
    
    auto response = http_client_->get(BYBIT_HOST, BYBIT_PORT, INSTRUMENTS_PATH, 
                                     {{"category", "spot"}});
    
    if (!response.has_value()) {
        spdlog::error("[BybitSymbolFetcher] Failed to fetch instruments from Bybit");
        return instruments;
    }

    try {
        rapidjson::Document doc;
        doc.Parse(response->c_str());

        if (doc.HasParseError()) {
            spdlog::error("[BybitSymbolFetcher] Failed to parse instruments JSON");
            return instruments;
        }

        if (!doc.HasMember("result") || !doc["result"].HasMember("list")) {
            spdlog::error("[BybitSymbolFetcher] Invalid instruments response format");
            return instruments;
        }

        const auto& list = doc["result"]["list"];
        for (const auto& item : list.GetArray()) {
            if (item.HasMember("symbol") && item["symbol"].IsString()) {
                std::string symbol = item["symbol"].GetString();
                std::transform(symbol.begin(), symbol.end(), symbol.begin(), ::toupper);
                
                // Extract key fields into a simple map
                std::map<std::string, std::string> instrument_data;
                if (item.HasMember("status") && item["status"].IsString()) {
                    instrument_data["status"] = item["status"].GetString();
                }
                if (item.HasMember("baseCoin") && item["baseCoin"].IsString()) {
                    instrument_data["baseCoin"] = item["baseCoin"].GetString();
                }
                if (item.HasMember("quoteCoin") && item["quoteCoin"].IsString()) {
                    instrument_data["quoteCoin"] = item["quoteCoin"].GetString();
                }
                
                instruments[symbol] = std::move(instrument_data);
            }
        }

        spdlog::info("[BybitSymbolFetcher] Fetched {} instruments", instruments.size());

    } catch (const std::exception& e) {
        spdlog::error("[BybitSymbolFetcher] Error parsing instruments: {}", e.what());
    }

    return instruments;
}

std::vector<std::map<std::string, std::string>> BybitSymbolFetcher::fetch_spot_tickers() {
    std::vector<std::map<std::string, std::string>> tickers;
    
    auto response = http_client_->get(BYBIT_HOST, BYBIT_PORT, TICKERS_PATH, 
                                     {{"category", "spot"}});
    
    if (!response.has_value()) {
        spdlog::error("[BybitSymbolFetcher] Failed to fetch tickers from Bybit");
        return tickers;
    }

    try {
        rapidjson::Document doc;
        doc.Parse(response->c_str());

        if (doc.HasParseError()) {
            spdlog::error("[BybitSymbolFetcher] Failed to parse tickers JSON");
            return tickers;
        }

        if (!doc.HasMember("result") || !doc["result"].HasMember("list")) {
            spdlog::error("[BybitSymbolFetcher] Invalid tickers response format");
            return tickers;
        }

        const auto& list = doc["result"]["list"];
        for (const auto& item : list.GetArray()) {
            std::map<std::string, std::string> ticker_data;
            
            if (item.HasMember("symbol") && item["symbol"].IsString()) {
                ticker_data["symbol"] = item["symbol"].GetString();
            }
            if (item.HasMember("turnover24h") && item["turnover24h"].IsString()) {
                ticker_data["turnover24h"] = item["turnover24h"].GetString();
            }
            if (item.HasMember("volume24h") && item["volume24h"].IsString()) {
                ticker_data["volume24h"] = item["volume24h"].GetString();
            }
            
            if (!ticker_data.empty()) {
                tickers.push_back(std::move(ticker_data));
            }
        }

        spdlog::info("[BybitSymbolFetcher] Fetched {} tickers", tickers.size());

    } catch (const std::exception& e) {
        spdlog::error("[BybitSymbolFetcher] Error parsing tickers: {}", e.what());
    }

    return tickers;
}

std::vector<SymbolInfo> BybitSymbolFetcher::process_symbols(
    const std::map<std::string, std::map<std::string, std::string>>& instruments,
    const std::vector<std::map<std::string, std::string>>& tickers,
    const FetcherConfig& config) {
    
    std::vector<SymbolInfo> symbol_candidates;
    std::string quote_upper = config.quote_currency;
    std::transform(quote_upper.begin(), quote_upper.end(), quote_upper.begin(), ::toupper);

    for (const auto& ticker : tickers) {
        auto symbol_it = ticker.find("symbol");
        if (symbol_it == ticker.end()) {
            continue;
        }

        std::string symbol = symbol_it->second;
        std::transform(symbol.begin(), symbol.end(), symbol.begin(), ::toupper);

        // Find corresponding instrument
        auto inst_it = instruments.find(symbol);
        if (inst_it == instruments.end()) {
            continue;
        }
        
        const auto& inst = inst_it->second;

        // Check trading status
        auto status_it = inst.find("status");
        if (status_it != inst.end() && status_it->second != "Trading") {
            continue;
        }

        // Extract base and quote assets
        std::string base_asset, quote_asset;
        auto base_it = inst.find("baseCoin");
        auto quote_it = inst.find("quoteCoin");
        
        if (base_it != inst.end() && quote_it != inst.end()) {
            base_asset = base_it->second;
            quote_asset = quote_it->second;
        } else {
            // Fallback: try to derive from symbol
            if (symbol.length() > quote_upper.length() && 
                symbol.substr(symbol.length() - quote_upper.length()) == quote_upper) {
                base_asset = symbol.substr(0, symbol.length() - quote_upper.length());
                quote_asset = quote_upper;
            } else {
                continue;
            }
        }

        std::transform(base_asset.begin(), base_asset.end(), base_asset.begin(), ::toupper);
        std::transform(quote_asset.begin(), quote_asset.end(), quote_asset.begin(), ::toupper);

        // Filter by quote currency
        if (quote_asset != quote_upper) {
            continue;
        }

        // Filter leveraged tokens if requested
        if (!config.include_leveraged && is_leveraged_token(base_asset)) {
            continue;
        }

        // Extract turnover and volume
        double turnover_24h = 0.0;
        double volume_24h = 0.0;

        auto turnover_it = ticker.find("turnover24h");
        if (turnover_it != ticker.end()) {
            turnover_24h = parse_float(turnover_it->second);
        }
        
        auto volume_it = ticker.find("volume24h");
        if (volume_it != ticker.end()) {
            volume_24h = parse_float(volume_it->second);
        }

        // Apply minimum turnover filter
        if (turnover_24h < config.min_turnover) {
            continue;
        }

        // Create symbol info
        SymbolInfo symbol_info;
        symbol_info.symbol = symbol;
        symbol_info.base_asset = base_asset;
        symbol_info.quote_asset = quote_asset;
        symbol_info.normalized_pair = normalize_symbol(base_asset, quote_asset);
        symbol_info.turnover_24h = turnover_24h;
        symbol_info.volume_24h = volume_24h;
        symbol_info.status = "Trading";
        symbol_info.is_leveraged_token = is_leveraged_token(base_asset);

        symbol_candidates.push_back(symbol_info);
    }

    // Sort by turnover descending
    std::sort(symbol_candidates.begin(), symbol_candidates.end(),
              [](const SymbolInfo& a, const SymbolInfo& b) {
                  return a.turnover_24h > b.turnover_24h;
              });

    // Limit to top_n symbols and remove duplicates by normalized pair
    std::vector<SymbolInfo> result;
    std::set<std::string> seen_pairs;
    
    for (const auto& symbol_info : symbol_candidates) {
        if (seen_pairs.count(symbol_info.normalized_pair)) {
            continue;
        }
        seen_pairs.insert(symbol_info.normalized_pair);
        result.push_back(symbol_info);
        
        if (result.size() >= static_cast<size_t>(config.top_n)) {
            break;
        }
    }

    return result;
}

//=============================================================================
// SymbolFetcherFactory Implementation
//=============================================================================

std::unique_ptr<BaseSymbolFetcher> SymbolFetcherFactory::create_fetcher(const std::string& exchange_name) {
    std::string exchange_lower = exchange_name;
    std::transform(exchange_lower.begin(), exchange_lower.end(), exchange_lower.begin(), ::tolower);

    if (exchange_lower == "bybit") {
        return std::make_unique<BybitSymbolFetcher>();
    }

    spdlog::warn("[SymbolFetcherFactory] Unsupported exchange: {}", exchange_name);
    return nullptr;
}

std::vector<std::string> SymbolFetcherFactory::get_supported_exchanges() {
    return {"bybit"};
}

//=============================================================================
// DynamicSymbolManager Implementation
//=============================================================================

DynamicSymbolManager::DynamicSymbolManager() = default;

std::map<std::string, std::vector<std::string>> DynamicSymbolManager::fetch_symbols_for_exchanges(
    const std::vector<std::string>& exchanges,
    const FetcherConfig& config) {
    
    std::map<std::string, std::vector<std::string>> result;

    for (const auto& exchange : exchanges) {
        try {
            auto symbols = fetch_symbols_for_exchange(exchange, config);
            if (!symbols.empty()) {
                result[exchange] = std::move(symbols);
                spdlog::info("[DynamicSymbolManager] Fetched {} symbols for {}", 
                           result[exchange].size(), exchange);
            } else {
                spdlog::warn("[DynamicSymbolManager] No symbols fetched for {}", exchange);
            }
        } catch (const std::exception& e) {
            spdlog::error("[DynamicSymbolManager] Error fetching symbols for {}: {}", 
                         exchange, e.what());
        }
    }

    return result;
}

std::vector<std::string> DynamicSymbolManager::fetch_symbols_for_exchange(
    const std::string& exchange_name,
    const FetcherConfig& config) {
    
    // Get or create fetcher for this exchange
    auto& fetcher = fetchers_[exchange_name];
    if (!fetcher) {
        fetcher = SymbolFetcherFactory::create_fetcher(exchange_name);
        if (!fetcher) {
            spdlog::error("[DynamicSymbolManager] Failed to create fetcher for {}", exchange_name);
            return {};
        }
    }

    // Fetch symbols
    auto symbol_infos = fetcher->fetch_top_symbols(config);
    
    // Extract normalized pairs
    std::vector<std::string> symbols;
    symbols.reserve(symbol_infos.size());
    
    for (const auto& info : symbol_infos) {
        symbols.push_back(info.normalized_pair);
    }

    return symbols;
}

} // namespace latentspeed
