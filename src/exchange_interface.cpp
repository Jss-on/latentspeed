/**
 * @file exchange_interface.cpp
 * @brief Exchange interface implementations
 * @author jessiondiwangan@gmail.com
 * @date 2025
 */

#include "exchange_interface.h"
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <spdlog/spdlog.h>
#include <sstream>
#include <map>

namespace latentspeed {

// ============================================================================
// BybitExchange Implementation
// ============================================================================

std::string BybitExchange::generate_subscription(
    const std::vector<std::string>& symbols,
    bool enable_trades,
    bool enable_orderbook
) const {
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    
    writer.StartObject();
    writer.Key("op");
    writer.String("subscribe");
    writer.Key("args");
    writer.StartArray();
    
    for (const auto& symbol : symbols) {
        std::string normalized = normalize_symbol(symbol);
        
        if (enable_trades) {
            std::string channel = "publicTrade." + normalized;
            writer.String(channel.c_str());
        }
        
        if (enable_orderbook) {
            std::string channel = "orderbook.10." + normalized;
            writer.String(channel.c_str());
        }
    }
    
    writer.EndArray();
    writer.EndObject();
    
    return buffer.GetString();
}

ExchangeInterface::MessageType BybitExchange::parse_message(
    const std::string& message,
    MarketTick& tick,
    OrderBookSnapshot& snapshot
) const {
    rapidjson::Document doc;
    doc.Parse(message.c_str());
    
    if (doc.HasParseError()) {
        return MessageType::ERROR;
    }
    
    // Check for heartbeat/ping
    if (doc.HasMember("op") && doc["op"].IsString()) {
        std::string op = doc["op"].GetString();
        if (op == "ping" || op == "pong") {
            return MessageType::HEARTBEAT;
        }
    }
    
    // Check for subscription response
    if (doc.HasMember("success") && doc["success"].IsBool()) {
        return MessageType::HEARTBEAT; // Treat as heartbeat
    }
    
    // Parse data messages
    if (!doc.HasMember("topic") || !doc["topic"].IsString()) {
        return MessageType::UNKNOWN;
    }
    
    std::string topic = doc["topic"].GetString();
    
    // Trade message
    if (topic.find("publicTrade") != std::string::npos) {
        if (!doc.HasMember("data") || !doc["data"].IsArray()) {
            return MessageType::ERROR;
        }
        
        const auto& trades = doc["data"].GetArray();
        if (trades.Empty()) {
            return MessageType::UNKNOWN;
        }
        
        // Take first trade (we'll process others in a loop if needed)
        const auto& trade = trades[0];
        
        tick.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        tick.exchange.assign("BYBIT");
        
        if (trade.HasMember("s") && trade["s"].IsString()) {
            std::string symbol = trade["s"].GetString();
            // Convert back to standard format with dash
            if (symbol.length() >= 6) {
                symbol.insert(symbol.length() - 4, "-"); // BTC-USDT format
            }
            tick.symbol.assign(symbol);
        }
        
        if (trade.HasMember("p") && trade["p"].IsString()) {
            tick.price = std::stod(trade["p"].GetString());
        }
        
        if (trade.HasMember("v") && trade["v"].IsString()) {
            tick.amount = std::stod(trade["v"].GetString());
        }
        
        if (trade.HasMember("S") && trade["S"].IsString()) {
            std::string side = trade["S"].GetString();
            tick.side.assign(side == "Buy" ? "buy" : "sell");
        }
        
        if (trade.HasMember("i") && trade["i"].IsString()) {
            tick.trade_id.assign(trade["i"].GetString());
        }
        
        return MessageType::TRADE;
    }
    
    // Orderbook message
    if (topic.find("orderbook") != std::string::npos) {
        if (!doc.HasMember("data") || !doc["data"].IsObject()) {
            return MessageType::ERROR;
        }
        
        const auto& data = doc["data"];
        
        snapshot.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        snapshot.exchange.assign("BYBIT");
        
        if (data.HasMember("s") && data["s"].IsString()) {
            std::string symbol = data["s"].GetString();
            if (symbol.length() >= 6) {
                symbol.insert(symbol.length() - 4, "-");
            }
            snapshot.symbol.assign(symbol);
        }
        
        // Parse bids
        if (data.HasMember("b") && data["b"].IsArray()) {
            const auto& bids = data["b"].GetArray();
            size_t count = std::min(static_cast<size_t>(bids.Size()), static_cast<size_t>(10));
            for (size_t i = 0; i < count; ++i) {
                if (bids[i].IsArray() && bids[i].GetArray().Size() >= 2) {
                    const auto& level = bids[i].GetArray();
                    snapshot.bids[i].price = std::stod(level[0].GetString());
                    snapshot.bids[i].quantity = std::stod(level[1].GetString());
                }
            }
        }
        
        // Parse asks
        if (data.HasMember("a") && data["a"].IsArray()) {
            const auto& asks = data["a"].GetArray();
            size_t count = std::min(static_cast<size_t>(asks.Size()), static_cast<size_t>(10));
            for (size_t i = 0; i < count; ++i) {
                if (asks[i].IsArray() && asks[i].GetArray().Size() >= 2) {
                    const auto& level = asks[i].GetArray();
                    snapshot.asks[i].price = std::stod(level[0].GetString());
                    snapshot.asks[i].quantity = std::stod(level[1].GetString());
                }
            }
        }
        
        return MessageType::BOOK;
    }
    
    return MessageType::UNKNOWN;
}

// ============================================================================
// BinanceExchange Implementation
// ============================================================================

std::string BinanceExchange::generate_subscription(
    const std::vector<std::string>& symbols,
    bool enable_trades,
    bool enable_orderbook
) const {
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    
    writer.StartObject();
    writer.Key("method");
    writer.String("SUBSCRIBE");
    writer.Key("params");
    writer.StartArray();
    
    for (const auto& symbol : symbols) {
        std::string normalized = normalize_symbol(symbol);
        
        if (enable_trades) {
            std::string channel = normalized + "@trade";
            writer.String(channel.c_str());
        }
        
        if (enable_orderbook) {
            std::string channel = normalized + "@depth10";
            writer.String(channel.c_str());
        }
    }
    
    writer.EndArray();
    writer.Key("id");
    writer.Int(1);
    writer.EndObject();
    
    return buffer.GetString();
}

ExchangeInterface::MessageType BinanceExchange::parse_message(
    const std::string& message,
    MarketTick& tick,
    OrderBookSnapshot& snapshot
) const {
    rapidjson::Document doc;
    doc.Parse(message.c_str());
    
    if (doc.HasParseError()) {
        return MessageType::ERROR;
    }
    
    // Check for subscription response
    if (doc.HasMember("result") && doc["result"].IsNull()) {
        return MessageType::HEARTBEAT;
    }
    
    // Parse event type
    if (!doc.HasMember("e") || !doc["e"].IsString()) {
        return MessageType::UNKNOWN;
    }
    
    std::string event_type = doc["e"].GetString();
    
    // Trade message
    if (event_type == "trade") {
        tick.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        tick.exchange.assign("BINANCE");
        
        if (doc.HasMember("s") && doc["s"].IsString()) {
            std::string symbol = doc["s"].GetString();
            // Convert to standard format (btcusdt -> BTC-USDT)
            std::transform(symbol.begin(), symbol.end(), symbol.begin(), ::toupper);
            if (symbol.length() >= 6) {
                symbol.insert(symbol.length() - 4, "-");
            }
            tick.symbol.assign(symbol);
        }
        
        if (doc.HasMember("p") && doc["p"].IsString()) {
            tick.price = std::stod(doc["p"].GetString());
        }
        
        if (doc.HasMember("q") && doc["q"].IsString()) {
            tick.amount = std::stod(doc["q"].GetString());
        }
        
        if (doc.HasMember("m") && doc["m"].IsBool()) {
            tick.side.assign(doc["m"].GetBool() ? "sell" : "buy");
        }
        
        if (doc.HasMember("t") && doc["t"].IsInt64()) {
            tick.trade_id.assign(std::to_string(doc["t"].GetInt64()));
        }
        
        return MessageType::TRADE;
    }
    
    // Orderbook message
    if (event_type == "depthUpdate") {
        snapshot.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        snapshot.exchange.assign("BINANCE");
        
        if (doc.HasMember("s") && doc["s"].IsString()) {
            std::string symbol = doc["s"].GetString();
            std::transform(symbol.begin(), symbol.end(), symbol.begin(), ::toupper);
            if (symbol.length() >= 6) {
                symbol.insert(symbol.length() - 4, "-");
            }
            snapshot.symbol.assign(symbol);
        }
        
        // Parse bids
        if (doc.HasMember("b") && doc["b"].IsArray()) {
            const auto& bids = doc["b"].GetArray();
            size_t count = std::min(static_cast<size_t>(bids.Size()), static_cast<size_t>(10));
            for (size_t i = 0; i < count; ++i) {
                if (bids[i].IsArray() && bids[i].GetArray().Size() >= 2) {
                    const auto& level = bids[i].GetArray();
                    snapshot.bids[i].price = std::stod(level[0].GetString());
                    snapshot.bids[i].quantity = std::stod(level[1].GetString());
                }
            }
        }
        
        // Parse asks
        if (doc.HasMember("a") && doc["a"].IsArray()) {
            const auto& asks = doc["a"].GetArray();
            size_t count = std::min(static_cast<size_t>(asks.Size()), static_cast<size_t>(10));
            for (size_t i = 0; i < count; ++i) {
                if (asks[i].IsArray() && asks[i].GetArray().Size() >= 2) {
                    const auto& level = asks[i].GetArray();
                    snapshot.asks[i].price = std::stod(level[0].GetString());
                    snapshot.asks[i].quantity = std::stod(level[1].GetString());
                }
            }
        }
        
        return MessageType::BOOK;
    }
    
    return MessageType::UNKNOWN;
}

// ============================================================================
// DydxExchange Implementation
// ============================================================================

std::string DydxExchange::generate_subscription(
    const std::vector<std::string>& symbols,
    bool enable_trades,
    bool enable_orderbook
) const {
    // dYdX v4 uses individual subscribe messages, not a batch
    // We'll create a JSON array of subscription messages
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    
    // Start array for multiple subscriptions
    writer.StartArray();
    
    for (const auto& symbol : symbols) {
        std::string normalized = normalize_symbol(symbol);
        
        if (enable_trades) {
            // Subscribe to trades channel
            writer.StartObject();
            writer.Key("type");
            writer.String("subscribe");
            writer.Key("channel");
            writer.String("v4_trades");
            writer.Key("id");
            writer.String(normalized.c_str());
            writer.Key("batched");
            writer.Bool(true);  // Use batched updates
            writer.EndObject();
        }
        
        if (enable_orderbook) {
            // Subscribe to orderbook channel
            writer.StartObject();
            writer.Key("type");
            writer.String("subscribe");
            writer.Key("channel");
            writer.String("v4_orderbook");
            writer.Key("id");
            writer.String(normalized.c_str());
            writer.Key("batched");
            writer.Bool(true);  // Use batched updates
            writer.EndObject();
        }
    }
    
    writer.EndArray();
    
    return buffer.GetString();
}

ExchangeInterface::MessageType DydxExchange::parse_message(
    const std::string& message,
    MarketTick& tick,
    OrderBookSnapshot& snapshot
) const {
    try {
        rapidjson::Document doc;
        doc.Parse(message.c_str());
        
        if (doc.HasParseError()) {
            spdlog::debug("[DydxExchange] JSON parse error at offset {}", doc.GetErrorOffset());
            return MessageType::ERROR;
        }
    
    // Check for subscription response
    if (doc.HasMember("type") && doc["type"].IsString()) {
        std::string type = doc["type"].GetString();
        if (type == "subscribed" || type == "unsubscribed" || type == "connected") {
            return MessageType::HEARTBEAT;
        }
    }
    
    // Parse data messages
    if (!doc.HasMember("channel") || !doc["channel"].IsString()) {
        return MessageType::UNKNOWN;
    }
    
    std::string channel = doc["channel"].GetString();
    
    // Get symbol from id field
    std::string symbol;
    if (doc.HasMember("id") && doc["id"].IsString()) {
        symbol = doc["id"].GetString();
    }
    
    // Trade message (v4_trades)
    if (channel == "v4_trades") {
        if (!doc.HasMember("contents") || !doc["contents"].IsObject()) {
            return MessageType::ERROR;
        }
        
        const auto& contents = doc["contents"];
        
        // Check if it's batched or single trade
        if (contents.HasMember("trades") && contents["trades"].IsArray()) {
            const auto& trades = contents["trades"].GetArray();
            if (trades.Empty()) {
                return MessageType::UNKNOWN;
            }
            
            // Take first trade from batch
            const auto& trade = trades[0];
            
            tick.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count();
            tick.exchange.assign("DYDX");
            tick.symbol.assign(symbol);
            
            if (trade.HasMember("price") && trade["price"].IsString()) {
                tick.price = std::stod(trade["price"].GetString());
            }
            
            if (trade.HasMember("size") && trade["size"].IsString()) {
                tick.amount = std::stod(trade["size"].GetString());
            }
            
            if (trade.HasMember("side") && trade["side"].IsString()) {
                std::string side = trade["side"].GetString();
                tick.side.assign(side == "BUY" ? "buy" : "sell");
            }
            
            if (trade.HasMember("id") && trade["id"].IsString()) {
                tick.trade_id.assign(trade["id"].GetString());
            }
            
            // Parse createdAt timestamp if available
            if (trade.HasMember("createdAt") && trade["createdAt"].IsString()) {
                // ISO 8601 timestamp parsing would go here
                // For now, use current timestamp
            }
            
            return MessageType::TRADE;
        }
    }
    
    // Orderbook message (v4_orderbook)
    if (channel == "v4_orderbook") {
        if (!doc.HasMember("contents")) {
            spdlog::debug("[DydxExchange] Orderbook message missing 'contents' field");
            return MessageType::ERROR;
        }
        
        snapshot.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        snapshot.exchange.assign("DYDX");
        snapshot.symbol.assign(symbol);
        
        // Handle batch format: contents is an array of updates
        const auto& contents_value = doc["contents"];
        
        if (!contents_value.IsArray()) {
            spdlog::debug("[DydxExchange] Contents is not an array for symbol {}", symbol);
            return MessageType::ERROR;
        }
        
        const auto& contents_array = contents_value.GetArray();
        if (contents_array.Empty()) {
            spdlog::debug("[DydxExchange] Empty contents array for {}", symbol);
            return MessageType::UNKNOWN;
        }
        
        // Merge all updates in the batch (dYdX sends incremental updates)
        // We'll collect all bid/ask updates across all batch elements
        std::map<double, double> merged_bids;  // price -> quantity
        std::map<double, double> merged_asks;
        
        for (const auto& update : contents_array) {
            if (!update.IsObject()) continue;
            
            // Parse bids from this update
            if (update.HasMember("bids") && update["bids"].IsArray()) {
                const auto& bids = update["bids"].GetArray();
                for (const auto& level : bids) {
                    if (level.IsArray() && level.GetArray().Size() >= 2) {
                        try {
                            double price = 0.0, quantity = 0.0;
                            if (level[0].IsString()) {
                                price = std::stod(level[0].GetString());
                            }
                            if (level[1].IsString()) {
                                quantity = std::stod(level[1].GetString());
                            }
                            if (price > 0.0) {
                                if (quantity == 0.0) {
                                    // Remove level if quantity is 0
                                    merged_bids.erase(price);
                                } else {
                                    merged_bids[price] = quantity;
                                }
                            }
                        } catch (const std::exception& e) {
                            spdlog::warn("[DydxExchange] Failed to parse bid level: {}", e.what());
                            continue;
                        }
                    }
                }
            }
            
            // Parse asks from this update
            if (update.HasMember("asks") && update["asks"].IsArray()) {
                const auto& asks = update["asks"].GetArray();
                for (const auto& level : asks) {
                    if (level.IsArray() && level.GetArray().Size() >= 2) {
                        try {
                            double price = 0.0, quantity = 0.0;
                            if (level[0].IsString()) {
                                price = std::stod(level[0].GetString());
                            }
                            if (level[1].IsString()) {
                                quantity = std::stod(level[1].GetString());
                            }
                            if (price > 0.0) {
                                if (quantity == 0.0) {
                                    // Remove level if quantity is 0
                                    merged_asks.erase(price);
                                } else {
                                    merged_asks[price] = quantity;
                                }
                            }
                        } catch (const std::exception& e) {
                            spdlog::warn("[DydxExchange] Failed to parse ask level: {}", e.what());
                            continue;
                        }
                    }
                }
            }
        }
        
        // Convert merged bids to snapshot (top 10, highest to lowest)
        size_t bid_idx = 0;
        for (auto it = merged_bids.rbegin(); it != merged_bids.rend() && bid_idx < 10; ++it, ++bid_idx) {
            snapshot.bids[bid_idx].price = it->first;
            snapshot.bids[bid_idx].quantity = it->second;
        }
        
        // Convert merged asks to snapshot (top 10, lowest to highest)
        size_t ask_idx = 0;
        for (auto it = merged_asks.begin(); it != merged_asks.end() && ask_idx < 10; ++it, ++ask_idx) {
            snapshot.asks[ask_idx].price = it->first;
            snapshot.asks[ask_idx].quantity = it->second;
        }
        
 
        // Validate we have at least one bid and one ask
        if (merged_bids.empty() && merged_asks.empty()) {
            spdlog::debug("[DydxExchange] No valid bid/ask levels for {}", symbol);
            return MessageType::UNKNOWN;
        }
        
        return MessageType::BOOK;
    }
    
    return MessageType::UNKNOWN;
    
    } catch (const std::exception& e) {
        spdlog::error("[DydxExchange] Exception in parse_message: {}", e.what());
        return MessageType::ERROR;
    }
}

} // namespace latentspeed
