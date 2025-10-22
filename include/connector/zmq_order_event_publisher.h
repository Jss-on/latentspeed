#pragma once

#include "connector/in_flight_order.h"
#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <memory>
#include <string>
#include <spdlog/spdlog.h>

namespace latentspeed {

/**
 * @brief Publishes order events to ZMQ topic for consumption by other system components
 * 
 * This integrates the new connector architecture with your existing ZMQ messaging infrastructure.
 * Events are published as JSON messages on configurable topics.
 */
class ZMQOrderEventPublisher {
public:
    /**
     * @brief Construct ZMQ publisher
     * @param context Shared ZMQ context (reuse your existing context)
     * @param endpoint ZMQ endpoint (e.g., "tcp://*:5555" or "ipc:///tmp/orders.ipc")
     * @param topic_prefix Topic prefix for order events (e.g., "orders.hyperliquid")
     */
    ZMQOrderEventPublisher(
        std::shared_ptr<zmq::context_t> context,
        const std::string& endpoint,
        const std::string& topic_prefix = "orders"
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

    /**
     * @brief Publish order created event
     */
    void publish_order_created(const InFlightOrder& order) {
        nlohmann::json event;
        event["event_type"] = "order_created";
        event["timestamp"] = std::chrono::system_clock::now().time_since_epoch().count();
        event["data"] = order_to_json(order);
        
        publish_event("created", event);
    }

    /**
     * @brief Publish order filled event
     */
    void publish_order_filled(const InFlightOrder& order) {
        nlohmann::json event;
        event["event_type"] = "order_filled";
        event["timestamp"] = std::chrono::system_clock::now().time_since_epoch().count();
        event["data"] = order_to_json(order);
        
        publish_event("filled", event);
    }

    /**
     * @brief Publish order cancelled event
     */
    void publish_order_cancelled(const InFlightOrder& order) {
        nlohmann::json event;
        event["event_type"] = "order_cancelled";
        event["timestamp"] = std::chrono::system_clock::now().time_since_epoch().count();
        event["data"] = order_to_json(order);
        
        publish_event("cancelled", event);
    }

    /**
     * @brief Publish order failed event
     */
    void publish_order_failed(const InFlightOrder& order, const std::string& reason) {
        nlohmann::json event;
        event["event_type"] = "order_failed";
        event["timestamp"] = std::chrono::system_clock::now().time_since_epoch().count();
        event["data"] = order_to_json(order);
        event["failure_reason"] = reason;
        
        publish_event("failed", event);
    }

    /**
     * @brief Publish partial fill event
     */
    void publish_order_partially_filled(const InFlightOrder& order, const TradeUpdate& trade) {
        nlohmann::json event;
        event["event_type"] = "order_partially_filled";
        event["timestamp"] = std::chrono::system_clock::now().time_since_epoch().count();
        event["data"] = order_to_json(order);
        event["trade"] = trade_to_json(trade);
        
        publish_event("partial_fill", event);
    }

    /**
     * @brief Publish generic order update event
     */
    void publish_order_update(const InFlightOrder& order) {
        nlohmann::json event;
        event["event_type"] = "order_update";
        event["timestamp"] = std::chrono::system_clock::now().time_since_epoch().count();
        event["data"] = order_to_json(order);
        
        publish_event("update", event);
    }

    /**
     * @brief Get current endpoint
     */
    std::string get_endpoint() const {
        return endpoint_;
    }

    /**
     * @brief Get topic prefix
     */
    std::string get_topic_prefix() const {
        return topic_prefix_;
    }

private:
    /**
     * @brief Publish event to ZMQ with topic
     * @param subtopic Event subtopic (e.g., "created", "filled")
     * @param event JSON event data
     */
    void publish_event(const std::string& subtopic, const nlohmann::json& event) {
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

    /**
     * @brief Convert InFlightOrder to JSON
     */
    nlohmann::json order_to_json(const InFlightOrder& order) const {
        nlohmann::json j;
        
        j["client_order_id"] = order.client_order_id;
        j["exchange_order_id"] = order.exchange_order_id;
        j["trading_pair"] = order.trading_pair;
        j["order_type"] = order_type_to_string(order.order_type);
        j["trade_type"] = trade_type_to_string(order.trade_type);
        j["price"] = order.price;
        j["amount"] = order.amount;
        j["filled_amount"] = order.filled_amount;
        j["average_executed_price"] = order.average_executed_price;
        j["order_state"] = order_state_to_string(order.current_state);
        j["creation_timestamp"] = order.creation_timestamp;
        j["last_update_timestamp"] = order.last_update_timestamp;
        j["fee_paid"] = order.fee_paid;
        j["fee_asset"] = order.fee_asset;
        
        return j;
    }

    /**
     * @brief Convert TradeUpdate to JSON
     */
    nlohmann::json trade_to_json(const TradeUpdate& trade) const {
        nlohmann::json j;
        
        j["trade_id"] = trade.trade_id;
        j["client_order_id"] = trade.client_order_id;
        j["exchange_order_id"] = trade.exchange_order_id;
        j["trading_pair"] = trade.trading_pair;
        j["trade_type"] = trade_type_to_string(trade.trade_type);
        j["price"] = trade.price;
        j["amount"] = trade.amount;
        j["timestamp"] = trade.timestamp;
        j["fee"] = trade.fee;
        j["fee_asset"] = trade.fee_asset;
        
        return j;
    }

    std::shared_ptr<zmq::context_t> context_;
    std::string endpoint_;
    std::string topic_prefix_;
    zmq::socket_t publisher_;
};

}  // namespace latentspeed
