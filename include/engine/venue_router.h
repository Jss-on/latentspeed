/**
 * @file venue_router.h
 * @brief Minimal registry/router for exchange adapters (Phase 1).
 */

#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <string_view>

#include "adapters/exchange_adapter.h"

namespace latentspeed {

class VenueRouter {
public:
    void register_adapter(std::unique_ptr<IExchangeAdapter> adapter) {
        if (!adapter) return;
        auto key = adapter->get_exchange_name();
        adapters_.emplace(std::move(key), std::move(adapter));
    }

    IExchangeAdapter* get(std::string_view venue) const {
        auto it = adapters_.find(std::string(venue));
        return (it == adapters_.end()) ? nullptr : it->second.get();
    }

    bool empty() const { return adapters_.empty(); }

private:
    std::unordered_map<std::string, std::unique_ptr<IExchangeAdapter>> adapters_;
};

} // namespace latentspeed

