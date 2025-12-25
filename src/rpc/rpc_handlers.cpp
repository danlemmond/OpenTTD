/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file rpc_handlers.cpp JSON-RPC method handlers for AI agent integration. */

#include "../stdafx.h"
#include "rpc_server.h"
#include "../company_base.h"
#include "../company_func.h"
#include "../timer/timer_game_calendar.h"
#include "../timer/timer_game_economy.h"
#include "../vehicle_base.h"
#include "../roadveh.h"
#include "../strings_func.h"
#include "../string_func.h"
#include "../table/strings.h"

#include "../safeguards.h"

static const char *VehicleTypeToString(VehicleType type)
{
	switch (type) {
		case VEH_TRAIN: return "train";
		case VEH_ROAD: return "road";
		case VEH_SHIP: return "ship";
		case VEH_AIRCRAFT: return "aircraft";
		default: return "unknown";
	}
}

static const char *VehicleStateToString(const Vehicle *v)
{
	if (v->IsStoppedInDepot()) return "in_depot";
	if (v->vehstatus.Test(VehState::Crashed)) return "crashed";
	if (v->vehstatus.Test(VehState::Stopped)) return "stopped";
	if (v->breakdown_ctr != 0) return "broken";
	if (v->current_order.IsType(OT_LOADING)) return "loading";
	return "running";
}

static nlohmann::json HandleCompanyStatus([[maybe_unused]] const nlohmann::json &params)
{
	nlohmann::json result = nlohmann::json::array();

	for (const Company *c : Company::Iterate()) {
		nlohmann::json company_json;
		company_json["id"] = c->index.base();
		company_json["is_ai"] = c->is_ai;
		company_json["money"] = c->money.base();
		company_json["current_loan"] = c->current_loan.base();
		company_json["max_loan"] = c->GetMaxLoan().base();
		company_json["inaugurated_year"] = c->inaugurated_year.base();
		company_json["quarters_of_bankruptcy"] = c->months_of_bankruptcy / 3;

		company_json["colour"] = static_cast<int>(c->colour);

		company_json["infrastructure"] = {
			{"rail", c->infrastructure.GetRailTotal()},
			{"road", c->infrastructure.GetRoadTotal()},
			{"tram", c->infrastructure.GetTramTotal()},
			{"water", c->infrastructure.water},
			{"station", c->infrastructure.station},
			{"airport", c->infrastructure.airport},
			{"signal", c->infrastructure.signal}
		};

		company_json["current_economy"] = {
			{"income", c->cur_economy.income.base()},
			{"expenses", c->cur_economy.expenses.base()},
			{"company_value", c->cur_economy.company_value.base()},
			{"performance", c->cur_economy.performance_history}
		};

		result.push_back(company_json);
	}

	return result;
}

static nlohmann::json HandleGameStatus([[maybe_unused]] const nlohmann::json &params)
{
	nlohmann::json result;

	auto ymd = TimerGameCalendar::ConvertDateToYMD(TimerGameCalendar::date);

	result["calendar"] = {
		{"year", TimerGameCalendar::year.base()},
		{"month", TimerGameCalendar::month + 1},
		{"day", ymd.day}
	};

	result["economy"] = {
		{"year", TimerGameEconomy::year.base()}
	};

	return result;
}

static nlohmann::json HandlePing([[maybe_unused]] const nlohmann::json &params)
{
	return {{"pong", true}};
}

