/**
 * @file client_order_tracker.h
 * @brief Centralized tracking of all in-flight orders (Hummingbot pattern)
 * @author jessiondiwangan@gmail.com
 * @date 2025
 */

#pragma once

#include "connector/in_flight_order.h"
#include "connector/events.h"
#include <unordered_map>
#include <shared_mutex>
#include <functional>
#include <spdlog/spdlog.h>

namespace latentspeed::connector {

/**
 * @class ClientOrderTracker
 * @brief Centralized tracking of all in-flight orders (Hummingbot pattern)
 * 
 * This class maintains the state of all active orders and provides
 * thread-safe access to order information. It processes updates from
 * both REST API responses and WebSocket streams.
 */
class ClientOrderTracker {
public:
    // ========================================================================
    // ORDER LIFECYCLE MANAGEMENT
    // ========================================================================
    
    /**
     * @brief Start tracking an order (MUST be called BEFORE submission)
     */
    void start_tracking(InFlightOrder&& order) {
        std::unique_lock lock(mutex_);
        
        const std::string order_id = order.client_order_id;  // Copy ID before move
        tracked_orders_.emplace(order_id, std::move(order));
        
        spdlog::debug("[OrderTracker] Started tracking order: {}", order_id);
    }
    
    /**
     * @brief Stop tracking an order (called when order reaches terminal state)
     */
    void stop_tracking(const std::string& client_order_id) {
        std::unique_lock lock(mutex_);
        
        auto it = tracked_orders_.find(client_order_id);
        if (it != tracked_orders_.end()) {
            spdlog::debug("[OrderTracker] Stopped tracking order: {}", client_order_id);
            tracked_orders_.erase(it);
        }
    }

    // ========================================================================
    // ORDER ACCESS
    // ========================================================================
    
    /**
     * @brief Get order by client order ID (thread-safe)
     */
    std::optional<InFlightOrder> get_order(const std::string& client_order_id) const {
        std::shared_lock lock(mutex_);
        
        auto it = tracked_orders_.find(client_order_id);
        if (it != tracked_orders_.end()) {
            return std::optional<InFlightOrder>(it->second);
        }
        return std::nullopt;
    }
    
    /**
     * @brief Get order by exchange order ID
     */
    std::optional<InFlightOrder> get_order_by_exchange_id(
        const std::string& exchange_order_id
    ) const {
        std::shared_lock lock(mutex_);
        
        for (const auto& [_, order] : tracked_orders_) {
            if (order.exchange_order_id == exchange_order_id) {
                return std::optional<InFlightOrder>(order);
            }
        }
        return std::nullopt;
    }
    
    /**
     * @brief Get all fillable orders (OPEN or PARTIALLY_FILLED)
     */
    std::unordered_map<std::string, InFlightOrder> all_fillable_orders() const {
        std::shared_lock lock(mutex_);
        
        std::unordered_map<std::string, InFlightOrder> result;
        for (const auto& [id, order] : tracked_orders_) {
            if (order.is_fillable()) {
                result.emplace(id, order);
            }
        }
        return result;
    }
    
    /**
     * @brief Get fillable orders indexed by exchange order ID
     */
    std::unordered_map<std::string, InFlightOrder> all_fillable_orders_by_exchange_id() const {
        std::shared_lock lock(mutex_);
        
        std::unordered_map<std::string, InFlightOrder> result;
        for (const auto& [_, order] : tracked_orders_) {
            if (order.is_fillable() && order.exchange_order_id.has_value()) {
                result.emplace(*order.exchange_order_id, order);
            }
        }
        return result;
    }
    
    /**
     * @brief Get count of active orders
     */
    size_t active_order_count() const {
        std::shared_lock lock(mutex_);
        return tracked_orders_.size();
    }

    // ========================================================================
    // STATE UPDATE PROCESSING (HUMMINGBOT PATTERN)
    // ========================================================================
    
