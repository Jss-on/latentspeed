#pragma once

#include "connector/in_flight_order.h"
#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <memory>
#include <string>
#include <spdlog/spdlog.h>

namespace latentspeed::connector {

// Forward declarations
class InFlightOrder;
struct TradeUpdate;

}  // namespace latentspeed::connector

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
     * @param endpoint ZMQ endpoint (e.g., "tcp://0.0.0.0:5555" or "ipc:///tmp/orders.ipc")
     * @param topic_prefix Topic prefix for order events (e.g., "orders.hyperliquid")
     */
    ZMQOrderEventPublisher(
        std::shared_ptr<zmq::context_t> context,
        const std::string& endpoint,
        const std::string& topic_prefix = "orders"
    );

    /**
     * @brief Publish order created event
     */
    void publish_order_created(const connector::InFlightOrder& order);
    void publish_order_filled(const connector::InFlightOrder& order);
    void publish_order_cancelled(const connector::InFlightOrder& order);
    void publish_order_failed(const connector::InFlightOrder& order, const std::string& reason);
    void publish_order_partially_filled(const connector::InFlightOrder& order, const connector::TradeUpdate& trade);
    void publish_order_update(const connector::InFlightOrder& order);

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
    // Private method declarations - implementations in .cpp file
    void publish_event(const std::string& subtopic, const nlohmann::json& event);
    nlohmann::json order_to_json(const connector::InFlightOrder& order) const;
    nlohmann::json trade_to_json(const connector::TradeUpdate& trade) const;

    // Member variables
    std::shared_ptr<zmq::context_t> context_;
    std::string endpoint_;
    std::string topic_prefix_;
    zmq::socket_t publisher_;
};

}  // namespace latentspeed
