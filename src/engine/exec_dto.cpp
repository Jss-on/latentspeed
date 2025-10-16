/**
 * @file exec_dto.cpp
 */

#include "engine/exec_dto.h"
#include <rapidjson/document.h>
#include <string>

namespace latentspeed {

static inline void collect_object_primitives(const rapidjson::Value& obj, std::map<std::string,std::string>& out, const std::string& prefix = std::string()) {
    for (auto it = obj.MemberBegin(); it != obj.MemberEnd(); ++it) {
        std::string key = prefix.empty() ? it->name.GetString() : (prefix + it->name.GetString());
        if (it->value.IsString()) {
            out[key] = it->value.GetString();
        } else if (it->value.IsNumber()) {
            out[key] = std::to_string(it->value.GetDouble());
        } else if (it->value.IsBool()) {
            out[key] = it->value.GetBool() ? "true" : "false";
        }
    }
}

bool parse_exec_order_json(std::string_view json, ExecParsed& out) {
    rapidjson::Document doc;
    doc.Parse(json.data(), json.size());
    if (doc.HasParseError() || !doc.IsObject()) return false;

    if (doc.HasMember("version") && doc["version"].IsInt()) out.version = doc["version"].GetInt();
    if (doc.HasMember("cl_id") && doc["cl_id"].IsString()) out.cl_id = doc["cl_id"].GetString();
    if (doc.HasMember("action") && doc["action"].IsString()) out.action = doc["action"].GetString();
    if (doc.HasMember("venue_type") && doc["venue_type"].IsString()) out.venue_type = doc["venue_type"].GetString();
    if (doc.HasMember("venue") && doc["venue"].IsString()) out.venue = doc["venue"].GetString();
    if (doc.HasMember("product_type") && doc["product_type"].IsString()) out.product_type = doc["product_type"].GetString();
    if (doc.HasMember("ts_ns") && doc["ts_ns"].IsUint64()) out.ts_ns = doc["ts_ns"].GetUint64();

    if (doc.HasMember("details") && doc["details"].IsObject()) {
        const auto& d = doc["details"];
        if (d.HasMember("symbol") && d["symbol"].IsString()) out.details.symbol = d["symbol"].GetString();
        if (d.HasMember("side") && d["side"].IsString()) out.details.side = d["side"].GetString();
        if (d.HasMember("order_type") && d["order_type"].IsString()) out.details.order_type = d["order_type"].GetString();
        if (d.HasMember("time_in_force") && d["time_in_force"].IsString()) out.details.time_in_force = d["time_in_force"].GetString();
        if (d.HasMember("price")) {
            if (d["price"].IsNumber()) out.details.price = d["price"].GetDouble();
            else if (d["price"].IsString()) { try { out.details.price = std::stod(d["price"].GetString()); } catch (...) {} }
        }
        if (d.HasMember("size")) {
            if (d["size"].IsNumber()) out.details.size = d["size"].GetDouble();
            else if (d["size"].IsString()) { try { out.details.size = std::stod(d["size"].GetString()); } catch (...) {} }
        }
        if (d.HasMember("stop_price")) {
            if (d["stop_price"].IsNumber()) out.details.stop_price = d["stop_price"].GetDouble();
            else if (d["stop_price"].IsString()) { try { out.details.stop_price = std::stod(d["stop_price"].GetString()); } catch (...) {} }
        }
        if (d.HasMember("reduce_only")) {
            if (d["reduce_only"].IsBool()) out.details.reduce_only = d["reduce_only"].GetBool();
            else if (d["reduce_only"].IsString()) out.details.reduce_only = std::string(d["reduce_only"].GetString()) == "true";
        }
        if (d.HasMember("params") && d["params"].IsObject()) collect_object_primitives(d["params"], out.details.params);
        if (d.HasMember("cancel") && d["cancel"].IsObject()) collect_object_primitives(d["cancel"], out.details.cancel);
        if (d.HasMember("replace") && d["replace"].IsObject()) collect_object_primitives(d["replace"], out.details.replace);
    }

    if (doc.HasMember("tags") && doc["tags"].IsObject()) {
        collect_object_primitives(doc["tags"], out.tags);
    }
    return true;
}

} // namespace latentspeed

