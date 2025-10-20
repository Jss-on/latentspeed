/**
 * @file connector_base.cpp
 * @brief Implementation of ConnectorBase utility methods
 * @author jessiondiwangan@gmail.com
 * @date 2025
 */

#include "connector/connector_base.h"
#include <sstream>
#include <iomanip>
#include <chrono>

namespace latentspeed::connector {

std::string ConnectorBase::generate_client_order_id() {
    // Format: PREFIX-TIMESTAMP-COUNTER
    // Example: LS-1729425600000-12345
    
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    ).count();
    
    uint64_t counter = order_id_counter_.fetch_add(1, std::memory_order_relaxed);
    
    std::ostringstream oss;
    oss << client_order_id_prefix_ << "-" << ms << "-" << counter;
    
    return oss.str();
}

double ConnectorBase::quantize_order_price(const std::string& trading_pair, double price) const {
    auto rule_opt = get_trading_rule(trading_pair);
    if (!rule_opt.has_value()) {
        // No rule found, return as-is
        return price;
    }
    
    return rule_opt->quantize_price(price);
}

double ConnectorBase::quantize_order_amount(const std::string& trading_pair, double amount) const {
    auto rule_opt = get_trading_rule(trading_pair);
    if (!rule_opt.has_value()) {
        // No rule found, return as-is
        return amount;
    }
    
    return rule_opt->quantize_size(amount);
}

// ============================================================================
// EVENT EMISSION HELPERS
// ============================================================================

void ConnectorBase::emit_order_created_event(
    const std::string& client_order_id,
    const std::string& exchange_order_id
) {
    if (order_event_listener_) {
        order_event_listener_->on_order_created(client_order_id, exchange_order_id);
    }
}

void ConnectorBase::emit_order_filled_event(
    const std::string& client_order_id,
    double fill_price,
    double fill_amount
) {
    if (order_event_listener_) {
        order_event_listener_->on_order_filled(client_order_id, fill_price, fill_amount);
    }
}

void ConnectorBase::emit_order_completed_event(
    const std::string& client_order_id,
    double avg_price,
    double total_filled
) {
    if (order_event_listener_) {
        order_event_listener_->on_order_completed(client_order_id, avg_price, total_filled);
    }
}

void ConnectorBase::emit_order_cancelled_event(
    const std::string& client_order_id
) {
    if (order_event_listener_) {
        order_event_listener_->on_order_cancelled(client_order_id);
    }
}

void ConnectorBase::emit_order_failed_event(
    const std::string& client_order_id,
    const std::string& reason
) {
    if (order_event_listener_) {
        order_event_listener_->on_order_failed(client_order_id, reason);
    }
}

void ConnectorBase::emit_error_event(
    const std::string& error_code,
    const std::string& error_message
) {
    if (error_event_listener_) {
        error_event_listener_->on_error(error_code, error_message);
    }
}

} // namespace latentspeed::connector
