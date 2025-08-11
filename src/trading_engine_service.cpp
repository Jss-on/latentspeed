#include "trading_engine_service.h"
#include <iostream>
#include <sstream>
#include <chrono>
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>

namespace latentspeed {

TradingEngineService::TradingEngineService()
    : running_(false)
    , strategy_endpoint_("tcp://*:5555")
    , market_data_endpoint_("tcp://*:5556")
{
}

TradingEngineService::~TradingEngineService() {
    stop();
}

bool TradingEngineService::initialize() {
    try {
        // Initialize ZeroMQ
        zmq_context_ = std::make_unique<zmq::context_t>(1);
        
        // Strategy communication socket (REP - receives commands)
        strategy_socket_ = std::make_unique<zmq::socket_t>(*zmq_context_, ZMQ_REP);
        strategy_socket_->bind(strategy_endpoint_);
        
        // Market data broadcast socket (PUB - sends market data)
        market_data_socket_ = std::make_unique<zmq::socket_t>(*zmq_context_, ZMQ_PUB);
        market_data_socket_->bind(market_data_endpoint_);

        // Initialize ccapi
        session_options_ = std::make_unique<ccapi::SessionOptions>();
        session_configs_ = std::make_unique<ccapi::SessionConfigs>();
        ccapi_session_ = std::make_unique<ccapi::Session>(*session_options_, *session_configs_, this);

        std::cout << "[TradingEngine] Initialized successfully" << std::endl;
        std::cout << "[TradingEngine] Strategy endpoint: " << strategy_endpoint_ << std::endl;
        std::cout << "[TradingEngine] Market data endpoint: " << market_data_endpoint_ << std::endl;
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[TradingEngine] Initialization failed: " << e.what() << std::endl;
        return false;
    }
}

void TradingEngineService::start() {
    if (running_) {
        std::cout << "[TradingEngine] Already running" << std::endl;
        return;
    }

    running_ = true;
    
    // Start ZeroMQ worker thread
    zmq_thread_ = std::make_unique<std::thread>(&TradingEngineService::zmq_worker_thread, this);
    
    std::cout << "[TradingEngine] Service started" << std::endl;
}

void TradingEngineService::stop() {
    if (!running_) {
        return;
    }

    running_ = false;
    
    // Stop ccapi session
    if (ccapi_session_) {
        ccapi_session_->stop();
    }
    
    // Wait for ZeroMQ thread to finish
    if (zmq_thread_ && zmq_thread_->joinable()) {
        zmq_thread_->join();
    }
    
    std::cout << "[TradingEngine] Service stopped" << std::endl;
}

void TradingEngineService::processEvent(const ccapi::Event& event, ccapi::Session* sessionPtr) {
    if (event.getType() == ccapi::Event::Type::SUBSCRIPTION_STATUS) {
        std::cout << "[TradingEngine] Subscription status: " << event.toPrettyString(2, 2) << std::endl;
    } else if (event.getType() == ccapi::Event::Type::SUBSCRIPTION_DATA) {
        // Process market data
        for (const auto& message : event.getMessageList()) {
            MarketDataSnapshot snapshot;
            
            // Extract correlation ID to determine exchange/instrument
            auto correlationIds = message.getCorrelationIdList();
            if (!correlationIds.empty()) {
                std::string corrId = correlationIds[0];
                // Parse correlation ID format: "exchange:instrument"
                size_t pos = corrId.find(':');
                if (pos != std::string::npos) {
                    snapshot.exchange = corrId.substr(0, pos);
                    snapshot.instrument = corrId.substr(pos + 1);
                }
            }
            
            // Extract market data
            for (const auto& element : message.getElementList()) {
                const auto& nameValueMap = element.getNameValueMap();
                
                auto bidPrice = nameValueMap.find("BID_PRICE");
                auto bidSize = nameValueMap.find("BID_SIZE");
                auto askPrice = nameValueMap.find("ASK_PRICE");
                auto askSize = nameValueMap.find("ASK_SIZE");
                
                if (bidPrice != nameValueMap.end()) snapshot.bid_price = std::stod(bidPrice->second);
                if (bidSize != nameValueMap.end()) snapshot.bid_size = std::stod(bidSize->second);
                if (askPrice != nameValueMap.end()) snapshot.ask_price = std::stod(askPrice->second);
                if (askSize != nameValueMap.end()) snapshot.ask_size = std::stod(askSize->second);
            }
            
            snapshot.timestamp = std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
            
            // Broadcast market data via ZeroMQ
            std::string market_data_json = create_response("MARKET_DATA", 
                snapshot.exchange + ":" + snapshot.instrument + ":" + 
                std::to_string(snapshot.bid_price) + ":" + std::to_string(snapshot.ask_price));
            
            zmq::message_t msg(market_data_json.size());
            memcpy(msg.data(), market_data_json.c_str(), market_data_json.size());
            market_data_socket_->send(msg, zmq::send_flags::dontwait);
        }
    } else if (event.getType() == ccapi::Event::Type::RESPONSE) {
        // Handle order execution responses
        std::cout << "[TradingEngine] Order response: " << event.toPrettyString(2, 2) << std::endl;
    }
}

void TradingEngineService::zmq_worker_thread() {
    std::cout << "[TradingEngine] ZeroMQ worker thread started" << std::endl;
    
    while (running_) {
        try {
            zmq::message_t request;
            
            // Poll for messages with timeout
            zmq::pollitem_t items[] = { { *strategy_socket_, 0, ZMQ_POLLIN, 0 } };
            zmq::poll(&items[0], 1, std::chrono::milliseconds(100));
            
            if (items[0].revents & ZMQ_POLLIN) {
                auto result = strategy_socket_->recv(request, zmq::recv_flags::dontwait);
                if (result) {
                    std::string message(static_cast<char*>(request.data()), request.size());
                    handle_strategy_message(message);
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[TradingEngine] ZeroMQ worker error: " << e.what() << std::endl;
        }
    }
    
    std::cout << "[TradingEngine] ZeroMQ worker thread stopped" << std::endl;
}

void TradingEngineService::handle_strategy_message(const std::string& message) {
    std::cout << "[TradingEngine] Received strategy message: " << message << std::endl;
    
    try {
        rapidjson::Document doc;
        doc.Parse(message.c_str());
        
        if (doc.HasParseError()) {
            send_response(create_response("ERROR", "Invalid JSON"));
            return;
        }
        
        if (!doc.HasMember("type") || !doc["type"].IsString()) {
            send_response(create_response("ERROR", "Missing or invalid 'type' field"));
            return;
        }
        
        std::string type = doc["type"].GetString();
        
        if (type == "PLACE_ORDER") {
            OrderRequest order = parse_order_request(message);
            execute_order(order);
            send_response(create_response("ACK", "Order submitted: " + order.correlation_id));
        } else if (type == "SUBSCRIBE_MARKET_DATA") {
            if (doc.HasMember("exchange") && doc.HasMember("instrument")) {
                std::string exchange = doc["exchange"].GetString();
                std::string instrument = doc["instrument"].GetString();
                subscribe_market_data(exchange, instrument);
                send_response(create_response("ACK", "Subscribed to " + exchange + ":" + instrument));
            } else {
                send_response(create_response("ERROR", "Missing exchange or instrument"));
            }
        } else {
            send_response(create_response("ERROR", "Unknown command type: " + type));
        }
    } catch (const std::exception& e) {
        send_response(create_response("ERROR", std::string("Processing error: ") + e.what()));
    }
}

void TradingEngineService::send_response(const std::string& response) {
    zmq::message_t reply(response.size());
    memcpy(reply.data(), response.c_str(), response.size());
    strategy_socket_->send(reply, zmq::send_flags::none);
}

void TradingEngineService::execute_order(const OrderRequest& order) {
    std::cout << "[TradingEngine] Executing order: " << order.exchange << " " 
              << order.instrument << " " << order.side << " " << order.quantity 
              << " @ " << order.price << std::endl;
    
    try {
        ccapi::Request request(ccapi::Request::Operation::CREATE_ORDER, order.exchange, order.instrument, order.correlation_id);
        
        request.appendParam({
            {"SIDE", order.side},
            {"QUANTITY", std::to_string(order.quantity)},
            {"LIMIT_PRICE", std::to_string(order.price)},
            {"ORDER_TYPE", order.order_type}
        });
        
        ccapi_session_->sendRequest(request);
    } catch (const std::exception& e) {
        std::cerr << "[TradingEngine] Order execution error: " << e.what() << std::endl;
    }
}

void TradingEngineService::subscribe_market_data(const std::string& exchange, const std::string& instrument) {
    std::cout << "[TradingEngine] Subscribing to market data: " << exchange << ":" << instrument << std::endl;
    
    try {
        std::string correlation_id = exchange + ":" + instrument;
        ccapi::Subscription subscription(exchange, instrument, "MARKET_DEPTH", "", correlation_id);
        
        active_subscriptions_[correlation_id] = subscription;
        ccapi_session_->subscribe(subscription);
    } catch (const std::exception& e) {
        std::cerr << "[TradingEngine] Market data subscription error: " << e.what() << std::endl;
    }
}

OrderRequest TradingEngineService::parse_order_request(const std::string& json_message) {
    OrderRequest order;
    
    rapidjson::Document doc;
    doc.Parse(json_message.c_str());
    
    if (doc.HasMember("exchange")) order.exchange = doc["exchange"].GetString();
    if (doc.HasMember("instrument")) order.instrument = doc["instrument"].GetString();
    if (doc.HasMember("side")) order.side = doc["side"].GetString();
    if (doc.HasMember("quantity")) order.quantity = doc["quantity"].GetDouble();
    if (doc.HasMember("price")) order.price = doc["price"].GetDouble();
    if (doc.HasMember("order_type")) order.order_type = doc["order_type"].GetString();
    if (doc.HasMember("correlation_id")) order.correlation_id = doc["correlation_id"].GetString();
    
    // Generate correlation ID if not provided
    if (order.correlation_id.empty()) {
        order.correlation_id = "order_" + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    }
    
    return order;
}

std::string TradingEngineService::create_response(const std::string& type, const std::string& data) {
    rapidjson::Document doc;
    doc.SetObject();
    
    rapidjson::Document::AllocatorType& allocator = doc.GetAllocator();
    
    doc.AddMember("type", rapidjson::Value(type.c_str(), allocator), allocator);
    doc.AddMember("data", rapidjson::Value(data.c_str(), allocator), allocator);
    doc.AddMember("timestamp", rapidjson::Value(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count()), allocator);
    
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);
    
    return buffer.GetString();
}

} // namespace latentspeed
