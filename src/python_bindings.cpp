/**
 * @file python_bindings.cpp
 * @brief Python bindings for TradingEngineService
 * @author jessiondiwangan@gmail.com
 * @date 2025
 */

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/chrono.h>
#include "trading_engine_service.h"

namespace py = pybind11;

PYBIND11_MODULE(latentspeed, m) {
    m.doc() = "Latentspeed Trading Engine Python Bindings";

    // ExecutionOrder struct
    py::class_<latentspeed::ExecutionOrder>(m, "ExecutionOrder")
        .def(py::init<>())
        .def_readwrite("version", &latentspeed::ExecutionOrder::version)
        .def_readwrite("cl_id", &latentspeed::ExecutionOrder::cl_id)
        .def_readwrite("action", &latentspeed::ExecutionOrder::action)
        .def_readwrite("venue_type", &latentspeed::ExecutionOrder::venue_type)
        .def_readwrite("venue", &latentspeed::ExecutionOrder::venue)
        .def_readwrite("product_type", &latentspeed::ExecutionOrder::product_type)
        .def_readwrite("details", &latentspeed::ExecutionOrder::details)
        .def_readwrite("ts_ns", &latentspeed::ExecutionOrder::ts_ns)
        .def_readwrite("tags", &latentspeed::ExecutionOrder::tags);

    // ExecutionReport struct
    py::class_<latentspeed::ExecutionReport>(m, "ExecutionReport")
        .def(py::init<>())
        .def_readwrite("version", &latentspeed::ExecutionReport::version)
        .def_readwrite("cl_id", &latentspeed::ExecutionReport::cl_id)
        .def_readwrite("status", &latentspeed::ExecutionReport::status)
        .def_readwrite("exchange_order_id", &latentspeed::ExecutionReport::exchange_order_id)
        .def_readwrite("reason_code", &latentspeed::ExecutionReport::reason_code)
        .def_readwrite("reason_text", &latentspeed::ExecutionReport::reason_text)
        .def_readwrite("ts_ns", &latentspeed::ExecutionReport::ts_ns)
        .def_readwrite("tags", &latentspeed::ExecutionReport::tags);

    // Fill struct
    py::class_<latentspeed::Fill>(m, "Fill")
        .def(py::init<>())
        .def_readwrite("version", &latentspeed::Fill::version)
        .def_readwrite("cl_id", &latentspeed::Fill::cl_id)
        .def_readwrite("exchange_order_id", &latentspeed::Fill::exchange_order_id)
        .def_readwrite("exec_id", &latentspeed::Fill::exec_id)
        .def_readwrite("symbol_or_pair", &latentspeed::Fill::symbol_or_pair)
        .def_readwrite("price", &latentspeed::Fill::price)
        .def_readwrite("size", &latentspeed::Fill::size)
        .def_readwrite("fee_currency", &latentspeed::Fill::fee_currency)
        .def_readwrite("fee_amount", &latentspeed::Fill::fee_amount)
        .def_readwrite("liquidity", &latentspeed::Fill::liquidity)
        .def_readwrite("ts_ns", &latentspeed::Fill::ts_ns)
        .def_readwrite("tags", &latentspeed::Fill::tags);

    // TradeData struct
    py::class_<latentspeed::TradeData>(m, "TradeData")
        .def(py::init<>())
        .def_readwrite("exchange", &latentspeed::TradeData::exchange)
        .def_readwrite("symbol", &latentspeed::TradeData::symbol)
        .def_readwrite("side", &latentspeed::TradeData::side)
        .def_readwrite("price", &latentspeed::TradeData::price)
        .def_readwrite("amount", &latentspeed::TradeData::amount)
        .def_readwrite("timestamp_ns", &latentspeed::TradeData::timestamp_ns)
        .def_readwrite("trade_id", &latentspeed::TradeData::trade_id)
        .def_readwrite("transaction_price", &latentspeed::TradeData::transaction_price)
        .def_readwrite("trading_volume", &latentspeed::TradeData::trading_volume)
        .def_readwrite("volatility_transaction_price", &latentspeed::TradeData::volatility_transaction_price)
        .def_readwrite("window_size", &latentspeed::TradeData::window_size)
        .def_readwrite("transaction_price_window_size", &latentspeed::TradeData::transaction_price_window_size)
        .def_readwrite("seq", &latentspeed::TradeData::seq)
        .def_readwrite("sequence_number", &latentspeed::TradeData::sequence_number)
        .def_readwrite("schema_version", &latentspeed::TradeData::schema_version)
        .def_readwrite("preprocessing_timestamp", &latentspeed::TradeData::preprocessing_timestamp)
        .def_readwrite("receipt_timestamp_ns", &latentspeed::TradeData::receipt_timestamp_ns);

    // OrderBookData struct
    py::class_<latentspeed::OrderBookData>(m, "OrderBookData")
        .def(py::init<>())
        .def_readwrite("exchange", &latentspeed::OrderBookData::exchange)
        .def_readwrite("symbol", &latentspeed::OrderBookData::symbol)
        .def_readwrite("timestamp_ns", &latentspeed::OrderBookData::timestamp_ns)
        .def_readwrite("best_bid_price", &latentspeed::OrderBookData::best_bid_price)
        .def_readwrite("best_bid_size", &latentspeed::OrderBookData::best_bid_size)
        .def_readwrite("best_ask_price", &latentspeed::OrderBookData::best_ask_price)
        .def_readwrite("best_ask_size", &latentspeed::OrderBookData::best_ask_size)
        .def_readwrite("midpoint", &latentspeed::OrderBookData::midpoint)
        .def_readwrite("relative_spread", &latentspeed::OrderBookData::relative_spread)
        .def_readwrite("breadth", &latentspeed::OrderBookData::breadth)
        .def_readwrite("imbalance_lvl1", &latentspeed::OrderBookData::imbalance_lvl1)
        .def_readwrite("bid_depth_n", &latentspeed::OrderBookData::bid_depth_n)
        .def_readwrite("ask_depth_n", &latentspeed::OrderBookData::ask_depth_n)
        .def_readwrite("depth_n", &latentspeed::OrderBookData::depth_n)
        .def_readwrite("volatility_mid", &latentspeed::OrderBookData::volatility_mid)
        .def_readwrite("ofi_rolling", &latentspeed::OrderBookData::ofi_rolling)
        .def_readwrite("bids", &latentspeed::OrderBookData::bids)
        .def_readwrite("asks", &latentspeed::OrderBookData::asks)
        .def_readwrite("seq", &latentspeed::OrderBookData::seq)
        .def_readwrite("sequence_number", &latentspeed::OrderBookData::sequence_number)
        .def_readwrite("schema_version", &latentspeed::OrderBookData::schema_version)
        .def_readwrite("preprocessing_timestamp", &latentspeed::OrderBookData::preprocessing_timestamp)
        .def_readwrite("receipt_timestamp_ns", &latentspeed::OrderBookData::receipt_timestamp_ns)
        .def_readwrite("window_size", &latentspeed::OrderBookData::window_size)
        .def_readwrite("midpoint_window_size", &latentspeed::OrderBookData::midpoint_window_size);

    // FastRollingStats class
    py::class_<latentspeed::FastRollingStats>(m, "FastRollingStats")
        .def(py::init<size_t>(), py::arg("window_size") = 20);

    // TradeRollResult struct
    py::class_<latentspeed::FastRollingStats::TradeRollResult>(m, "TradeRollResult")
        .def(py::init<>())
        .def_readwrite("volatility_transaction_price", &latentspeed::FastRollingStats::TradeRollResult::volatility_transaction_price)
        .def_readwrite("transaction_price_window_size", &latentspeed::FastRollingStats::TradeRollResult::transaction_price_window_size);

    // BookRollResult struct  
    py::class_<latentspeed::FastRollingStats::BookRollResult>(m, "BookRollResult")
        .def(py::init<>())
        .def_readwrite("volatility_mid", &latentspeed::FastRollingStats::BookRollResult::volatility_mid)
        .def_readwrite("ofi_rolling", &latentspeed::FastRollingStats::BookRollResult::ofi_rolling)
        .def_readwrite("midpoint_window_size", &latentspeed::FastRollingStats::BookRollResult::midpoint_window_size);

    // Main TradingEngineService class
    py::class_<latentspeed::TradingEngineService>(m, "TradingEngineService")
        .def(py::init<>())
        .def("initialize", &latentspeed::TradingEngineService::initialize,
             "Initialize the trading engine")
        .def("start", &latentspeed::TradingEngineService::start,
             "Start the trading engine service")
        .def("stop", &latentspeed::TradingEngineService::stop,
             "Stop the trading engine service")
        .def("is_running", &latentspeed::TradingEngineService::is_running,
             "Check if the service is running");

    // Version information
    m.attr("__version__") = "0.1.0";
}
