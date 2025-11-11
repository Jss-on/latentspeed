#include "engine/order_serializer.h"
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>

namespace latentspeed {
namespace engine {

std::string serialize_execution_report(const hft::HFTExecutionReport& report) {
    rapidjson::Document doc;
    doc.SetObject();
    auto& allocator = doc.GetAllocator();
    
    doc.AddMember("version", rapidjson::Value(report.version), allocator);
    doc.AddMember("cl_id", rapidjson::Value(report.cl_id.c_str(), allocator), allocator);
    doc.AddMember("status", rapidjson::Value(report.status.c_str(), allocator), allocator);
    
    if (!report.exchange_order_id.empty()) {
        doc.AddMember("exchange_order_id", rapidjson::Value(report.exchange_order_id.c_str(), allocator), allocator);
    }
    
    doc.AddMember("reason_code", rapidjson::Value(report.reason_code.c_str(), allocator), allocator);
    doc.AddMember("reason_text", rapidjson::Value(report.reason_text.c_str(), allocator), allocator);
    doc.AddMember("ts_ns", rapidjson::Value(report.ts_ns.load()), allocator);
    
    // Serialize tags
    rapidjson::Value tags_obj(rapidjson::kObjectType);
    report.tags.for_each([&](const auto& key, const auto& value) {
        rapidjson::Value json_key(key.c_str(), allocator);
        rapidjson::Value json_val(value.c_str(), allocator);
        tags_obj.AddMember(json_key, json_val, allocator);
    });
    doc.AddMember("tags", tags_obj, allocator);
    
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);
    
    return buffer.GetString();
}

std::string serialize_fill(const hft::HFTFill& fill) {
    rapidjson::Document doc;
    doc.SetObject();
    auto& allocator = doc.GetAllocator();
    
    doc.AddMember("version", rapidjson::Value(fill.version), allocator);
    doc.AddMember("cl_id", rapidjson::Value(fill.cl_id.c_str(), allocator), allocator);
    doc.AddMember("exchange_order_id", rapidjson::Value(fill.exchange_order_id.c_str(), allocator), allocator);
    doc.AddMember("exec_id", rapidjson::Value(fill.exec_id.c_str(), allocator), allocator);
    doc.AddMember("symbol_or_pair", rapidjson::Value(fill.symbol_or_pair.c_str(), allocator), allocator);
    doc.AddMember("price", rapidjson::Value(fill.price), allocator);
    doc.AddMember("size", rapidjson::Value(fill.size), allocator);
    doc.AddMember("fee_currency", rapidjson::Value(fill.fee_currency.c_str(), allocator), allocator);
    doc.AddMember("fee_amount", rapidjson::Value(fill.fee_amount), allocator);
    doc.AddMember("liquidity", rapidjson::Value(fill.liquidity.c_str(), allocator), allocator);
    doc.AddMember("ts_ns", rapidjson::Value(fill.ts_ns.load()), allocator);
    
    // Serialize tags
    rapidjson::Value tags_obj(rapidjson::kObjectType);
    fill.tags.for_each([&](const auto& key, const auto& value) {
        rapidjson::Value json_key(key.c_str(), allocator);
        rapidjson::Value json_val(value.c_str(), allocator);
        tags_obj.AddMember(json_key, json_val, allocator);
    });
    doc.AddMember("tags", tags_obj, allocator);
    
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);
    
    return buffer.GetString();
}

} // namespace engine
} // namespace latentspeed