static nlohmann::json HandleVehicleList(const nlohmann::json &params)
{
	nlohmann::json result = nlohmann::json::array();

	VehicleType filter_type = VEH_INVALID;
	if (params.contains("type")) {
		std::string type_str = params["type"].get<std::string>();
		if (type_str == "road") filter_type = VEH_ROAD;
		else if (type_str == "train") filter_type = VEH_TRAIN;
		else if (type_str == "ship") filter_type = VEH_SHIP;
		else if (type_str == "aircraft") filter_type = VEH_AIRCRAFT;
	}

	CompanyID filter_company = CompanyID::Invalid();
	if (params.contains("company")) {
		filter_company = static_cast<CompanyID>(params["company"].get<int>());
	}

	for (const Vehicle *v : Vehicle::Iterate()) {
		if (!v->IsPrimaryVehicle()) continue;
		if (filter_type != VEH_INVALID && v->type != filter_type) continue;
		if (filter_company != CompanyID::Invalid() && v->owner != filter_company) continue;

		nlohmann::json vehicle_json;
		vehicle_json["id"] = v->index.base();
		vehicle_json["type"] = VehicleTypeToString(v->type);
		vehicle_json["owner"] = v->owner != CompanyID::Invalid() ? v->owner.base() : -1;
		vehicle_json["unit_number"] = v->unitnumber;
		vehicle_json["name"] = StrMakeValid(GetString(STR_VEHICLE_NAME, v->index));
		vehicle_json["state"] = VehicleStateToString(v);
		vehicle_json["location"] = {
			{"tile", v->tile != INVALID_TILE ? v->tile.base() : 0},
			{"x", v->x_pos},
			{"y", v->y_pos}
		};
		vehicle_json["speed"] = v->GetDisplaySpeed();
		vehicle_json["max_speed"] = v->GetDisplayMaxSpeed();
		vehicle_json["age_days"] = v->age.base();
		vehicle_json["profit_this_year"] = (v->profit_this_year >> 8).base();
		vehicle_json["profit_last_year"] = (v->profit_last_year >> 8).base();
		vehicle_json["value"] = v->value.base();
		vehicle_json["cargo_type"] = static_cast<int>(v->cargo_type);
		vehicle_json["cargo_capacity"] = v->cargo_cap;
		vehicle_json["cargo_count"] = v->cargo.StoredCount();

		result.push_back(vehicle_json);
	}

	return result;
}

static nlohmann::json HandleVehicleGet(const nlohmann::json &params)
{
	if (!params.contains("id")) {
		throw std::runtime_error("Missing required parameter: id");
	}

	VehicleID vid = static_cast<VehicleID>(params["id"].get<int>());
	const Vehicle *v = Vehicle::GetIfValid(vid);
	if (v == nullptr) {
		throw std::runtime_error("Invalid vehicle ID");
	}

	nlohmann::json result;
	result["id"] = v->index.base();
	result["type"] = VehicleTypeToString(v->type);
	result["owner"] = v->owner != CompanyID::Invalid() ? v->owner.base() : -1;
	result["name"] = StrMakeValid(GetString(STR_VEHICLE_NAME, v->index));
	result["state"] = VehicleStateToString(v);
	result["location"] = {
		{"tile", v->tile != INVALID_TILE ? v->tile.base() : 0},
		{"x", v->x_pos},
		{"y", v->y_pos}
	};
	result["speed"] = v->GetDisplaySpeed();
	result["max_speed"] = v->GetDisplayMaxSpeed();
	result["age_days"] = v->age.base();
	result["build_year"] = v->build_year.base();
	result["reliability"] = v->reliability * 100 / 0x10000;
	result["profit_this_year"] = (v->profit_this_year >> 8).base();
	result["profit_last_year"] = (v->profit_last_year >> 8).base();
	result["value"] = v->value.base();
	result["running_cost"] = v->GetRunningCost().base();
	result["cargo_type"] = static_cast<int>(v->cargo_type);
	result["cargo_capacity"] = v->cargo_cap;
	result["cargo_count"] = v->cargo.StoredCount();

	if (v->type == VEH_ROAD) {
		const RoadVehicle *rv = RoadVehicle::From(v);
		result["is_bus"] = rv->IsBus();
		result["roadtype"] = static_cast<int>(rv->roadtype);
	}

	return result;
}

void RpcRegisterHandlers(RpcServer &server)
{
	server.RegisterHandler("ping", HandlePing);
	server.RegisterHandler("game.status", HandleGameStatus);
	server.RegisterHandler("company.list", HandleCompanyStatus);
	server.RegisterHandler("vehicle.list", HandleVehicleList);
	server.RegisterHandler("vehicle.get", HandleVehicleGet);
}
