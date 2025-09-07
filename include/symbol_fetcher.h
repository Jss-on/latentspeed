/**
 * @file symbol_fetcher.h
 * @brief Dynamic symbol fetching from cryptocurrency exchanges
 * @author jessiondiwangan@gmail.com
 * @date 2025
 * 
 * Provides functionality to dynamically fetch top volume trading pairs
 * from various cryptocurrency exchanges during initialization.
 */

#pragma once

#include <string>
#include <vector>
#include <map>
#include <regex>
#include <optional>
#include <memory>
#include <chrono>

// HTTP client using boost::beast
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>

// JSON parsing
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>

// Logging
#include <spdlog/spdlog.h>

namespace latentspeed {

/**
 * @struct SymbolInfo
 * @brief Information about a trading symbol
 */
struct SymbolInfo {
    std::string symbol;           ///< Exchange-specific symbol (e.g., "BTCUSDT")
    std::string base_asset;       ///< Base asset (e.g., "BTC")
    std::string quote_asset;      ///< Quote asset (e.g., "USDT") 
    std::string normalized_pair;  ///< Normalized pair format (e.g., "BTC-USDT")
    double turnover_24h = 0.0;    ///< 24-hour turnover in quote currency
    double volume_24h = 0.0;      ///< 24-hour volume in base currency
    std::string status;           ///< Trading status (e.g., "Trading")
    bool is_leveraged_token = false; ///< Whether this is a leveraged/ETF token
};

/**
 * @struct FetcherConfig
 * @brief Configuration for symbol fetching
 */
struct FetcherConfig {
    int top_n = 500;                     ///< Number of top symbols to fetch
    std::string quote_currency = "USDT"; ///< Preferred quote currency
    bool include_leveraged = false;      ///< Include leveraged/ETF tokens
    double min_turnover = 0.0;          ///< Minimum 24h turnover threshold
    std::chrono::seconds timeout{30};    ///< HTTP request timeout
};

/**
 * @class HttpClient
 * @brief HTTP client using boost::beast for API calls
 */
class HttpClient {
public:
    HttpClient();
    ~HttpClient();

    /**
     * @brief Perform HTTPS GET request
     * @param host Target host (e.g., "api.bybit.com")
     * @param port Target port (default 443 for HTTPS)
     * @param target Request path (e.g., "/v5/market/instruments-info")
     * @param params Query parameters
     * @param timeout Request timeout
     * @return Response body on success, std::nullopt on failure
     */
    std::optional<std::string> get(const std::string& host,
                                   const std::string& port,
                                   const std::string& target,
                                   const std::map<std::string, std::string>& params = {},
                                   std::chrono::seconds timeout = std::chrono::seconds{30});

private:
    std::string build_query_string(const std::map<std::string, std::string>& params);
};

/**
 * @class BaseSymbolFetcher
 * @brief Abstract base class for exchange-specific symbol fetchers
 */
class BaseSymbolFetcher {
public:
    explicit BaseSymbolFetcher(const std::string& exchange_name);
    virtual ~BaseSymbolFetcher() = default;

    /**
     * @brief Fetch top volume symbols from the exchange
     * @param config Fetcher configuration
     * @return Vector of symbol information, sorted by turnover descending
     */
    virtual std::vector<SymbolInfo> fetch_top_symbols(const FetcherConfig& config) = 0;

    /**
     * @brief Get exchange name
     * @return Exchange name
     */
    const std::string& get_exchange_name() const { return exchange_name_; }

protected:
    /**
     * @brief Check if a token is a leveraged/ETF token
     * @param base_asset Base asset name
     * @return true if leveraged token, false otherwise
     */
    bool is_leveraged_token(const std::string& base_asset) const;

    /**
     * @brief Normalize symbol to BASE-QUOTE format
     * @param base_asset Base asset
     * @param quote_asset Quote asset
     * @return Normalized symbol (e.g., "BTC-USDT")
     */
    std::string normalize_symbol(const std::string& base_asset, const std::string& quote_asset) const;

    /**
     * @brief Parse float value from string safely
     * @param value String value to parse
     * @return Parsed float value, 0.0 if parsing fails
     */
    double parse_float(const std::string& value) const;

    std::string exchange_name_;
    std::unique_ptr<HttpClient> http_client_;

private:
    static const std::regex leveraged_pattern_;
};

/**
 * @class BybitSymbolFetcher
 * @brief Symbol fetcher for Bybit exchange
 */
class BybitSymbolFetcher : public BaseSymbolFetcher {
public:
    BybitSymbolFetcher();
    ~BybitSymbolFetcher() override = default;

    /**
     * @brief Fetch top volume symbols from Bybit
     * @param config Fetcher configuration
     * @return Vector of symbol information, sorted by turnover descending
     */
    std::vector<SymbolInfo> fetch_top_symbols(const FetcherConfig& config) override;

private:
    /**
     * @brief Fetch spot instruments from Bybit API
     * @return Map of symbol -> instrument info
     */
    std::map<std::string, std::map<std::string, std::string>> fetch_spot_instruments();

    /**
     * @brief Fetch spot tickers from Bybit API
     * @return Vector of ticker information
     */
    std::vector<std::map<std::string, std::string>> fetch_spot_tickers();

    /**
     * @brief Process and filter symbols based on configuration
     * @param instruments Map of instruments
     * @param tickers Vector of tickers
     * @param config Fetcher configuration
     * @return Filtered and sorted vector of symbols
     */
    std::vector<SymbolInfo> process_symbols(
        const std::map<std::string, std::map<std::string, std::string>>& instruments,
        const std::vector<std::map<std::string, std::string>>& tickers,
        const FetcherConfig& config);

    static constexpr const char* BYBIT_HOST = "api.bybit.com";
    static constexpr const char* BYBIT_PORT = "443";
    static constexpr const char* INSTRUMENTS_PATH = "/v5/market/instruments-info";
    static constexpr const char* TICKERS_PATH = "/v5/market/tickers";
};

/**
 * @class SymbolFetcherFactory
 * @brief Factory for creating exchange-specific symbol fetchers
 */
class SymbolFetcherFactory {
public:
    /**
     * @brief Create symbol fetcher for the specified exchange
     * @param exchange_name Exchange name (e.g., "bybit", "binance")
     * @return Unique pointer to symbol fetcher, nullptr if unsupported
     */
    static std::unique_ptr<BaseSymbolFetcher> create_fetcher(const std::string& exchange_name);

    /**
     * @brief Get list of supported exchanges
     * @return Vector of supported exchange names
     */
    static std::vector<std::string> get_supported_exchanges();
};

/**
 * @class DynamicSymbolManager
 * @brief Main class for managing dynamic symbol fetching
 */
class DynamicSymbolManager {
public:
    DynamicSymbolManager();
    ~DynamicSymbolManager() = default;

    /**
     * @brief Fetch top symbols from specified exchanges
     * @param exchanges List of exchange names
     * @param config Fetcher configuration
     * @return Map of exchange -> vector of normalized symbols
     */
    std::map<std::string, std::vector<std::string>> fetch_symbols_for_exchanges(
        const std::vector<std::string>& exchanges,
        const FetcherConfig& config = FetcherConfig{});

    /**
     * @brief Fetch symbols from a single exchange
     * @param exchange_name Exchange name
     * @param config Fetcher configuration
     * @return Vector of normalized symbols for the exchange
     */
    std::vector<std::string> fetch_symbols_for_exchange(
        const std::string& exchange_name,
        const FetcherConfig& config = FetcherConfig{});

private:
    std::map<std::string, std::unique_ptr<BaseSymbolFetcher>> fetchers_;
};

} // namespace latentspeed
