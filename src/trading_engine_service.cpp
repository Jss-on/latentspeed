#include "trading_engine_service.h"
#include <iostream>
#include <sstream>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <unordered_set>
#include <random>
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/error/en.h>

namespace latentspeed {

TradingEngineService::TradingEngineService()
    : running_(false)
    , order_endpoint_("tcp://127.0.0.1:5601")        // Contract-specified endpoint
    , report_endpoint_("tcp://127.0.0.1:5602")       // Contract-specified endpoint
    , gateway_base_url_("http://localhost:8080")      // Default Hummingbot Gateway URL
{
    // Initialize curl for DEX gateway communication
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

TradingEngineService::~TradingEngineService() {
    stop();
    curl_global_cleanup();
}

bool TradingEngineService::initialize() {
    try {
        // Initialize ZeroMQ context
        zmq_context_ = std::make_unique<zmq::context_t>(1);
        
        // Order receiver socket (PULL - receives ExecutionOrders)
        order_receiver_socket_ = std::make_unique<zmq::socket_t>(*zmq_context_, ZMQ_PULL);
        order_receiver_socket_->bind(order_endpoint_);
        
        // Report publisher socket (PUB - sends ExecutionReports and Fills)
        report_publisher_socket_ = std::make_unique<zmq::socket_t>(*zmq_context_, ZMQ_PUB);
        report_publisher_socket_->bind(report_endpoint_);

        // Initialize ccapi for CEX integration
        session_options_ = std::make_unique<ccapi::SessionOptions>();
        session_configs_ = std::make_unique<ccapi::SessionConfigs>();
        ccapi_session_ = std::make_unique<ccapi::Session>(*session_options_, *session_configs_, this);

        std::cout << "[TradingEngine] Initialized successfully" << std::endl;
        std::cout << "[TradingEngine] Order receiver endpoint: " << order_endpoint_ << std::endl;
        std::cout << "[TradingEngine] Report publisher endpoint: " << report_endpoint_ << std::endl;
        std::cout << "[TradingEngine] DEX Gateway URL: " << gateway_base_url_ << std::endl;
        
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
    
    // Start order receiver thread
    order_receiver_thread_ = std::make_unique<std::thread>(&TradingEngineService::zmq_order_receiver_thread, this);
    
    // Start publisher thread
    publisher_thread_ = std::make_unique<std::thread>(&TradingEngineService::zmq_publisher_thread, this);
    
    std::cout << "[TradingEngine] Service started" << std::endl;
}

void TradingEngineService::stop() {
    if (!running_) {
        return;
    }

    running_ = false;
    
    // Notify publisher thread to wake up
    {
        std::lock_guard<std::mutex> lock(publish_mutex_);
        publish_cv_.notify_all();
    }
    
    // Stop ccapi session
    if (ccapi_session_) {
        ccapi_session_->stop();
    }
    
    // Wait for threads to finish
    if (order_receiver_thread_ && order_receiver_thread_->joinable()) {
        order_receiver_thread_->join();
    }
    
    if (publisher_thread_ && publisher_thread_->joinable()) {
        publisher_thread_->join();
    }
    
    std::cout << "[TradingEngine] Service stopped" << std::endl;
}

void TradingEngineService::processEvent(const ccapi::Event& event, ccapi::Session* sessionPtr) {
    if (event.getType() == ccapi::Event::Type::SUBSCRIPTION_STATUS) {
        std::cout << "[TradingEngine] Subscription status: " << event.toPrettyString(2, 2) << std::endl;
    } else if (event.getType() == ccapi::Event::Type::SUBSCRIPTION_DATA) {
        // Process market data - unchanged for now, can be enhanced later
        for (const auto& message : event.getMessageList()) {
            MarketDataSnapshot snapshot;
            
            // Extract correlation ID to determine exchange/instrument
            auto correlationIds = message.getCorrelationIdList();
            if (!correlationIds.empty()) {
                std::string corrId = correlationIds[0];
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
            
            // TODO: Broadcast market data via contract-compliant method
            std::cout << "[TradingEngine] Market data: " << snapshot.exchange << ":" << snapshot.instrument 
                      << " bid=" << snapshot.bid_price << " ask=" << snapshot.ask_price << std::endl;
        }
    } else if (event.getType() == ccapi::Event::Type::RESPONSE) {
        // Handle CEX order responses
        for (const auto& message : event.getMessageList()) {
            auto correlationIds = message.getCorrelationIdList();
            if (!correlationIds.empty()) {
                std::string cl_id = correlationIds[0];
                
                // Create ExecutionReport based on response
                ExecutionReport report;
                report.cl_id = cl_id;
                report.ts_ns = get_current_time_ns();
                
                // Check if order was accepted or rejected
                if (message.getType() == ccapi::Message::Type::CREATE_ORDER) {
                    auto elements = message.getElementList();
                    if (!elements.empty()) {
                        const auto& element = elements[0];
                        const auto& nameValueMap = element.getNameValueMap();
                        
                        auto orderIdIt = nameValueMap.find("ORDER_ID");
                        if (orderIdIt != nameValueMap.end()) {
                            report.status = "accepted";
                            report.reason_code = "ok";
                            report.reason_text = "Order accepted by exchange";
                            report.exchange_order_id = orderIdIt->second;
                            
                            // Store mapping for future reference
                            cex_order_mapping_[cl_id] = orderIdIt->second;
                        } else {
                            report.status = "rejected";
                            report.reason_code = "venue_reject";
                            report.reason_text = "Order rejected by exchange";
                        }
                    }
                } else {
                    report.status = "rejected";
                    report.reason_code = "venue_reject";
                    report.reason_text = "Order rejected by exchange";
                }
                
                publish_execution_report(report);
            }
        }
    }
}

void TradingEngineService::zmq_order_receiver_thread() {
    std::cout << "[TradingEngine] Order receiver thread started" << std::endl;
    
    while (running_) {
        try {
            zmq::message_t request;
            auto result = order_receiver_socket_->recv(request, zmq::recv_flags::dontwait);
            
            if (result.has_value()) {
                std::string message(static_cast<char*>(request.data()), request.size());
                
                // Parse ExecutionOrder from JSON
                ExecutionOrder order = parse_execution_order(message);
                
                // Process the order
                process_execution_order(order);
            }
            
            // Small sleep to prevent busy waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            
        } catch (const zmq::error_t& e) {
            if (e.num() != EAGAIN) {  // EAGAIN is expected with non-blocking recv
                std::cerr << "[TradingEngine] ZeroMQ order receiver error: " << e.what() << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "[TradingEngine] Order receiver thread error: " << e.what() << std::endl;
        }
    }
    
    std::cout << "[TradingEngine] Order receiver thread stopped" << std::endl;
}

void TradingEngineService::zmq_publisher_thread() {
    std::cout << "[TradingEngine] Publisher thread started" << std::endl;
    
    while (running_) {
        std::unique_lock<std::mutex> lock(publish_mutex_);
        
        // Wait for messages to publish or shutdown signal
        publish_cv_.wait(lock, [this] { return !publish_queue_.empty() || !running_; });
        
        while (!publish_queue_.empty()) {
            std::string message = publish_queue_.front();
            publish_queue_.pop();
            
            lock.unlock();
            
            try {
                zmq::message_t zmq_msg(message.size());
                memcpy(zmq_msg.data(), message.c_str(), message.size());
                report_publisher_socket_->send(zmq_msg, zmq::send_flags::dontwait);
            } catch (const std::exception& e) {
                std::cerr << "[TradingEngine] Publisher error: " << e.what() << std::endl;
            }
            
            lock.lock();
        }
    }
    
    std::cout << "[TradingEngine] Publisher thread stopped" << std::endl;
}

void TradingEngineService::process_execution_order(const ExecutionOrder& order) {
    try {
        // Check for duplicate orders (idempotency)
        if (is_duplicate_order(order.cl_id)) {
            std::cout << "[TradingEngine] Ignoring duplicate order: " << order.cl_id << std::endl;
            return;
        }
        
        // Mark order as processed
        mark_order_processed(order.cl_id);
        
        // Store pending order for tracking
        pending_orders_[order.cl_id] = order;
        
        std::cout << "[TradingEngine] Processing " << order.action << " order: " 
                  << order.venue_type << "/" << order.product_type << " cl_id=" << order.cl_id << std::endl;
        
        // Route based on action and product type
        if (order.action == "place") {
            if (order.venue_type == "cex") {
                if (order.product_type == "spot" || order.product_type == "perpetual") {
                    const auto& details = std::get<CexOrderDetails>(order.details);
                    handle_cex_order(order, details);
                }
            } else if (order.venue_type == "dex") {
                if (order.product_type == "amm_swap") {
                    const auto& details = std::get<AmmSwapDetails>(order.details);
                    handle_amm_swap(order, details);
                } else if (order.product_type == "clmm_swap") {
                    const auto& details = std::get<ClmmSwapDetails>(order.details);
                    handle_clmm_swap(order, details);
                }
            } else if (order.venue_type == "chain") {
                if (order.product_type == "transfer") {
                    const auto& details = std::get<TransferDetails>(order.details);
                    handle_transfer(order, details);
                }
            }
        } else if (order.action == "cancel") {
            const auto& details = std::get<CancelDetails>(order.details);
            handle_cancel(order, details);
        } else if (order.action == "replace") {
            const auto& details = std::get<ReplaceDetails>(order.details);
            handle_replace(order, details);
        } else {
            // Reject unknown action
            ExecutionReport report;
            report.cl_id = order.cl_id;
            report.status = "rejected";
            report.reason_code = "invalid_params";
            report.reason_text = "Unknown action: " + order.action;
            report.ts_ns = get_current_time_ns();
            publish_execution_report(report);
        }
        
    } catch (const std::exception& e) {
        // Send rejection report
        ExecutionReport report;
        report.cl_id = order.cl_id;
        report.status = "rejected";
        report.reason_code = "invalid_params";
        report.reason_text = std::string("Processing error: ") + e.what();
        report.ts_ns = get_current_time_ns();
        publish_execution_report(report);
        
        std::cerr << "[TradingEngine] Order processing error: " << e.what() << std::endl;
    }
}

void TradingEngineService::handle_cex_order(const ExecutionOrder& order, const CexOrderDetails& details) {
    try {
        // Create ccapi request for CEX order
        ccapi::Request request(ccapi::Request::Operation::CREATE_ORDER, order.venue, details.symbol, order.cl_id);
        
        // Map order parameters
        std::map<std::string, std::string> params = details.params;
        params["SIDE"] = details.side == "buy" ? "BUY" : "SELL";
        params["QUANTITY"] = std::to_string(details.size);
        params["ORDER_TYPE"] = details.order_type;
        
        if (details.price.has_value()) {
            params["LIMIT_PRICE"] = std::to_string(details.price.value());
        }
        if (details.stop_price.has_value()) {
            params["STOP_PRICE"] = std::to_string(details.stop_price.value());
        }
        if (details.time_in_force != "gtc") {  // gtc is usually default
            params["TIME_IN_FORCE"] = details.time_in_force;
        }
        if (details.reduce_only) {
            params["REDUCE_ONLY"] = "true";
        }
        if (details.margin_mode.has_value()) {
            params["MARGIN_MODE"] = details.margin_mode.value();
        }
        
        request.appendParam(params);
        
        // Send request to ccapi
        ccapi_session_->sendRequest(request);
        
        std::cout << "[TradingEngine] CEX order submitted: " << details.symbol << " " 
                  << details.side << " " << details.size << std::endl;
        
    } catch (const std::exception& e) {
        // Send rejection report
        ExecutionReport report;
        report.cl_id = order.cl_id;
        report.status = "rejected";
        report.reason_code = "network_error";
        report.reason_text = std::string("CEX order error: ") + e.what();
        report.ts_ns = get_current_time_ns();
        publish_execution_report(report);
    }
}

void TradingEngineService::handle_amm_swap(const ExecutionOrder& order, const AmmSwapDetails& details) {
    try {
        // Build JSON payload for Hummingbot Gateway AMM swap
        rapidjson::Document doc;
        doc.SetObject();
        auto& allocator = doc.GetAllocator();
        
        doc.AddMember("chain", rapidjson::Value(details.chain.c_str(), allocator), allocator);
        doc.AddMember("connector", rapidjson::Value(details.protocol.c_str(), allocator), allocator);
        doc.AddMember("tokenIn", rapidjson::Value(details.token_in.c_str(), allocator), allocator);
        doc.AddMember("tokenOut", rapidjson::Value(details.token_out.c_str(), allocator), allocator);
        doc.AddMember("amount", rapidjson::Value(details.trade_mode == "exact_in" ? 
            details.amount_in.value() : details.amount_out.value()), allocator);
        doc.AddMember("side", rapidjson::Value(details.trade_mode.c_str(), allocator), allocator);
        doc.AddMember("allowedSlippage", rapidjson::Value(details.slippage_bps / 100.0), allocator);  // Convert bps to percentage
        
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        doc.Accept(writer);
        
        // Call gateway API
        std::string response = call_gateway_api("/amm/trade", buffer.GetString());
        
        // Parse response and send appropriate ExecutionReport
        rapidjson::Document resp_doc;
        resp_doc.Parse(response.c_str());
        
        ExecutionReport report;
        report.cl_id = order.cl_id;
        report.ts_ns = get_current_time_ns();
        
        if (resp_doc.HasMember("txHash")) {
            report.status = "accepted";
            report.reason_code = "ok";
            report.reason_text = "AMM swap submitted to blockchain";
            report.exchange_order_id = resp_doc["txHash"].GetString();
        } else {
            report.status = "rejected";
            report.reason_code = "venue_reject";
            report.reason_text = "AMM swap failed";
        }
        
        publish_execution_report(report);
        
    } catch (const std::exception& e) {
        ExecutionReport report;
        report.cl_id = order.cl_id;
        report.status = "rejected";
        report.reason_code = "network_error";
        report.reason_text = std::string("AMM swap error: ") + e.what();
        report.ts_ns = get_current_time_ns();
        publish_execution_report(report);
    }
}

void TradingEngineService::handle_clmm_swap(const ExecutionOrder& order, const ClmmSwapDetails& details) {
    try {
        // Build JSON payload for Hummingbot Gateway CLMM swap
        rapidjson::Document doc;
        doc.SetObject();
        auto& allocator = doc.GetAllocator();
        
        doc.AddMember("chain", rapidjson::Value(details.chain.c_str(), allocator), allocator);
        doc.AddMember("connector", rapidjson::Value(details.protocol.c_str(), allocator), allocator);
        doc.AddMember("token0", rapidjson::Value(details.pool.token0.c_str(), allocator), allocator);
        doc.AddMember("token1", rapidjson::Value(details.pool.token1.c_str(), allocator), allocator);
        doc.AddMember("fee", rapidjson::Value(details.pool.fee_tier_bps), allocator);
        doc.AddMember("amount", rapidjson::Value(details.trade_mode == "exact_in" ? 
            details.amount_in.value() : details.amount_out.value()), allocator);
        doc.AddMember("side", rapidjson::Value(details.trade_mode.c_str(), allocator), allocator);
        doc.AddMember("allowedSlippage", rapidjson::Value(details.slippage_bps / 100.0), allocator);
        
        if (details.price_limit.has_value()) {
            doc.AddMember("priceLimit", rapidjson::Value(details.price_limit.value()), allocator);
        }
        
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        doc.Accept(writer);
        
        // Call gateway API
        std::string response = call_gateway_api("/clmm/trade", buffer.GetString());
        
        // Parse response and send appropriate ExecutionReport
        rapidjson::Document resp_doc;
        resp_doc.Parse(response.c_str());
        
        ExecutionReport report;
        report.cl_id = order.cl_id;
        report.ts_ns = get_current_time_ns();
        
        if (resp_doc.HasMember("txHash")) {
            report.status = "accepted";
            report.reason_code = "ok";
            report.reason_text = "CLMM swap submitted to blockchain";
            report.exchange_order_id = resp_doc["txHash"].GetString();
        } else {
            report.status = "rejected";
            report.reason_code = "venue_reject";
            report.reason_text = "CLMM swap failed";
        }
        
        publish_execution_report(report);
        
    } catch (const std::exception& e) {
        ExecutionReport report;
        report.cl_id = order.cl_id;
        report.status = "rejected";
        report.reason_code = "network_error";
        report.reason_text = std::string("CLMM swap error: ") + e.what();
        report.ts_ns = get_current_time_ns();
        publish_execution_report(report);
    }
}

void TradingEngineService::handle_transfer(const ExecutionOrder& order, const TransferDetails& details) {
    try {
        // Build JSON payload for chain transfer
        rapidjson::Document doc;
        doc.SetObject();
        auto& allocator = doc.GetAllocator();
        
        doc.AddMember("chain", rapidjson::Value(details.chain.c_str(), allocator), allocator);
        doc.AddMember("token", rapidjson::Value(details.token.c_str(), allocator), allocator);
        doc.AddMember("amount", rapidjson::Value(details.amount), allocator);
        doc.AddMember("to", rapidjson::Value(details.to_address.c_str(), allocator), allocator);
        
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        doc.Accept(writer);
        
        // Call gateway API
        std::string response = call_gateway_api("/chain/transfer", buffer.GetString());
        
        // Parse response
        rapidjson::Document resp_doc;
        resp_doc.Parse(response.c_str());
        
        ExecutionReport report;
        report.cl_id = order.cl_id;
        report.ts_ns = get_current_time_ns();
        
        if (resp_doc.HasMember("txHash")) {
            report.status = "accepted";
            report.reason_code = "ok";
            report.reason_text = "Transfer submitted to blockchain";
            report.exchange_order_id = resp_doc["txHash"].GetString();
        } else {
            report.status = "rejected";
            report.reason_code = "venue_reject";
            report.reason_text = "Transfer failed";
        }
        
        publish_execution_report(report);
        
    } catch (const std::exception& e) {
        ExecutionReport report;
        report.cl_id = order.cl_id;
        report.status = "rejected";
        report.reason_code = "network_error";
        report.reason_text = std::string("Transfer error: ") + e.what();
        report.ts_ns = get_current_time_ns();
        publish_execution_report(report);
    }
}

void TradingEngineService::handle_cancel(const ExecutionOrder& order, const CancelDetails& details) {
    try {
        // CEX cancel order
        ccapi::Request request(ccapi::Request::Operation::CANCEL_ORDER, order.venue, details.symbol, order.cl_id);
        
        std::map<std::string, std::string> params;
        if (!details.cl_id_to_cancel.empty()) {
            params["CLIENT_ORDER_ID"] = details.cl_id_to_cancel;
        }
        if (details.exchange_order_id.has_value()) {
            params["ORDER_ID"] = details.exchange_order_id.value();
        }
        
        request.appendParam(params);
        ccapi_session_->sendRequest(request);
        
    } catch (const std::exception& e) {
        ExecutionReport report;
        report.cl_id = order.cl_id;
        report.status = "rejected";
        report.reason_code = "network_error";
        report.reason_text = std::string("Cancel error: ") + e.what();
        report.ts_ns = get_current_time_ns();
        publish_execution_report(report);
    }
}

void TradingEngineService::handle_replace(const ExecutionOrder& order, const ReplaceDetails& details) {
    try {
        // CEX replace order - most exchanges implement as cancel + new order
        // For now, send rejection as many exchanges don't support direct modify
        ExecutionReport report;
        report.cl_id = order.cl_id;
        report.status = "rejected";
        report.reason_code = "venue_reject";
        report.reason_text = "Order replacement not yet supported - use cancel + new order";
        report.ts_ns = get_current_time_ns();
        publish_execution_report(report);
        
    } catch (const std::exception& e) {
        ExecutionReport report;
        report.cl_id = order.cl_id;
        report.status = "rejected";
        report.reason_code = "network_error";
        report.reason_text = std::string("Replace error: ") + e.what();
        report.ts_ns = get_current_time_ns();
        publish_execution_report(report);
    }
}

// Publishing methods
void TradingEngineService::publish_execution_report(const ExecutionReport& report) {
    try {
        std::string json_report = serialize_execution_report(report);
        std::string topic_message = "exec.report " + json_report;
        
        std::lock_guard<std::mutex> lock(publish_mutex_);
        publish_queue_.push(topic_message);
        publish_cv_.notify_one();
        
    } catch (const std::exception& e) {
        std::cerr << "[TradingEngine] Error publishing execution report: " << e.what() << std::endl;
    }
}

void TradingEngineService::publish_fill(const Fill& fill) {
    try {
        std::string json_fill = serialize_fill(fill);
        std::string topic_message = "exec.fill " + json_fill;
        
        std::lock_guard<std::mutex> lock(publish_mutex_);
        publish_queue_.push(topic_message);
        publish_cv_.notify_one();
        
    } catch (const std::exception& e) {
        std::cerr << "[TradingEngine] Error publishing fill: " << e.what() << std::endl;
    }
}

// DEX Gateway communication
std::string TradingEngineService::call_gateway_api(const std::string& endpoint, const std::string& json_payload) {
    CURL* curl = curl_easy_init();
    std::string response_string;
    
    if (curl) {
        // Set callback for response data
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, [](char* data, size_t size, size_t nmemb, void* userdata) -> size_t {
            size_t real_size = size * nmemb;
            std::string* response = static_cast<std::string*>(userdata);
            response->append(data, real_size);
            return real_size;
        });
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_string);
        
        // Set URL and headers
        std::string full_url = gateway_base_url_ + endpoint;
        curl_easy_setopt(curl, CURLOPT_URL, full_url.c_str());
        
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        
        // Set POST data
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_payload.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, json_payload.length());
        
        // Perform request
        CURLcode res = curl_easy_perform(curl);
        
        // Cleanup
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        
        if (res != CURLE_OK) {
            throw std::runtime_error("Gateway API call failed: " + std::string(curl_easy_strerror(res)));
        }
    } else {
        throw std::runtime_error("Failed to initialize curl");
    }
    
    return response_string;
}

// Utility methods
uint64_t TradingEngineService::get_current_time_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string TradingEngineService::generate_exec_id() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(100000, 999999);
    
    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    
    return "exec_" + std::to_string(millis) + "_" + std::to_string(dis(gen));
}

bool TradingEngineService::is_duplicate_order(const std::string& cl_id) {
    return processed_orders_.find(cl_id) != processed_orders_.end();
}

void TradingEngineService::mark_order_processed(const std::string& cl_id) {
    processed_orders_.insert(cl_id);
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

// Contract-compliant parsing and serialization methods
ExecutionOrder TradingEngineService::parse_execution_order(const std::string& json_message) {
    ExecutionOrder order;
    
    rapidjson::Document doc;
    doc.Parse(json_message.c_str());
    
    if (doc.HasParseError()) {
        throw std::runtime_error("Failed to parse ExecutionOrder JSON: " + std::string(rapidjson::GetParseError_En(doc.GetParseError())));
    }
    
    // Parse top-level fields
    if (doc.HasMember("version")) order.version = doc["version"].GetInt();
    if (doc.HasMember("cl_id")) order.cl_id = doc["cl_id"].GetString();
    if (doc.HasMember("action")) order.action = doc["action"].GetString();
    if (doc.HasMember("venue_type")) order.venue_type = doc["venue_type"].GetString();
    if (doc.HasMember("venue")) order.venue = doc["venue"].GetString();
    if (doc.HasMember("product_type")) order.product_type = doc["product_type"].GetString();
    if (doc.HasMember("ts_ns")) order.ts_ns = doc["ts_ns"].GetUint64();
    
    // Parse tags
    if (doc.HasMember("tags") && doc["tags"].IsObject()) {
        for (auto it = doc["tags"].MemberBegin(); it != doc["tags"].MemberEnd(); ++it) {
            order.tags[it->name.GetString()] = it->value.GetString();
        }
    }
    
    // Parse details based on product type and action
    if (doc.HasMember("details") && doc["details"].IsObject()) {
        const auto& details = doc["details"];
        
        if (order.action == "place") {
            if (order.venue_type == "cex" && (order.product_type == "spot" || order.product_type == "perpetual")) {
                CexOrderDetails cex_details;
                if (details.HasMember("symbol")) cex_details.symbol = details["symbol"].GetString();
                if (details.HasMember("side")) cex_details.side = details["side"].GetString();
                if (details.HasMember("order_type")) cex_details.order_type = details["order_type"].GetString();
                if (details.HasMember("time_in_force")) cex_details.time_in_force = details["time_in_force"].GetString();
                if (details.HasMember("size")) cex_details.size = details["size"].GetDouble();
                if (details.HasMember("price")) cex_details.price = details["price"].GetDouble();
                if (details.HasMember("stop_price")) cex_details.stop_price = details["stop_price"].GetDouble();
                if (details.HasMember("reduce_only")) cex_details.reduce_only = details["reduce_only"].GetBool();
                if (details.HasMember("margin_mode")) cex_details.margin_mode = details["margin_mode"].GetString();
                
                // Parse params
                if (details.HasMember("params") && details["params"].IsObject()) {
                    for (auto it = details["params"].MemberBegin(); it != details["params"].MemberEnd(); ++it) {
                        cex_details.params[it->name.GetString()] = it->value.GetString();
                    }
                }
                order.details = cex_details;
                
            } else if (order.venue_type == "dex" && order.product_type == "amm_swap") {
                AmmSwapDetails amm_details;
                if (details.HasMember("chain")) amm_details.chain = details["chain"].GetString();
                if (details.HasMember("protocol")) amm_details.protocol = details["protocol"].GetString();
                if (details.HasMember("token_in")) amm_details.token_in = details["token_in"].GetString();
                if (details.HasMember("token_out")) amm_details.token_out = details["token_out"].GetString();
                if (details.HasMember("trade_mode")) amm_details.trade_mode = details["trade_mode"].GetString();
                if (details.HasMember("amount_in")) amm_details.amount_in = details["amount_in"].GetDouble();
                if (details.HasMember("amount_out")) amm_details.amount_out = details["amount_out"].GetDouble();
                if (details.HasMember("slippage_bps")) amm_details.slippage_bps = details["slippage_bps"].GetInt();
                if (details.HasMember("deadline_sec")) amm_details.deadline_sec = details["deadline_sec"].GetInt();
                if (details.HasMember("recipient")) amm_details.recipient = details["recipient"].GetString();
                
                // Parse route
                if (details.HasMember("route") && details["route"].IsArray()) {
                    for (auto& v : details["route"].GetArray()) {
                        amm_details.route.push_back(v.GetString());
                    }
                }
                order.details = amm_details;
                
            } else if (order.venue_type == "dex" && order.product_type == "clmm_swap") {
                ClmmSwapDetails clmm_details;
                if (details.HasMember("chain")) clmm_details.chain = details["chain"].GetString();
                if (details.HasMember("protocol")) clmm_details.protocol = details["protocol"].GetString();
                if (details.HasMember("trade_mode")) clmm_details.trade_mode = details["trade_mode"].GetString();
                if (details.HasMember("amount_in")) clmm_details.amount_in = details["amount_in"].GetDouble();
                if (details.HasMember("amount_out")) clmm_details.amount_out = details["amount_out"].GetDouble();
                if (details.HasMember("slippage_bps")) clmm_details.slippage_bps = details["slippage_bps"].GetInt();
                if (details.HasMember("price_limit")) clmm_details.price_limit = details["price_limit"].GetDouble();
                if (details.HasMember("deadline_sec")) clmm_details.deadline_sec = details["deadline_sec"].GetInt();
                if (details.HasMember("recipient")) clmm_details.recipient = details["recipient"].GetString();
                
                // Parse pool details
                if (details.HasMember("pool") && details["pool"].IsObject()) {
                    const auto& pool = details["pool"];
                    if (pool.HasMember("token0")) clmm_details.pool.token0 = pool["token0"].GetString();
                    if (pool.HasMember("token1")) clmm_details.pool.token1 = pool["token1"].GetString();
                    if (pool.HasMember("fee_tier_bps")) clmm_details.pool.fee_tier_bps = pool["fee_tier_bps"].GetInt();
                }
                order.details = clmm_details;
                
            } else if (order.venue_type == "chain" && order.product_type == "transfer") {
                TransferDetails transfer_details;
                if (details.HasMember("chain")) transfer_details.chain = details["chain"].GetString();
                if (details.HasMember("token")) transfer_details.token = details["token"].GetString();
                if (details.HasMember("amount")) transfer_details.amount = details["amount"].GetDouble();
                if (details.HasMember("to_address")) transfer_details.to_address = details["to_address"].GetString();
                order.details = transfer_details;
            }
        } else if (order.action == "cancel") {
            CancelDetails cancel_details;
            if (details.HasMember("symbol")) cancel_details.symbol = details["symbol"].GetString();
            if (details.HasMember("cl_id_to_cancel")) cancel_details.cl_id_to_cancel = details["cl_id_to_cancel"].GetString();
            if (details.HasMember("exchange_order_id")) cancel_details.exchange_order_id = details["exchange_order_id"].GetString();
            order.details = cancel_details;
            
        } else if (order.action == "replace") {
            ReplaceDetails replace_details;
            if (details.HasMember("symbol")) replace_details.symbol = details["symbol"].GetString();
            if (details.HasMember("cl_id_to_replace")) replace_details.cl_id_to_replace = details["cl_id_to_replace"].GetString();
            if (details.HasMember("new_price")) replace_details.new_price = details["new_price"].GetDouble();
            if (details.HasMember("new_size")) replace_details.new_size = details["new_size"].GetDouble();
            order.details = replace_details;
        }
    }
    
    return order;
}

std::string TradingEngineService::serialize_execution_report(const ExecutionReport& report) {
    rapidjson::Document doc;
    doc.SetObject();
    auto& allocator = doc.GetAllocator();
    
    doc.AddMember("version", rapidjson::Value(report.version), allocator);
    doc.AddMember("cl_id", rapidjson::Value(report.cl_id.c_str(), allocator), allocator);
    doc.AddMember("status", rapidjson::Value(report.status.c_str(), allocator), allocator);
    
    if (report.exchange_order_id.has_value()) {
        doc.AddMember("exchange_order_id", rapidjson::Value(report.exchange_order_id.value().c_str(), allocator), allocator);
    } else {
        doc.AddMember("exchange_order_id", rapidjson::Value().SetNull(), allocator);
    }
    
    doc.AddMember("reason_code", rapidjson::Value(report.reason_code.c_str(), allocator), allocator);
    doc.AddMember("reason_text", rapidjson::Value(report.reason_text.c_str(), allocator), allocator);
    doc.AddMember("ts_ns", rapidjson::Value(report.ts_ns), allocator);
    
    // Add tags
    rapidjson::Value tags_obj(rapidjson::kObjectType);
    for (const auto& tag : report.tags) {
        tags_obj.AddMember(
            rapidjson::Value(tag.first.c_str(), allocator),
            rapidjson::Value(tag.second.c_str(), allocator),
            allocator
        );
    }
    doc.AddMember("tags", tags_obj, allocator);
    
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);
    
    return buffer.GetString();
}

std::string TradingEngineService::serialize_fill(const Fill& fill) {
    rapidjson::Document doc;
    doc.SetObject();
    auto& allocator = doc.GetAllocator();
    
    doc.AddMember("version", rapidjson::Value(fill.version), allocator);
    doc.AddMember("cl_id", rapidjson::Value(fill.cl_id.c_str(), allocator), allocator);
    
    if (fill.exchange_order_id.has_value()) {
        doc.AddMember("exchange_order_id", rapidjson::Value(fill.exchange_order_id.value().c_str(), allocator), allocator);
    } else {
        doc.AddMember("exchange_order_id", rapidjson::Value().SetNull(), allocator);
    }
    
    doc.AddMember("exec_id", rapidjson::Value(fill.exec_id.c_str(), allocator), allocator);
    doc.AddMember("symbol_or_pair", rapidjson::Value(fill.symbol_or_pair.c_str(), allocator), allocator);
    doc.AddMember("price", rapidjson::Value(fill.price), allocator);
    doc.AddMember("size", rapidjson::Value(fill.size), allocator);
    doc.AddMember("fee_currency", rapidjson::Value(fill.fee_currency.c_str(), allocator), allocator);
    doc.AddMember("fee_amount", rapidjson::Value(fill.fee_amount), allocator);
    
    if (fill.liquidity.has_value()) {
        doc.AddMember("liquidity", rapidjson::Value(fill.liquidity.value().c_str(), allocator), allocator);
    } else {
        doc.AddMember("liquidity", rapidjson::Value().SetNull(), allocator);
    }
    
    doc.AddMember("ts_ns", rapidjson::Value(fill.ts_ns), allocator);
    
    // Add tags
    rapidjson::Value tags_obj(rapidjson::kObjectType);
    for (const auto& tag : fill.tags) {
        tags_obj.AddMember(
            rapidjson::Value(tag.first.c_str(), allocator),
            rapidjson::Value(tag.second.c_str(), allocator),
            allocator
        );
    }
    doc.AddMember("tags", tags_obj, allocator);
    
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);
    
    return buffer.GetString();
}

} // namespace latentspeed
