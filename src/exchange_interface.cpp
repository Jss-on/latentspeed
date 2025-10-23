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
    
    // Check for connection/status messages without data
    if (doc.HasMember("type") && doc["type"].IsString()) {
        std::string type = doc["type"].GetString();
        
        // Only return HEARTBEAT if there's no actual data to parse
        // "subscribed" and "channel_batch_data" contain actual market data in "contents"
        if (type == "connected" || type == "unsubscribed") {
            return MessageType::HEARTBEAT;
        }
        
        // For "subscribed" and "channel_batch_data", continue parsing if contents exist
        if ((type == "subscribed" || type == "channel_batch_data") && 
            !doc.HasMember("contents")) {
            // Acknowledgment without data
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
        if (!doc.HasMember("contents")) {
            spdlog::error("[DydxExchange] Trade message missing 'contents' for {}", symbol);
            return MessageType::ERROR;
        }
        
        const auto& contents = doc["contents"];
        const rapidjson::Value* trades_array = nullptr;
        
        spdlog::debug("[DydxExchange] Trade {} - contents is {}", 
                     symbol, 
                     contents.IsObject() ? "object" : (contents.IsArray() ? "array" : "unknown"));
        
        // Handle two formats:
        // 1. Initial snapshot: contents is object with "trades" array
        // 2. Real-time update: contents is array, each element has "trades" array
        if (contents.IsObject() && contents.HasMember("trades") && contents["trades"].IsArray()) {
            // Format 1: {"contents": {"trades": [...]}}
            trades_array = &contents["trades"];
            spdlog::debug("[DydxExchange] Trade {} - using format 1 (object)", symbol);
        } else if (contents.IsArray() && !contents.GetArray().Empty()) {
            // Format 2: {"contents": [{"trades": [...]}]}
            const auto& first_update = contents[0];
            if (first_update.IsObject() && first_update.HasMember("trades") && first_update["trades"].IsArray()) {
                trades_array = &first_update["trades"];
                spdlog::debug("[DydxExchange] Trade {} - using format 2 (array)", symbol);
            } else {
                spdlog::error("[DydxExchange] Trade {} - format 2 but no trades in first element", symbol);
            }
        } else {
            spdlog::error("[DydxExchange] Trade {} - unrecognized contents format", symbol);
        }
        
        if (trades_array && !trades_array->GetArray().Empty()) {
            // Take first trade from batch
            const auto& trade = (*trades_array)[0];
            
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
        } else {
            spdlog::debug("[DydxExchange] Could not extract trades array from message for {}", symbol);
            return MessageType::ERROR;
        }
    }
    
    // Orderbook message (v4_orderbook)
    if (channel == "v4_orderbook") {
        if (!doc.HasMember("contents")) {
            spdlog::error("[DydxExchange] Orderbook message missing 'contents' field for {}", symbol);
            return MessageType::ERROR;
        }
        
        snapshot.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        snapshot.exchange.assign("DYDX");
        snapshot.symbol.assign(symbol);
        
        const auto& contents_value = doc["contents"];
        
        // Debug: log format type
        spdlog::debug("[DydxExchange] Orderbook {} - contents is {}", 
                     symbol, 
                     contents_value.IsObject() ? "object" : (contents_value.IsArray() ? "array" : "unknown"));
        
        std::map<double, double> merged_bids;  // price -> quantity
        std::map<double, double> merged_asks;
        
        // Handle two formats:
        // 1. Initial snapshot: contents is object with "bids"/"asks" arrays of objects
        // 2. Real-time updates: contents is array of update objects
        
        auto parse_book_levels = [&](const rapidjson::Value& book_obj) {
            // Parse bids from this update
            if (book_obj.HasMember("bids") && book_obj["bids"].IsArray()) {
                const auto& bids = book_obj["bids"].GetArray();
                for (const auto& level : bids) {
                    // Handle both formats: ["price", "size"] or {"price": "...", "size": "..."}
                    double price = 0.0, quantity = 0.0;
                    
                    if (level.IsArray() && level.GetArray().Size() >= 2) {
                        // Format: [["price", "size"], ...]
                        try {
                            if (level[0].IsString()) {
                                price = std::stod(level[0].GetString());
                            }
                            if (level[1].IsString()) {
                                quantity = std::stod(level[1].GetString());
                            }
                        } catch (const std::exception& e) {
                            spdlog::warn("[DydxExchange] Failed to parse bid level (array): {}", e.what());
                            continue;
                        }
                    } else if (level.IsObject()) {
                        // Format: [{"price": "...", "size": "..."}, ...]
                        try {
                            if (level.HasMember("price") && level["price"].IsString()) {
                                price = std::stod(level["price"].GetString());
                            }
                            if (level.HasMember("size") && level["size"].IsString()) {
                                quantity = std::stod(level["size"].GetString());
                            }
                        } catch (const std::exception& e) {
                            spdlog::warn("[DydxExchange] Failed to parse bid level (object): {}", e.what());
                            continue;
                        }
                    }
                    
                    if (price > 0.0) {
                        if (quantity == 0.0) {
                            merged_bids.erase(price);
                        } else {
                            merged_bids[price] = quantity;
                        }
                    }
                }
            }
            
            // Parse asks from this update  
            if (book_obj.HasMember("asks") && book_obj["asks"].IsArray()) {
                const auto& asks = book_obj["asks"].GetArray();
                for (const auto& level : asks) {
                    double price = 0.0, quantity = 0.0;
                    
                    if (level.IsArray() && level.GetArray().Size() >= 2) {
                        try {
                            if (level[0].IsString()) {
                                price = std::stod(level[0].GetString());
                            }
                            if (level[1].IsString()) {
                                quantity = std::stod(level[1].GetString());
                            }
                        } catch (const std::exception& e) {
                            spdlog::warn("[DydxExchange] Failed to parse ask level (array): {}", e.what());
                            continue;
                        }
                    } else if (level.IsObject()) {
                        try {
                            if (level.HasMember("price") && level["price"].IsString()) {
                                price = std::stod(level["price"].GetString());
                            }
                            if (level.HasMember("size") && level["size"].IsString()) {
                                quantity = std::stod(level["size"].GetString());
                            }
                        } catch (const std::exception& e) {
                            spdlog::warn("[DydxExchange] Failed to parse ask level (object): {}", e.what());
                            continue;
                        }
                    }
                    
                    if (price > 0.0) {
                        if (quantity == 0.0) {
                            merged_asks.erase(price);
                        } else {
                            merged_asks[price] = quantity;
                        }
                    }
                }
            }
        };
        
        // Apply the parser to the appropriate format
        if (contents_value.IsObject()) {
            // Format 1: Initial snapshot - {"contents": {"bids": [...], "asks": [...]}}
            parse_book_levels(contents_value);
        } else if (contents_value.IsArray()) {
            // Format 2: Real-time updates - {"contents": [{"bids": [...]}, {"asks": [...]}]}
            for (const auto& update : contents_value.GetArray()) {
                if (update.IsObject()) {
                    parse_book_levels(update);
                }
            }
        } else {
            spdlog::debug("[DydxExchange] Unsupported contents format for {}", symbol);
            return MessageType::ERROR;
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

// ============================================================================
// HyperliquidExchange Implementation
// ============================================================================

std::string HyperliquidExchange::generate_subscription(
    const std::vector<std::string>& symbols,
    bool enable_trades,
    bool enable_orderbook
) const {
    // Hyperliquid uses individual subscribe messages
    // Format: { "method": "subscribe", "subscription": { "type": "...", "coin": "..." } }
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    
    // Start array for multiple subscriptions
    writer.StartArray();
    
    for (const auto& symbol : symbols) {
        std::string normalized = normalize_symbol(symbol);
        
        if (enable_trades) {
            // Subscribe to trades channel
            writer.StartObject();
            writer.Key("method");
            writer.String("subscribe");
            writer.Key("subscription");
            writer.StartObject();
            writer.Key("type");
            writer.String("trades");
            writer.Key("coin");
            writer.String(normalized.c_str());
            writer.EndObject();
            writer.EndObject();
        }
        
        if (enable_orderbook) {
            // Subscribe to l2Book channel
            writer.StartObject();
            writer.Key("method");
            writer.String("subscribe");
            writer.Key("subscription");
            writer.StartObject();
            writer.Key("type");
            writer.String("l2Book");
            writer.Key("coin");
            writer.String(normalized.c_str());
            writer.EndObject();
            writer.EndObject();
        }
    }
    
    writer.EndArray();
    
    return buffer.GetString();
}

ExchangeInterface::MessageType HyperliquidExchange::parse_message(
    const std::string& message,
    MarketTick& tick,
    OrderBookSnapshot& snapshot
) const {
    try {
        rapidjson::Document doc;
        doc.Parse(message.c_str());
        
        if (doc.HasParseError()) {
            spdlog::debug("[HyperliquidExchange] JSON parse error at offset {}", doc.GetErrorOffset());
            return MessageType::ERROR;
        }
        
        // Check for subscription response
        if (doc.HasMember("channel") && doc["channel"].IsString()) {
            std::string channel = doc["channel"].GetString();
            
            if (channel == "subscriptionResponse") {
                return MessageType::HEARTBEAT;
            }
            
            // Parse data messages
            if (!doc.HasMember("data")) {
                return MessageType::UNKNOWN;
            }
            
            const auto& data = doc["data"];
            
            // Trade message (channel: "trades")
            if (channel == "trades") {
                if (!data.IsArray() || data.GetArray().Empty()) {
                    return MessageType::UNKNOWN;
                }
                
                // Take first trade from array
                const auto& trade = data[0];
                
                if (!trade.HasMember("coin") || !trade["coin"].IsString()) {
                    return MessageType::ERROR;
                }
                
                tick.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::system_clock::now().time_since_epoch()
                ).count();
                tick.exchange.assign("HYPERLIQUID");
                {
                    std::string coin = trade["coin"].GetString();
                    // Normalize outbound symbol to BASE-USDC-PERP for cross-venue parity
                    std::transform(coin.begin(), coin.end(), coin.begin(), ::toupper);
                    tick.symbol.assign(coin + "-USDC-PERP");
                }
                
                // Parse price (px)
                if (trade.HasMember("px") && trade["px"].IsString()) {
                    tick.price = std::stod(trade["px"].GetString());
                }
                
                // Parse size (sz)
                if (trade.HasMember("sz") && trade["sz"].IsString()) {
                    tick.amount = std::stod(trade["sz"].GetString());
                }
                
                // Parse side
                if (trade.HasMember("side") && trade["side"].IsString()) {
                    std::string side = trade["side"].GetString();
                    tick.side.assign(side == "A" ? "sell" : "buy"); // A=Ask(sell), B=Bid(buy)
                }
                
                // Parse trade ID (tid)
                if (trade.HasMember("tid") && trade["tid"].IsNumber()) {
                    tick.trade_id.assign(std::to_string(trade["tid"].GetInt64()));
                }
                
                // Keep local receipt timestamp for staleness gating.
                // Hyperliquid 'time' reflects server time and may skew vs local clock,
                // causing false stale triggers downstream. We intentionally do not
                // override tick.timestamp_ns here.
                
                return MessageType::TRADE;
            }
            
            // Order book message (channel: "l2Book")
            if (channel == "l2Book") {
                if (!data.IsObject()) {
                    return MessageType::ERROR;
                }
                
                if (!data.HasMember("coin") || !data["coin"].IsString()) {
                    return MessageType::ERROR;
                }
                
                snapshot.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::system_clock::now().time_since_epoch()
                ).count();
                snapshot.exchange.assign("HYPERLIQUID");
                {
                    std::string coin = data["coin"].GetString();
                    std::transform(coin.begin(), coin.end(), coin.begin(), ::toupper);
                    snapshot.symbol.assign(coin + "-USDC-PERP");
                }
                
                // Keep local receipt timestamp for staleness gating.
                // Hyperliquid 'time' may be skewed; do not override snapshot.timestamp_ns.
                
                // Parse levels: [Array<WsLevel>, Array<WsLevel>]
                // WsLevel format: { px: string, sz: string, n: number }
                if (!data.HasMember("levels") || !data["levels"].IsArray()) {
                    return MessageType::ERROR;
                }
                
                const auto& levels = data["levels"].GetArray();
                if (levels.Size() < 2) {
                    return MessageType::ERROR;
                }
                
                // Parse bids (first array)
                if (levels[0].IsArray()) {
                    const auto& bids = levels[0].GetArray();
                    size_t bid_idx = 0;
                    for (const auto& level : bids) {
                        if (bid_idx >= 10) break; // Take top 10
                        
                        if (level.IsObject() && level.HasMember("px") && level.HasMember("sz")) {
                            if (level["px"].IsString() && level["sz"].IsString()) {
                                snapshot.bids[bid_idx].price = std::stod(level["px"].GetString());
                                snapshot.bids[bid_idx].quantity = std::stod(level["sz"].GetString());
                                bid_idx++;
                            }
                        }
                    }
                }
                
                // Parse asks (second array)
                if (levels[1].IsArray()) {
                    const auto& asks = levels[1].GetArray();
                    size_t ask_idx = 0;
                    for (const auto& level : asks) {
                        if (ask_idx >= 10) break; // Take top 10
                        
                        if (level.IsObject() && level.HasMember("px") && level.HasMember("sz")) {
                            if (level["px"].IsString() && level["sz"].IsString()) {
                                snapshot.asks[ask_idx].price = std::stod(level["px"].GetString());
                                snapshot.asks[ask_idx].quantity = std::stod(level["sz"].GetString());
                                ask_idx++;
                            }
                        }
                    }
                }
                
                return MessageType::BOOK;
            }
        }
        
        return MessageType::UNKNOWN;
        
    } catch (const std::exception& e) {
        spdlog::error("[HyperliquidExchange] Exception in parse_message: {}", e.what());
        return MessageType::ERROR;
    }
}

// ============================================================================
// UNISWAP V4 EXCHANGE (DEX - On-chain via Ethereum Node)
// ============================================================================

std::string UniswapV4Exchange::generate_subscription(
    const std::vector<std::string>& symbols,
    bool enable_trades,
    bool enable_orderbook
) const {
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    
    writer.StartArray();
    
    // Uniswap V4 PoolManager contract address (Mainnet)
    // This is a placeholder - update with actual V4 address when deployed
    const std::string POOL_MANAGER_ADDRESS = "0x0000000000000000000000000000000000000000";
    
    // Swap event signature: Swap(address,address,int256,int256,uint160,uint128,int24)
    const std::string SWAP_EVENT_SIG = "0xc42079f94a6350d7e6235f29174924f928cc2ac818eb64fed8004e115fbcca67";
    
    // ModifyLiquidity event signature for pool state changes
    const std::string LIQUIDITY_EVENT_SIG = "0x3067048beee31b25b2f1681f88dac838c8bba36af25bfb2b7cf7473a5847e35f";
    
    for (const auto& symbol : symbols) {
        // For each symbol/pair, subscribe to relevant events
        
        if (enable_trades) {
            // Subscribe to Swap events (trades)
            writer.StartObject();
            writer.Key("jsonrpc"); writer.String("2.0");
            writer.Key("id"); writer.Int(1);
            writer.Key("method"); writer.String("eth_subscribe");
            writer.Key("params");
            writer.StartArray();
            writer.String("logs");
            writer.StartObject();
            writer.Key("address"); writer.String(POOL_MANAGER_ADDRESS.c_str());
            writer.Key("topics");
            writer.StartArray();
            writer.String(SWAP_EVENT_SIG.c_str());
            writer.EndArray();
            writer.EndObject();
            writer.EndArray();
            writer.EndObject();
        }
        
        if (enable_orderbook) {
            // Subscribe to ModifyLiquidity events (pool state updates)
            writer.StartObject();
            writer.Key("jsonrpc"); writer.String("2.0");
            writer.Key("id"); writer.Int(2);
            writer.Key("method"); writer.String("eth_subscribe");
            writer.Key("params");
            writer.StartArray();
            writer.String("logs");
            writer.StartObject();
            writer.Key("address"); writer.String(POOL_MANAGER_ADDRESS.c_str());
            writer.Key("topics");
            writer.StartArray();
            writer.String(LIQUIDITY_EVENT_SIG.c_str());
            writer.EndArray();
            writer.EndObject();
            writer.EndArray();
            writer.EndObject();
        }
    }
    
    writer.EndArray();
    
    return buffer.GetString();
}

ExchangeInterface::MessageType UniswapV4Exchange::parse_message(
    const std::string& message,
    MarketTick& tick,
    OrderBookSnapshot& snapshot
) const {
    try {
        rapidjson::Document doc;
        doc.Parse(message.c_str());
        
        if (doc.HasParseError()) {
            return MessageType::ERROR;
        }
        
        // Handle JSON-RPC responses
        if (doc.HasMember("method") && doc["method"].IsString()) {
            std::string method = doc["method"].GetString();
            
            // Subscription confirmation
            if (method == "eth_subscription") {
                if (!doc.HasMember("params") || !doc["params"].IsObject()) {
                    return MessageType::HEARTBEAT;
                }
                
                const auto& params = doc["params"];
                if (!params.HasMember("result") || !params["result"].IsObject()) {
                    return MessageType::HEARTBEAT;
                }
                
                const auto& result = params["result"];
                
                // Parse Ethereum log entry
                if (!result.HasMember("topics") || !result["topics"].IsArray()) {
                    return MessageType::UNKNOWN;
                }
                
                const auto& topics = result["topics"];
                if (topics.Empty()) {
                    return MessageType::UNKNOWN;
                }
                
                // Get event signature (first topic)
                std::string event_sig = topics[0].GetString();
                
                // Swap event (trade)
                if (event_sig.find("c42079f94a6350d7e6235f29174924f928cc2ac818eb64fed8004e115fbcca67") != std::string::npos) {
                    // Parse Swap event data
                    if (!result.HasMember("data") || !result["data"].IsString()) {
                        return MessageType::ERROR;
                    }
                    
                    // Decode swap data (simplified - would need full ABI decoding in production)
                    // For now, extract basic info
                    tick.exchange.assign("UNISWAPV4");
                    tick.symbol.assign("WETH/USDC");  // TODO: Decode from pool ID
                    
                    // Would decode amounts from 'data' field
                    // Format: 0x + hex encoded values (amount0, amount1, sqrtPriceX96, liquidity, tick)
                    
                    // Timestamp from block
                    if (result.HasMember("blockNumber") && result["blockNumber"].IsString()) {
                        tick.timestamp_ns = std::chrono::system_clock::now().time_since_epoch().count();
                    }
                    
                    // Parse transaction hash as trade_id
                    if (result.HasMember("transactionHash") && result["transactionHash"].IsString()) {
                        tick.trade_id.assign(result["transactionHash"].GetString());
                    }
                    
                    // Note: Full implementation would decode:
                    // - int256 amount0 (token0 delta)
                    // - int256 amount1 (token1 delta)
                    // - uint160 sqrtPriceX96 (current pool price)
                    // Then calculate: price = (sqrtPriceX96 / 2^96)^2
                    
                    // Placeholder values
                    tick.price = 0.0;
                    tick.amount = 0.0;
                    tick.side.assign("buy");  // Determined by amount signs
                    
                    return MessageType::TRADE;
                }
                
                // ModifyLiquidity event (orderbook update)
                else if (event_sig.find("3067048beee31b25b2f1681f88dac838c8bba36af25bfb2b7cf7473a5847e35f") != std::string::npos) {
                    // Parse liquidity modification
                    snapshot.exchange.assign("UNISWAPV4");
                    snapshot.symbol.assign("WETH/USDC");  // TODO: Decode from pool ID
                    snapshot.timestamp_ns = std::chrono::system_clock::now().time_since_epoch().count();
                    
                    // For AMM pools, we'd query current reserves via eth_call
                    // and construct synthetic orderbook from the constant product curve
                    // This requires additional RPC calls to get pool state
                    
                    // Placeholder: Would calculate synthetic orderbook from AMM curve
                    // Using formula: price = reserve1 / reserve0
                    // And generate price levels around current price
                    
                    return MessageType::BOOK;
                }
            }
        }
        
        // Handle subscription confirmation
        if (doc.HasMember("result") && doc["result"].IsString()) {
            // Subscription ID received
            return MessageType::HEARTBEAT;
        }
        
        return MessageType::UNKNOWN;
        
    } catch (const std::exception& e) {
        spdlog::error("[UniswapV4Exchange] Exception in parse_message: {}", e.what());
        return MessageType::ERROR;
    }
}

} // namespace latentspeed
