/**
 * @file zmq_order_event_publisher.cpp
 * @brief Implementation of ZMQOrderEventPublisher
 */

#include "connector/zmq_order_event_publisher.h"
#include <spdlog/spdlog.h>

namespace latentspeed {

// ============================================================================
// CONSTRUCTOR
// ============================================================================

ZMQOrderEventPublisher::ZMQOrderEventPublisher(
    std::shared_ptr<zmq::context_t> context,
    const std::string& endpoint,
    const std::string& topic_prefix
)   : context_(context),
      endpoint_(endpoint),
      topic_prefix_(topic_prefix),
      publisher_(*context_, zmq::socket_type::pub) {
    
    if (!context_) {
        throw std::invalid_argument("ZMQ context cannot be null");
    }
    
    try {
        publisher_.bind(endpoint_);
        spdlog::info("ZMQOrderEventPublisher: Bound to {}", endpoint_);
    } catch (const zmq::error_t& e) {
        spdlog::error("ZMQOrderEventPublisher: Failed to bind to {}: {}", endpoint_, e.what());
        throw;
    }
}

// ============================================================================
// PUBLIC METHODS - Event Publishing
// ============================================================================

void ZMQOrderEventPublisher::publish_order_created(const connector::InFlightOrder& order) {
    nlohmann::json event;
    event["event_type"] = "order_created";
    event["timestamp"] = std::chrono::system_clock::now().time_since_epoch().count();
    event["data"] = order_to_json(order);
    
    publish_event("created", event);
}

void ZMQOrderEventPublisher::publish_order_filled(const connector::InFlightOrder& order) {
    nlohmann::json event;
    event["event_type"] = "order_filled";
    event["timestamp"] = std::chrono::system_clock::now().time_since_epoch().count();
    event["data"] = order_to_json(order);
    
    publish_event("filled", event);
}

void ZMQOrderEventPublisher::publish_order_cancelled(const connector::InFlightOrder& order) {
    nlohmann::json event;
    event["event_type"] = "order_cancelled";
    event["timestamp"] = std::chrono::system_clock::now().time_since_epoch().count();
    event["data"] = order_to_json(order);
    
    publish_event("cancelled", event);
}

void ZMQOrderEventPublisher::publish_order_failed(const connector::InFlightOrder& order, const std::string& reason) {
    nlohmann::json event;
    event["event_type"] = "order_failed";
    event["timestamp"] = std::chrono::system_clock::now().time_since_epoch().count();
    event["data"] = order_to_json(order);
    event["failure_reason"] = reason;
    
    publish_event("failed", event);
}

void ZMQOrderEventPublisher::publish_order_partially_filled(const connector::InFlightOrder& order, const connector::TradeUpdate& trade) {
    nlohmann::json event;
    event["event_type"] = "order_partially_filled";
    event["timestamp"] = std::chrono::system_clock::now().time_since_epoch().count();
    event["data"] = order_to_json(order);
    event["trade"] = trade_to_json(trade);
    
    publish_event("partial_fill", event);
}

void ZMQOrderEventPublisher::publish_order_update(const connector::InFlightOrder& order) {
    nlohmann::json event;
    event["event_type"] = "order_update";
    event["timestamp"] = std::chrono::system_clock::now().time_since_epoch().count();
    event["data"] = order_to_json(order);
    
    publish_event("update", event);
}

// ============================================================================
// PRIVATE METHODS
// ============================================================================

void ZMQOrderEventPublisher::publish_event(const std::string& subtopic, const nlohmann::json& event) {
    try {
        // Topic format: "orders.hyperliquid.created"
        std::string topic = topic_prefix_ + "." + subtopic;
        std::string message_str = event.dump();
        
        // Send topic (multipart message)
        zmq::message_t topic_msg(topic.data(), topic.size());
        publisher_.send(topic_msg, zmq::send_flags::sndmore);
        
        // Send message body
        zmq::message_t body_msg(message_str.data(), message_str.size());
        publisher_.send(body_msg, zmq::send_flags::none);
        
        spdlog::debug("ZMQ published: topic={}, size={}", topic, message_str.size());
        
    } catch (const zmq::error_t& e) {
        spdlog::error("ZMQ publish error: {}", e.what());
    } catch (const std::exception& e) {
        spdlog::error("ZMQ publish error: {}", e.what());
    }
}

nlohmann::json ZMQOrderEventPublisher::order_to_json(const connector::InFlightOrder& order) const {
    nlohmann::json j;
    
    j["client_order_id"] = order.client_order_id;
    j["exchange_order_id"] = order.exchange_order_id.value_or("");
    j["trading_pair"] = order.trading_pair;
    j["order_type"] = connector::to_string(order.order_type);
    j["trade_type"] = connector::to_string(order.trade_type);
    j["price"] = order.price;
    j["amount"] = order.amount;
    j["filled_amount"] = order.filled_amount;
    j["average_fill_price"] = order.average_fill_price;
    j["order_state"] = connector::to_string(order.current_state);
    j["creation_timestamp"] = order.creation_timestamp;
    j["last_update_timestamp"] = order.last_update_timestamp;
    j["remaining_amount"] = order.remaining_amount();
    j["is_done"] = order.is_done();
    
    return j;
}

nlohmann::json ZMQOrderEventPublisher::trade_to_json(const connector::TradeUpdate& trade) const {
    nlohmann::json j;
    
    j["trade_id"] = trade.trade_id;
    j["client_order_id"] = trade.client_order_id;
    j["exchange_order_id"] = trade.exchange_order_id;
    j["trading_pair"] = trade.trading_pair;
    j["fill_price"] = trade.fill_price;
    j["fill_base_amount"] = trade.fill_base_amount;
    j["fill_quote_amount"] = trade.fill_quote_amount;
    j["fill_timestamp"] = trade.fill_timestamp;
    j["fee_amount"] = trade.fee_amount;
    j["fee_currency"] = trade.fee_currency;
    
    return j;
}

}  // namespace latentspeed