    /**
     * @brief Process order state update
     * @param update Order update from REST or WebSocket
     */
    void process_order_update(const OrderUpdate& update) {
        std::unique_lock lock(mutex_);
        
        auto it = tracked_orders_.find(update.client_order_id);
        if (it == tracked_orders_.end()) {
            spdlog::warn("[OrderTracker] Received update for unknown order: {}", 
                         update.client_order_id);
            return;
        }
        
        InFlightOrder& order = it->second;
        OrderState old_state = order.current_state;
        
        // Update state
        order.current_state = update.new_state;
        order.last_update_timestamp = update.update_timestamp;
        
        // Update exchange order ID if provided
        if (update.exchange_order_id.has_value()) {
            order.exchange_order_id = update.exchange_order_id;
        }
        
        spdlog::info("[OrderTracker] Order {} state: {} -> {}", 
                     update.client_order_id,
                     to_string(old_state),
                     to_string(update.new_state));
        
        // Emit event
        trigger_order_event(OrderEventType::ORDER_UPDATE, update.client_order_id);
        
        // Auto-stop tracking if done
        if (order.is_done() && auto_cleanup_) {
            spdlog::debug("[OrderTracker] Order {} completed, auto-removing from tracker", 
                          update.client_order_id);
            tracked_orders_.erase(it);
        }
    }
    
    /**
     * @brief Process trade/fill update
     * @param update Trade update from WebSocket
     */
    void process_trade_update(const TradeUpdate& update) {
        std::unique_lock lock(mutex_);
        
        auto it = tracked_orders_.find(update.client_order_id);
        if (it == tracked_orders_.end()) {
            spdlog::warn("[OrderTracker] Received trade update for unknown order: {}", 
                         update.client_order_id);
            return;
        }
        
        InFlightOrder& order = it->second;
        
        // Add trade to history
        order.trade_fills.push_back(update);
        order.filled_amount += update.fill_base_amount;
        order.last_update_timestamp = update.fill_timestamp;
        
        // Recalculate average fill price
        double total_quote = 0.0;
        double total_base = 0.0;
        for (const auto& fill : order.trade_fills) {
            total_quote += fill.fill_quote_amount;
            total_base += fill.fill_base_amount;
        }
        order.average_fill_price = (total_base > 1e-10) ? (total_quote / total_base) : 0.0;
        
        // Update state based on fill amount
        if (order.filled_amount >= order.amount - 1e-8) {
            order.current_state = OrderState::FILLED;
            spdlog::info("[OrderTracker] Order {} fully filled at avg price {}", 
                         update.client_order_id, order.average_fill_price);
        } else {
            order.current_state = OrderState::PARTIALLY_FILLED;
            spdlog::info("[OrderTracker] Order {} partially filled: {}/{}", 
                         update.client_order_id, order.filled_amount, order.amount);
        }
        
        // Emit events
        trigger_order_event(OrderEventType::ORDER_FILLED, update.client_order_id);
        
        if (order.is_done()) {
            trigger_order_event(OrderEventType::ORDER_COMPLETED, update.client_order_id);
            
            if (auto_cleanup_) {
                tracked_orders_.erase(it);
            }
        }
    }
    
    /**
     * @brief Process order not found (for DEX order tracking after cancel)
     */
    void process_order_not_found(const std::string& client_order_id) {
        not_found_count_[client_order_id]++;
        
        // After multiple "not found" responses, consider it cancelled
        if (not_found_count_[client_order_id] >= max_not_found_retries_) {
            OrderUpdate update{
                .client_order_id = client_order_id,
                .new_state = OrderState::CANCELLED,
                .update_timestamp = static_cast<uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::system_clock::now().time_since_epoch()
                    ).count()
                )
            };
            process_order_update(update);
            not_found_count_.erase(client_order_id);
        }
    }

    // ========================================================================
    // EVENT SYSTEM
    // ========================================================================
    
    /**
     * @brief Set event callback
     */
    void set_event_callback(std::function<void(OrderEventType, const std::string&)> callback) {
        event_callback_ = std::move(callback);
    }
    
    /**
     * @brief Enable/disable auto cleanup of completed orders
     */
    void set_auto_cleanup(bool enabled) {
        auto_cleanup_ = enabled;
    }

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, InFlightOrder> tracked_orders_;
    std::unordered_map<std::string, int> not_found_count_;
    
    std::function<void(OrderEventType, const std::string&)> event_callback_;
    bool auto_cleanup_ = true;
    int max_not_found_retries_ = 3;
    
    void trigger_order_event(OrderEventType type, const std::string& order_id) {
        if (event_callback_) {
            event_callback_(type, order_id);
        }
    }
};

} // namespace latentspeed::connector
