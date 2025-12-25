/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file rpc_handlers_query.cpp JSON-RPC query handlers for reading game state. */

#include "../stdafx.h"
#include "rpc_handlers.h"
#include "../company_base.h"
#include "../company_func.h"
#include "../timer/timer_game_calendar.h"
#include "../timer/timer_game_economy.h"
#include "../vehicle_base.h"
#include "../vehicle_func.h"
#include "../roadveh.h"
#include "../train.h"
#include "../station_base.h"
#include "../industry.h"
#include "../cargotype.h"
#include "../town.h"
#include "../map_func.h"
#include "../tile_map.h"
#include "../landscape.h"
#include "../settings_type.h"
#include "../strings_func.h"
#include "../string_func.h"
#include "../table/strings.h"
#include "../order_base.h"
#include "../engine_base.h"
#include "../engine_func.h"
#include "../articulated_vehicles.h"
#include "../subsidy_base.h"
#include "../economy_func.h"
#include "../cargomonitor.h"
#include "../airport.h"
#include "../newgrf_airport.h"
#include "../station_map.h"
#include "../linkgraph/linkgraph_base.h"
#include "../news_func.h"
#include "../news_type.h"
#include "../road_map.h"
#include "../rail_map.h"
#include "../water_map.h"
#include "../depot_map.h"

#include "../safeguards.h"

/* Forward declaration for news access */
const NewsContainer &GetNews();

static const char *VehicleStateToString(const Vehicle *v)
{
	/* Use First() since IsStoppedInDepot() requires primary vehicle */
	if (v->First()->IsStoppedInDepot()) return "in_depot";
	if (v->vehstatus.Test(VehState::Crashed)) return "crashed";
	if (v->vehstatus.Test(VehState::Stopped)) return "stopped";
	if (v->breakdown_ctr != 0) return "broken";
	if (v->current_order.IsType(OT_LOADING)) return "loading";
	return "running";
}

static const char *LandscapeToString(LandscapeType landscape)
{
	switch (landscape) {
		case LandscapeType::Temperate: return "temperate";
		case LandscapeType::Arctic: return "arctic";
		case LandscapeType::Tropic: return "tropic";
		case LandscapeType::Toyland: return "toyland";
		default: return "unknown";
	}
}

static const char *OrderTypeToString(OrderType type)
{
	switch (type) {
		case OT_NOTHING: return "nothing";
		case OT_GOTO_STATION: return "goto_station";
		case OT_GOTO_DEPOT: return "goto_depot";
		case OT_LOADING: return "loading";
		case OT_LEAVESTATION: return "leave_station";
		case OT_DUMMY: return "dummy";
		case OT_GOTO_WAYPOINT: return "goto_waypoint";
		case OT_CONDITIONAL: return "conditional";
		case OT_IMPLICIT: return "implicit";
		default: return "unknown";
	}
}

static const char *LoadTypeToString(OrderLoadType type)
{
	switch (type) {
		case OrderLoadType::LoadIfPossible: return "load_if_possible";
		case OrderLoadType::FullLoad: return "full_load";
		case OrderLoadType::FullLoadAny: return "full_load_any";
		case OrderLoadType::NoLoad: return "no_load";
		default: return "unknown";
	}
}

static const char *UnloadTypeToString(OrderUnloadType type)
{
	switch (type) {
		case OrderUnloadType::UnloadIfPossible: return "unload_if_possible";
		case OrderUnloadType::Unload: return "unload";
		case OrderUnloadType::Transfer: return "transfer";
		case OrderUnloadType::NoUnload: return "no_unload";
		default: return "unknown";
	}
}

static nlohmann::json OrderToJson(const Order &order, VehicleOrderID index)
{
	nlohmann::json order_json;
	order_json["index"] = index;
	order_json["type"] = OrderTypeToString(order.GetType());

	if (order.IsGotoOrder()) {
		order_json["destination"] = order.GetDestination().base();

		/* Get destination name based on order type */
		if (order.IsType(OT_GOTO_STATION) || order.IsType(OT_GOTO_WAYPOINT)) {
			const Station *st = Station::GetIfValid(order.GetDestination().ToStationID());
			if (st != nullptr) {
				order_json["destination_name"] = StrMakeValid(st->GetCachedName());
			}
		} else if (order.IsType(OT_GOTO_DEPOT)) {
			order_json["destination_name"] = "Depot";
		}
	}

	if (order.IsType(OT_GOTO_STATION)) {
		order_json["load_type"] = LoadTypeToString(order.GetLoadType());
		order_json["unload_type"] = UnloadTypeToString(order.GetUnloadType());

		bool non_stop = order.GetNonStopType().Test(OrderNonStopFlag::NoIntermediate);
		bool via = order.GetNonStopType().Test(OrderNonStopFlag::NoDestination);
		order_json["non_stop"] = non_stop;
		order_json["via"] = via;
	}

	if (order.IsType(OT_CONDITIONAL)) {
		order_json["skip_to"] = order.GetConditionSkipToOrder();
		order_json["condition_value"] = order.GetConditionValue();
	}

	if (order.GetWaitTime() > 0) {
		order_json["wait_time"] = order.GetWaitTime();
		order_json["wait_timetabled"] = order.IsWaitTimetabled();
	}

	if (order.GetTravelTime() > 0) {
		order_json["travel_time"] = order.GetTravelTime();
		order_json["travel_timetabled"] = order.IsTravelTimetabled();
	}

	if (order.GetMaxSpeed() != UINT16_MAX) {
		order_json["max_speed"] = order.GetMaxSpeed();
	}

	return order_json;
}

static nlohmann::json HandlePing([[maybe_unused]] const nlohmann::json &params)
{
	return {{"pong", true}};
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

static nlohmann::json HandleCompanyList([[maybe_unused]] const nlohmann::json &params)
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
		vehicle_json["type"] = RpcVehicleTypeToString(v->type);
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
	result["type"] = RpcVehicleTypeToString(v->type);
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

	/* For trains, include wagon composition */
	if (v->type == VEH_TRAIN) {
		nlohmann::json composition = nlohmann::json::array();
		uint total_capacity = 0;
		uint total_cargo = 0;

		for (const Vehicle *u = v->First(); u != nullptr; u = u->Next()) {
			nlohmann::json wagon;
			wagon["id"] = u->index.base();
			wagon["engine_id"] = Engine::IsValidID(u->engine_type) ? u->engine_type.base() : -1;

			/* Get engine name */
			if (Engine::IsValidID(u->engine_type)) {
				wagon["engine_name"] = StrMakeValid(GetString(STR_ENGINE_NAME, u->engine_type));
			}

			/* Determine wagon type */
			const Train *t = Train::From(u);
			if (t->IsEngine()) {
				wagon["wagon_type"] = "engine";
			} else if (t->IsMultiheaded()) {
				wagon["wagon_type"] = "rear_engine";
			} else {
				wagon["wagon_type"] = "wagon";
			}

			/* Cargo info */
			if (u->cargo_cap > 0) {
				const CargoSpec *cs = CargoSpec::Get(u->cargo_type);
				wagon["cargo_type"] = cs != nullptr ? StrMakeValid(GetString(cs->name)) : "none";
				wagon["cargo_capacity"] = u->cargo_cap;
				wagon["cargo_count"] = u->cargo.StoredCount();
				total_capacity += u->cargo_cap;
				total_cargo += u->cargo.StoredCount();
			} else {
				wagon["cargo_type"] = "none";
				wagon["cargo_capacity"] = 0;
				wagon["cargo_count"] = 0;
			}

			composition.push_back(wagon);
		}

		result["composition"] = composition;
		result["total_capacity"] = total_capacity;
		result["total_cargo"] = total_cargo;
		result["wagon_count"] = composition.size();
	}

	return result;
}

static nlohmann::json HandleStationList(const nlohmann::json &params)
{
	nlohmann::json result = nlohmann::json::array();

	CompanyID filter_company = CompanyID::Invalid();
	if (params.contains("company")) {
		filter_company = static_cast<CompanyID>(params["company"].get<int>());
	}

	for (const Station *st : Station::Iterate()) {
		if (filter_company != CompanyID::Invalid() && st->owner != filter_company) continue;

		nlohmann::json station_json;
		station_json["id"] = st->index.base();
		station_json["name"] = StrMakeValid(st->GetCachedName());
		station_json["owner"] = st->owner != CompanyID::Invalid() ? st->owner.base() : -1;
		station_json["location"] = {
			{"tile", st->xy != INVALID_TILE ? st->xy.base() : 0},
			{"x", TileX(st->xy)},
			{"y", TileY(st->xy)}
		};

		nlohmann::json facilities = nlohmann::json::array();
		if (st->facilities.Test(StationFacility::Train)) facilities.push_back("train");
		if (st->facilities.Test(StationFacility::TruckStop)) facilities.push_back("truck");
		if (st->facilities.Test(StationFacility::BusStop)) facilities.push_back("bus");
		if (st->facilities.Test(StationFacility::Airport)) facilities.push_back("airport");
		if (st->facilities.Test(StationFacility::Dock)) facilities.push_back("dock");
		station_json["facilities"] = facilities;

		int total_waiting = 0;
		for (CargoType c = 0; c < NUM_CARGO; c++) {
			total_waiting += st->goods[c].TotalCount();
		}
		station_json["cargo_waiting_total"] = total_waiting;

		result.push_back(station_json);
	}

	return result;
}

static nlohmann::json HandleStationGet(const nlohmann::json &params)
{
	if (!params.contains("id")) {
		throw std::runtime_error("Missing required parameter: id");
	}

	StationID sid = static_cast<StationID>(params["id"].get<int>());
	const Station *st = Station::GetIfValid(sid);
	if (st == nullptr) {
		throw std::runtime_error("Invalid station ID");
	}

	nlohmann::json result;
	result["id"] = st->index.base();
	result["name"] = StrMakeValid(st->GetCachedName());
	result["owner"] = st->owner != CompanyID::Invalid() ? st->owner.base() : -1;
	result["location"] = {
		{"tile", st->xy != INVALID_TILE ? st->xy.base() : 0},
		{"x", TileX(st->xy)},
		{"y", TileY(st->xy)}
	};

	nlohmann::json facilities = nlohmann::json::array();
	if (st->facilities.Test(StationFacility::Train)) facilities.push_back("train");
	if (st->facilities.Test(StationFacility::TruckStop)) facilities.push_back("truck");
	if (st->facilities.Test(StationFacility::BusStop)) facilities.push_back("bus");
	if (st->facilities.Test(StationFacility::Airport)) facilities.push_back("airport");
	if (st->facilities.Test(StationFacility::Dock)) facilities.push_back("dock");
	result["facilities"] = facilities;

	nlohmann::json cargo_list = nlohmann::json::array();
	for (CargoType c = 0; c < NUM_CARGO; c++) {
		const GoodsEntry &ge = st->goods[c];
		if (!ge.HasRating() && ge.TotalCount() == 0) continue;

		const CargoSpec *cs = CargoSpec::Get(c);
		if (!cs->IsValid()) continue;

		nlohmann::json cargo_json;
		cargo_json["cargo_id"] = c;
		cargo_json["cargo_name"] = StrMakeValid(GetString(cs->name));
		cargo_json["waiting"] = ge.TotalCount();
		cargo_json["rating"] = ge.HasRating() ? ge.rating * 100 / 255 : -1;
		cargo_list.push_back(cargo_json);
	}
	result["cargo"] = cargo_list;

	return result;
}

static nlohmann::json HandleIndustryList(const nlohmann::json &params)
{
	nlohmann::json result = nlohmann::json::array();

	int filter_type = -1;
	if (params.contains("type")) {
		filter_type = params["type"].get<int>();
	}

	for (const Industry *ind : Industry::Iterate()) {
		if (filter_type >= 0 && ind->type != filter_type) continue;

		nlohmann::json industry_json;
		industry_json["id"] = ind->index.base();
		industry_json["type"] = ind->type;
		industry_json["name"] = StrMakeValid(GetString(STR_INDUSTRY_NAME, ind->index));
		industry_json["location"] = {
			{"tile", ind->location.tile != INVALID_TILE ? ind->location.tile.base() : 0},
			{"x", TileX(ind->location.tile)},
			{"y", TileY(ind->location.tile)}
		};
		if (ind->town != nullptr) {
			industry_json["town"] = StrMakeValid(GetString(STR_TOWN_NAME, ind->town->index));
		}
		industry_json["production_level"] = ind->prod_level;

		nlohmann::json produces = nlohmann::json::array();
		for (const auto &p : ind->produced) {
			if (!IsValidCargoType(p.cargo)) continue;
			const CargoSpec *cs = CargoSpec::Get(p.cargo);
			if (!cs->IsValid()) continue;

			nlohmann::json cargo_json;
			cargo_json["cargo_id"] = p.cargo;
			cargo_json["cargo_name"] = StrMakeValid(GetString(cs->name));
			cargo_json["waiting"] = p.waiting;
			cargo_json["rate"] = p.rate;
			if (!p.history.empty()) {
				cargo_json["last_month_production"] = p.history[0].production;
				cargo_json["last_month_transported"] = p.history[0].transported;
			}
			produces.push_back(cargo_json);
		}
		industry_json["produces"] = produces;

		nlohmann::json accepts = nlohmann::json::array();
		for (const auto &a : ind->accepted) {
			if (!IsValidCargoType(a.cargo)) continue;
			const CargoSpec *cs = CargoSpec::Get(a.cargo);
			if (!cs->IsValid()) continue;

			nlohmann::json cargo_json;
			cargo_json["cargo_id"] = a.cargo;
			cargo_json["cargo_name"] = StrMakeValid(GetString(cs->name));
			cargo_json["waiting"] = a.waiting;
			accepts.push_back(cargo_json);
		}
		industry_json["accepts"] = accepts;

		result.push_back(industry_json);
	}

	return result;
}

static nlohmann::json HandleIndustryGet(const nlohmann::json &params)
{
	if (!params.contains("id")) {
		throw std::runtime_error("Missing required parameter: id");
	}

	IndustryID iid = static_cast<IndustryID>(params["id"].get<int>());
	const Industry *ind = Industry::GetIfValid(iid);
	if (ind == nullptr) {
		throw std::runtime_error("Invalid industry ID");
	}

	nlohmann::json result;
	result["id"] = ind->index.base();
	result["type"] = ind->type;
	result["name"] = StrMakeValid(GetString(STR_INDUSTRY_NAME, ind->index));
	result["location"] = {
		{"tile", ind->location.tile != INVALID_TILE ? ind->location.tile.base() : 0},
		{"x", TileX(ind->location.tile)},
		{"y", TileY(ind->location.tile)},
		{"width", ind->location.w},
		{"height", ind->location.h}
	};
	if (ind->town != nullptr) {
		result["town"] = StrMakeValid(GetString(STR_TOWN_NAME, ind->town->index));
	}
	result["production_level"] = ind->prod_level;
	result["last_production_year"] = ind->last_prod_year.base();
	result["stations_nearby"] = static_cast<int>(ind->stations_near.size());

	nlohmann::json produces = nlohmann::json::array();
	for (const auto &p : ind->produced) {
		if (!IsValidCargoType(p.cargo)) continue;
		const CargoSpec *cs = CargoSpec::Get(p.cargo);
		if (!cs->IsValid()) continue;

		nlohmann::json cargo_json;
		cargo_json["cargo_id"] = p.cargo;
		cargo_json["cargo_name"] = StrMakeValid(GetString(cs->name));
		cargo_json["waiting"] = p.waiting;
		cargo_json["rate"] = p.rate;
		if (!p.history.empty()) {
			cargo_json["last_month_production"] = p.history[0].production;
			cargo_json["last_month_transported"] = p.history[0].transported;
			cargo_json["transported_pct"] = p.history[0].PctTransported();
		}
		produces.push_back(cargo_json);
	}
	result["produces"] = produces;

	nlohmann::json accepts = nlohmann::json::array();
	for (const auto &a : ind->accepted) {
		if (!IsValidCargoType(a.cargo)) continue;
		const CargoSpec *cs = CargoSpec::Get(a.cargo);
		if (!cs->IsValid()) continue;

		nlohmann::json cargo_json;
		cargo_json["cargo_id"] = a.cargo;
		cargo_json["cargo_name"] = StrMakeValid(GetString(cs->name));
		cargo_json["waiting"] = a.waiting;
		accepts.push_back(cargo_json);
	}
	result["accepts"] = accepts;

	return result;
}

/**
 * Handler for industry.nearest - Find nearest industry matching criteria.
 *
 * Parameters:
 *   x, y: Reference coordinates (required)
 *   produces: Cargo type ID or name that industry must produce (optional)
 *   accepts: Cargo type ID or name that industry must accept (optional)
 *
 * Returns the nearest matching industry with distance.
 */
static nlohmann::json HandleIndustryNearest(const nlohmann::json &params)
{
	if (!params.contains("x") || !params.contains("y")) {
		throw std::runtime_error("Missing required parameters: x, y");
	}

	uint ref_x = params["x"].get<uint>();
	uint ref_y = params["y"].get<uint>();
	if (ref_x >= Map::SizeX() || ref_y >= Map::SizeY()) {
		throw std::runtime_error("Coordinates out of bounds");
	}
	TileIndex ref_tile = TileXY(ref_x, ref_y);

	/* Parse cargo filter - can be ID (int) or name (string) */
	CargoType filter_produces = INVALID_CARGO;
	CargoType filter_accepts = INVALID_CARGO;

	if (params.contains("produces")) {
		if (params["produces"].is_number()) {
			filter_produces = static_cast<CargoType>(params["produces"].get<int>());
		} else if (params["produces"].is_string()) {
			std::string cargo_name = params["produces"].get<std::string>();
			for (const CargoSpec *cs : CargoSpec::Iterate()) {
				if (cs->IsValid()) {
					std::string name = StrMakeValid(GetString(cs->name));
					/* Case-insensitive comparison */
					std::transform(name.begin(), name.end(), name.begin(), ::tolower);
					std::transform(cargo_name.begin(), cargo_name.end(), cargo_name.begin(), ::tolower);
					if (name == cargo_name) {
						filter_produces = cs->Index();
						break;
					}
				}
			}
			if (!IsValidCargoType(filter_produces)) {
				throw std::runtime_error("Unknown cargo type: " + params["produces"].get<std::string>());
			}
		}
	}

	if (params.contains("accepts")) {
		if (params["accepts"].is_number()) {
			filter_accepts = static_cast<CargoType>(params["accepts"].get<int>());
		} else if (params["accepts"].is_string()) {
			std::string cargo_name = params["accepts"].get<std::string>();
			for (const CargoSpec *cs : CargoSpec::Iterate()) {
				if (cs->IsValid()) {
					std::string name = StrMakeValid(GetString(cs->name));
					std::transform(name.begin(), name.end(), name.begin(), ::tolower);
					std::transform(cargo_name.begin(), cargo_name.end(), cargo_name.begin(), ::tolower);
					if (name == cargo_name) {
						filter_accepts = cs->Index();
						break;
					}
				}
			}
			if (!IsValidCargoType(filter_accepts)) {
				throw std::runtime_error("Unknown cargo type: " + params["accepts"].get<std::string>());
			}
		}
	}

	const Industry *nearest = nullptr;
	uint min_distance = UINT_MAX;

	for (const Industry *ind : Industry::Iterate()) {
		/* Check cargo filter */
		if (IsValidCargoType(filter_produces)) {
			bool found = false;
			for (const auto &p : ind->produced) {
				if (p.cargo == filter_produces) {
					found = true;
					break;
				}
			}
			if (!found) continue;
		}

		if (IsValidCargoType(filter_accepts)) {
			bool found = false;
			for (const auto &a : ind->accepted) {
				if (a.cargo == filter_accepts) {
					found = true;
					break;
				}
			}
			if (!found) continue;
		}

		uint distance = DistanceManhattan(ref_tile, ind->location.tile);
		if (distance < min_distance) {
			min_distance = distance;
			nearest = ind;
		}
	}

	if (nearest == nullptr) {
		throw std::runtime_error("No matching industry found");
	}

	nlohmann::json result;
	result["id"] = nearest->index.base();
	result["name"] = StrMakeValid(GetString(STR_INDUSTRY_NAME, nearest->index));
	result["type"] = nearest->type;
	result["location"] = {
		{"tile", nearest->location.tile != INVALID_TILE ? nearest->location.tile.base() : 0},
		{"x", TileX(nearest->location.tile)},
		{"y", TileY(nearest->location.tile)}
	};
	result["distance"] = min_distance;

	/* Include what it produces/accepts */
	nlohmann::json produces = nlohmann::json::array();
	for (const auto &p : nearest->produced) {
		if (!IsValidCargoType(p.cargo)) continue;
		const CargoSpec *cs = CargoSpec::Get(p.cargo);
		if (cs != nullptr && cs->IsValid()) {
			produces.push_back(StrMakeValid(GetString(cs->name)));
		}
	}
	result["produces"] = produces;

	nlohmann::json accepts = nlohmann::json::array();
	for (const auto &a : nearest->accepted) {
		if (!IsValidCargoType(a.cargo)) continue;
		const CargoSpec *cs = CargoSpec::Get(a.cargo);
		if (cs != nullptr && cs->IsValid()) {
			accepts.push_back(StrMakeValid(GetString(cs->name)));
		}
	}
	result["accepts"] = accepts;

	return result;
}

static nlohmann::json HandleMapInfo([[maybe_unused]] const nlohmann::json &params)
{
	nlohmann::json result;
	result["size_x"] = Map::SizeX();
	result["size_y"] = Map::SizeY();
	result["size_total"] = Map::Size();
	result["max_x"] = Map::MaxX();
	result["max_y"] = Map::MaxY();
	result["climate"] = LandscapeToString(_settings_game.game_creation.landscape);
	return result;
}

static nlohmann::json HandleMapDistance(const nlohmann::json &params)
{
	TileIndex tile1, tile2;

	if (params.contains("tile1") && params.contains("tile2")) {
		tile1 = static_cast<TileIndex>(params["tile1"].get<uint32_t>());
		tile2 = static_cast<TileIndex>(params["tile2"].get<uint32_t>());
	} else if (params.contains("x1") && params.contains("y1") &&
	           params.contains("x2") && params.contains("y2")) {
		uint x1 = params["x1"].get<uint>();
		uint y1 = params["y1"].get<uint>();
		uint x2 = params["x2"].get<uint>();
		uint y2 = params["y2"].get<uint>();
		tile1 = TileXY(x1, y1);
		tile2 = TileXY(x2, y2);
	} else {
		throw std::runtime_error("Missing required parameters: tile1/tile2 or x1/y1/x2/y2");
	}

	nlohmann::json result;
	result["manhattan"] = DistanceManhattan(tile1, tile2);
	result["max"] = DistanceMax(tile1, tile2);
	result["square"] = DistanceSquare(tile1, tile2);
	return result;
}

static nlohmann::json HandleTileGet(const nlohmann::json &params)
{
	TileIndex tile;

	if (params.contains("tile")) {
		tile = static_cast<TileIndex>(params["tile"].get<uint32_t>());
	} else if (params.contains("x") && params.contains("y")) {
		uint x = params["x"].get<uint>();
		uint y = params["y"].get<uint>();
		if (x >= Map::SizeX() || y >= Map::SizeY()) {
			throw std::runtime_error("Coordinates out of bounds");
		}
		tile = TileXY(x, y);
	} else {
		throw std::runtime_error("Missing required parameter: tile or x/y");
	}

	if (tile >= Map::Size()) {
		throw std::runtime_error("Invalid tile index");
	}

	nlohmann::json result;
	result["tile"] = tile.base();
	result["x"] = TileX(tile);
	result["y"] = TileY(tile);
	result["type"] = RpcTileTypeToString(GetTileType(tile));
	result["height"] = TileHeight(tile);

	Slope slope = GetTileSlope(tile);
	result["slope"] = static_cast<int>(slope);
	result["is_flat"] = (slope == SLOPE_FLAT);

	/* GetTileOwner has assertions that fail for MP_HOUSE and MP_INDUSTRY tiles.
	 * Check tile type first to avoid assertion failures. */
	TileType tt = GetTileType(tile);
	if (tt != MP_HOUSE && tt != MP_INDUSTRY) {
		Owner owner = GetTileOwner(tile);
		result["owner"] = owner != INVALID_OWNER ? owner.base() : -1;
	} else {
		result["owner"] = -1;  /* Houses and industries don't have a single owner */
	}

	return result;
}

static nlohmann::json HandleTownList([[maybe_unused]] const nlohmann::json &params)
{
	nlohmann::json result = nlohmann::json::array();

	for (const Town *t : Town::Iterate()) {
		nlohmann::json town_json;
		town_json["id"] = t->index.base();
		town_json["name"] = StrMakeValid(GetString(STR_TOWN_NAME, t->index));
		town_json["location"] = {
			{"tile", t->xy != INVALID_TILE ? t->xy.base() : 0},
			{"x", TileX(t->xy)},
			{"y", TileY(t->xy)}
		};
		town_json["population"] = t->cache.population;
		town_json["houses"] = t->cache.num_houses;
		town_json["is_city"] = t->larger_town;

		result.push_back(town_json);
	}

	return result;
}

static nlohmann::json HandleTownGet(const nlohmann::json &params)
{
	if (!params.contains("id")) {
		throw std::runtime_error("Missing required parameter: id");
	}

	TownID tid = static_cast<TownID>(params["id"].get<int>());
	const Town *t = Town::GetIfValid(tid);
	if (t == nullptr) {
		throw std::runtime_error("Invalid town ID");
	}

	nlohmann::json result;
	result["id"] = t->index.base();
	result["name"] = StrMakeValid(GetString(STR_TOWN_NAME, t->index));
	result["location"] = {
		{"tile", t->xy != INVALID_TILE ? t->xy.base() : 0},
		{"x", TileX(t->xy)},
		{"y", TileY(t->xy)}
	};
	result["population"] = t->cache.population;
	result["houses"] = t->cache.num_houses;
	result["is_city"] = t->larger_town;
	result["growth_rate"] = t->growth_rate != TOWN_GROWTH_RATE_NONE ? static_cast<int>(t->growth_rate) : -1;
	result["fund_buildings_months"] = t->fund_buildings_months;

	/* Company ratings with this town */
	nlohmann::json ratings = nlohmann::json::array();
	for (const Company *c : Company::Iterate()) {
		if (t->have_ratings.Test(c->index)) {
			nlohmann::json rating_json;
			rating_json["company"] = c->index.base();
			rating_json["rating"] = t->ratings[c->index];
			ratings.push_back(rating_json);
		}
	}
	result["ratings"] = ratings;

	return result;
}

/**
 * Handler for town.nearest - Find nearest town matching criteria.
 *
 * Parameters:
 *   x, y: Reference coordinates (required)
 *   min_pop: Minimum population (optional)
 *   is_city: If true, only match cities (optional)
 *
 * Returns the nearest matching town with distance.
 */
static nlohmann::json HandleTownNearest(const nlohmann::json &params)
{
	if (!params.contains("x") || !params.contains("y")) {
		throw std::runtime_error("Missing required parameters: x, y");
	}

	uint ref_x = params["x"].get<uint>();
	uint ref_y = params["y"].get<uint>();
	if (ref_x >= Map::SizeX() || ref_y >= Map::SizeY()) {
		throw std::runtime_error("Coordinates out of bounds");
	}
	TileIndex ref_tile = TileXY(ref_x, ref_y);

	uint min_pop = params.value("min_pop", 0);
	bool require_city = params.value("is_city", false);

	const Town *nearest = nullptr;
	uint min_distance = UINT_MAX;

	for (const Town *t : Town::Iterate()) {
		/* Check population filter */
		if (t->cache.population < min_pop) continue;

		/* Check city filter */
		if (require_city && !t->larger_town) continue;

		uint distance = DistanceManhattan(ref_tile, t->xy);
		if (distance < min_distance) {
			min_distance = distance;
			nearest = t;
		}
	}

	if (nearest == nullptr) {
		throw std::runtime_error("No matching town found");
	}

	nlohmann::json result;
	result["id"] = nearest->index.base();
	result["name"] = StrMakeValid(GetString(STR_TOWN_NAME, nearest->index));
	result["location"] = {
		{"tile", nearest->xy != INVALID_TILE ? nearest->xy.base() : 0},
		{"x", TileX(nearest->xy)},
		{"y", TileY(nearest->xy)}
	};
	result["distance"] = min_distance;
	result["population"] = nearest->cache.population;
	result["houses"] = nearest->cache.num_houses;
	result["is_city"] = nearest->larger_town;

	return result;
}

static nlohmann::json HandleOrderList(const nlohmann::json &params)
{
	if (!params.contains("vehicle_id")) {
		throw std::runtime_error("Missing required parameter: vehicle_id");
	}

	VehicleID vid = static_cast<VehicleID>(params["vehicle_id"].get<int>());
	const Vehicle *v = Vehicle::GetIfValid(vid);
	if (v == nullptr) {
		throw std::runtime_error("Invalid vehicle ID");
	}

	nlohmann::json result;
	result["vehicle_id"] = v->index.base();
	result["vehicle_name"] = StrMakeValid(GetString(STR_VEHICLE_NAME, v->index));
	result["current_order_index"] = v->cur_real_order_index;

	nlohmann::json orders = nlohmann::json::array();

	if (v->orders != nullptr) {
		result["num_orders"] = v->orders->GetNumOrders();
		result["is_shared"] = v->orders->IsShared();
		if (v->orders->IsShared()) {
			result["num_vehicles_sharing"] = v->orders->GetNumVehicles();
		}

		VehicleOrderID idx = 0;
		for (const Order &order : v->orders->GetOrders()) {
			orders.push_back(OrderToJson(order, idx++));
		}
	} else {
		result["num_orders"] = 0;
		result["is_shared"] = false;
	}

	result["orders"] = orders;
	return result;
}

/**
 * Scan a block of tiles and determine the dominant feature.
 * Returns a character representing what's in that area.
 */
struct ScanBlock {
	int rail = 0;
	int road = 0;
	int water = 0;
	int station = 0;
	int industry = 0;
	int house = 0;
	int vehicles = 0;
	int total_tiles = 0;
};

static char GetBlockSymbol(const ScanBlock &block, bool show_traffic)
{
	if (block.total_tiles == 0) return ' ';

	/* If showing traffic and there are vehicles, show density */
	if (show_traffic && block.vehicles > 0) {
		if (block.vehicles >= 10) return '#';
		if (block.vehicles >= 5) return '*';
		return '0' + std::min(block.vehicles, 9);
	}

	/* Priority order for infrastructure display */
	if (block.station > 0) return 'S';
	if (block.industry > 0) return 'I';
	if (block.house > 0) return 'T';
	if (block.rail > 0 && block.road > 0) return 'X';  /* Mixed rail+road */
	if (block.rail > 0) return 'R';
	if (block.road > 0) return '+';
	if (block.water > 0) return '~';

	return '.';
}

static nlohmann::json HandleMapScan(const nlohmann::json &params)
{
	/* Parameters */
	int origin_x = params.value("x", -1);
	int origin_y = params.value("y", -1);
	int zoom = params.value("zoom", 8);  /* Tiles per cell */
	int grid_size = params.value("size", 16);  /* Grid dimensions */
	bool show_traffic = params.value("traffic", false);
	std::string scan_type = params.value("type", "infrastructure");

	/* Clamp values */
	zoom = std::clamp(zoom, 1, 32);
	grid_size = std::clamp(grid_size, 4, 32);

	/* Auto-center if no origin specified */
	if (origin_x < 0 || origin_y < 0) {
		int total_span = grid_size * zoom;
		origin_x = std::max(0, static_cast<int>(Map::SizeX() / 2) - total_span / 2);
		origin_y = std::max(0, static_cast<int>(Map::SizeY() / 2) - total_span / 2);
	}

	nlohmann::json result;
	result["origin"] = {{"x", origin_x}, {"y", origin_y}};
	result["zoom"] = zoom;
	result["grid_size"] = grid_size;
	result["scan_type"] = scan_type;
	result["show_traffic"] = show_traffic;

	/* Build vehicle location map for traffic overlay */
	std::map<uint32_t, int> vehicle_counts;
	if (show_traffic) {
		for (const Vehicle *v : Vehicle::Iterate()) {
			if (!v->IsPrimaryVehicle()) continue;
			if (v->tile == INVALID_TILE) continue;
			uint x = TileX(v->tile);
			uint y = TileY(v->tile);
			/* Map to block coordinates */
			int block_x = (x - origin_x) / zoom;
			int block_y = (y - origin_y) / zoom;
			if (block_x >= 0 && block_x < grid_size && block_y >= 0 && block_y < grid_size) {
				uint32_t key = (block_y << 16) | block_x;
				vehicle_counts[key]++;
			}
		}
	}

	/* Scan the grid */
	nlohmann::json rows = nlohmann::json::array();
	std::map<char, std::string> legend_map;

	for (int gy = 0; gy < grid_size; gy++) {
		std::string row;
		for (int gx = 0; gx < grid_size; gx++) {
			ScanBlock block;

			/* Scan all tiles in this block */
			for (int dy = 0; dy < zoom; dy++) {
				for (int dx = 0; dx < zoom; dx++) {
					int tx = origin_x + gx * zoom + dx;
					int ty = origin_y + gy * zoom + dy;

					if (tx < 0 || ty < 0 || tx >= (int)Map::SizeX() || ty >= (int)Map::SizeY()) {
						continue;
					}

					TileIndex tile = TileXY(tx, ty);
					block.total_tiles++;

					TileType type = GetTileType(tile);
					switch (type) {
						case MP_RAILWAY: block.rail++; break;
						case MP_ROAD: block.road++; break;
						case MP_WATER: block.water++; break;
						case MP_STATION: block.station++; break;
						case MP_INDUSTRY: block.industry++; break;
						case MP_HOUSE: block.house++; break;
						default: break;
					}
				}
			}

			/* Add vehicle count for this block */
			if (show_traffic) {
				uint32_t key = (gy << 16) | gx;
				auto it = vehicle_counts.find(key);
				if (it != vehicle_counts.end()) {
					block.vehicles = it->second;
				}
			}

			char symbol = GetBlockSymbol(block, show_traffic);
			row += symbol;

			/* Build legend */
			if (symbol != '.' && symbol != ' ' && legend_map.find(symbol) == legend_map.end()) {
				switch (symbol) {
					case 'R': legend_map[symbol] = "Railway"; break;
					case '+': legend_map[symbol] = "Road"; break;
					case 'X': legend_map[symbol] = "Rail+Road junction"; break;
					case 'S': legend_map[symbol] = "Station"; break;
					case 'I': legend_map[symbol] = "Industry"; break;
					case 'T': legend_map[symbol] = "Town"; break;
					case '~': legend_map[symbol] = "Water"; break;
					case '#': legend_map[symbol] = "Heavy traffic (10+ vehicles)"; break;
					case '*': legend_map[symbol] = "Busy (5-9 vehicles)"; break;
					default:
						if (symbol >= '1' && symbol <= '9') {
							legend_map[symbol] = std::string(1, symbol) + " vehicle(s)";
						}
						break;
				}
			}
		}
		rows.push_back(row);
	}

	result["rows"] = rows;

	/* Build legend array */
	nlohmann::json legend = nlohmann::json::array();
	legend.push_back({{"symbol", "."}, {"label", "Empty/clear"}});
	for (const auto &pair : legend_map) {
		legend.push_back({{"symbol", std::string(1, pair.first)}, {"label", pair.second}});
	}
	result["legend"] = legend;

	return result;
}

/**
 * Handler for map.terrain - Analyze terrain between two points.
 *
 * Parameters:
 *   x1, y1: Start coordinates (required)
 *   x2, y2: End coordinates (required)
 *
 * Returns terrain analysis including tile counts, height range, and estimated costs.
 */
static nlohmann::json HandleMapTerrain(const nlohmann::json &params)
{
	if (!params.contains("x1") || !params.contains("y1") ||
	    !params.contains("x2") || !params.contains("y2")) {
		throw std::runtime_error("Missing required parameters: x1, y1, x2, y2");
	}

	int x1 = params["x1"].get<int>();
	int y1 = params["y1"].get<int>();
	int x2 = params["x2"].get<int>();
	int y2 = params["y2"].get<int>();

	/* Validate bounds */
	if (x1 < 0 || y1 < 0 || x2 < 0 || y2 < 0 ||
	    x1 >= (int)Map::SizeX() || y1 >= (int)Map::SizeY() ||
	    x2 >= (int)Map::SizeX() || y2 >= (int)Map::SizeY()) {
		throw std::runtime_error("Coordinates out of bounds");
	}

	/* Ensure x1 <= x2 and y1 <= y2 */
	if (x1 > x2) std::swap(x1, x2);
	if (y1 > y2) std::swap(y1, y2);

	/* Count tile types and terrain features */
	int flat_tiles = 0;
	int slope_tiles = 0;
	int water_tiles = 0;
	int clear_tiles = 0;
	int road_tiles = 0;
	int rail_tiles = 0;
	int building_tiles = 0;
	int industry_tiles = 0;
	int station_tiles = 0;
	int min_height = INT_MAX;
	int max_height = 0;
	int total_tiles = 0;

	/* Track water segments for bridge estimation */
	int current_water_run = 0;
	int max_water_run = 0;

	/* Scan the rectangular region */
	for (int y = y1; y <= y2; y++) {
		current_water_run = 0;
		for (int x = x1; x <= x2; x++) {
			TileIndex tile = TileXY(x, y);
			total_tiles++;

			int height = TileHeight(tile);
			if (height < min_height) min_height = height;
			if (height > max_height) max_height = height;

			Slope slope = GetTileSlope(tile);
			if (slope == SLOPE_FLAT) {
				flat_tiles++;
			} else {
				slope_tiles++;
			}

			TileType tt = GetTileType(tile);
			switch (tt) {
				case MP_CLEAR:
				case MP_TREES:
					clear_tiles++;
					current_water_run = 0;
					break;
				case MP_WATER:
					water_tiles++;
					current_water_run++;
					if (current_water_run > max_water_run) {
						max_water_run = current_water_run;
					}
					break;
				case MP_ROAD:
					road_tiles++;
					current_water_run = 0;
					break;
				case MP_RAILWAY:
					rail_tiles++;
					current_water_run = 0;
					break;
				case MP_HOUSE:
					building_tiles++;
					current_water_run = 0;
					break;
				case MP_INDUSTRY:
					industry_tiles++;
					current_water_run = 0;
					break;
				case MP_STATION:
					station_tiles++;
					current_water_run = 0;
					break;
				default:
					current_water_run = 0;
					break;
			}
		}
	}

	/* Calculate buildable tiles (clear land that could have rail/road) */
	int buildable_tiles = clear_tiles;

	/* Estimate costs (rough approximations) */
	int64_t est_rail_cost = buildable_tiles * 100 + slope_tiles * 200;
	int64_t est_road_cost = buildable_tiles * 50 + slope_tiles * 100;

	/* Bridge cost estimate if water crossing needed */
	int64_t est_bridge_cost = 0;
	if (max_water_run > 0) {
		/* Rough bridge cost: base + per-tile cost */
		est_bridge_cost = 5000 + max_water_run * 1500;
	}

	nlohmann::json result;
	result["region"] = {
		{"x1", x1}, {"y1", y1},
		{"x2", x2}, {"y2", y2}
	};
	result["total_tiles"] = total_tiles;
	result["flat_tiles"] = flat_tiles;
	result["slope_tiles"] = slope_tiles;
	result["height_range"] = {{"min", min_height}, {"max", max_height}};

	result["tile_types"] = {
		{"clear", clear_tiles},
		{"water", water_tiles},
		{"road", road_tiles},
		{"rail", rail_tiles},
		{"building", building_tiles},
		{"industry", industry_tiles},
		{"station", station_tiles}
	};

	result["buildable_tiles"] = buildable_tiles;
	result["max_water_crossing"] = max_water_run;

	result["cost_estimates"] = {
		{"rail", est_rail_cost},
		{"road", est_road_cost},
		{"bridge", est_bridge_cost}
	};

	/* Difficulty assessment */
	std::string difficulty;
	int obstacle_pct = (water_tiles + building_tiles + industry_tiles) * 100 / std::max(1, total_tiles);
	int slope_pct = slope_tiles * 100 / std::max(1, total_tiles);

	if (obstacle_pct > 50 || max_water_run > 10) {
		difficulty = "hard";
	} else if (obstacle_pct > 20 || slope_pct > 40 || max_water_run > 5) {
		difficulty = "medium";
	} else {
		difficulty = "easy";
	}
	result["difficulty"] = difficulty;

	return result;
}

/**
 * Handler for engine.list - List available engines.
 *
 * Parameters:
 *   type: Filter by vehicle type ("road", "train", "ship", "aircraft") (optional)
 *   company: Company ID to check availability for (default: 0)
 *   buildable_only: Only show engines that can be built (default: true)
 *
 * Returns array of engines with details.
 */
static nlohmann::json HandleEngineList(const nlohmann::json &params)
{
	nlohmann::json result = nlohmann::json::array();

	/* Get company for buildability check */
	CompanyID company = static_cast<CompanyID>(params.value("company", 0));
	bool buildable_only = params.value("buildable_only", true);

	/* Filter by vehicle type */
	VehicleType filter_type = VEH_INVALID;
	if (params.contains("type")) {
		std::string type_str = params["type"].get<std::string>();
		if (type_str == "road") filter_type = VEH_ROAD;
		else if (type_str == "train") filter_type = VEH_TRAIN;
		else if (type_str == "ship") filter_type = VEH_SHIP;
		else if (type_str == "aircraft") filter_type = VEH_AIRCRAFT;
	}

	for (const Engine *e : Engine::Iterate()) {
		if (!e->IsEnabled()) continue;
		if (filter_type != VEH_INVALID && e->type != filter_type) continue;

		/* Check if buildable for company */
		bool is_buildable = Company::IsValidID(company) && IsEngineBuildable(e->index, e->type, company);
		if (buildable_only && !is_buildable) continue;

		nlohmann::json engine_json;
		engine_json["id"] = e->index.base();
		engine_json["name"] = StrMakeValid(GetString(STR_ENGINE_NAME, e->index));
		engine_json["type"] = RpcVehicleTypeToString(e->type);
		engine_json["buildable"] = is_buildable;

		/* Cost and running cost */
		engine_json["cost"] = e->GetCost() < 0 ? 0 : e->GetCost().base();
		engine_json["running_cost"] = e->GetRunningCost() < 0 ? 0 : e->GetRunningCost().base();

		/* Speed */
		engine_json["max_speed"] = e->GetDisplayMaxSpeed();

		/* Cargo */
		CargoType cargo = e->GetDefaultCargoType();
		if (IsValidCargoType(cargo)) {
			const CargoSpec *cs = CargoSpec::Get(cargo);
			if (cs != nullptr && cs->IsValid()) {
				engine_json["cargo_type"] = cargo;
				engine_json["cargo_name"] = StrMakeValid(GetString(cs->name));
			}
		}

		/* Capacity */
		uint16_t mail_cap = 0;
		uint capacity = e->GetDisplayDefaultCapacity(&mail_cap);
		engine_json["capacity"] = capacity;
		if (mail_cap > 0) {
			engine_json["mail_capacity"] = mail_cap;
		}

		/* Reliability */
		engine_json["reliability"] = e->reliability * 100 / 0x10000;

		/* Power and weight for ground vehicles */
		if (e->type == VEH_TRAIN || e->type == VEH_ROAD) {
			engine_json["power"] = e->GetPower();
			engine_json["weight"] = e->GetDisplayWeight();
		}

		/* Introduction date */
		engine_json["intro_date"] = e->intro_date.base();

		/* For trains, indicate if it's a wagon (no power) */
		if (e->type == VEH_TRAIN) {
			bool is_wagon = (e->GetPower() == 0);
			engine_json["is_wagon"] = is_wagon;
		}

		result.push_back(engine_json);
	}

	return result;
}

/**
 * Handler for engine.get - Get detailed info about a specific engine.
 *
 * Parameters:
 *   id: Engine ID (required)
 *   company: Company ID to check availability for (default: 0)
 *
 * Returns detailed engine information.
 */
static nlohmann::json HandleEngineGet(const nlohmann::json &params)
{
	if (!params.contains("id")) {
		throw std::runtime_error("Missing required parameter: id");
	}

	EngineID eid = static_cast<EngineID>(params["id"].get<int>());
	const Engine *e = Engine::GetIfValid(eid);
	if (e == nullptr || !e->IsEnabled()) {
		throw std::runtime_error("Invalid or unavailable engine ID");
	}

	CompanyID company = static_cast<CompanyID>(params.value("company", 0));
	bool is_buildable = Company::IsValidID(company) && IsEngineBuildable(e->index, e->type, company);

	nlohmann::json result;
	result["id"] = e->index.base();
	result["name"] = StrMakeValid(GetString(STR_ENGINE_NAME, e->index));
	result["type"] = RpcVehicleTypeToString(e->type);
	result["buildable"] = is_buildable;

	/* Cost and running cost */
	result["cost"] = e->GetCost() < 0 ? 0 : e->GetCost().base();
	result["running_cost"] = e->GetRunningCost() < 0 ? 0 : e->GetRunningCost().base();

	/* Speed */
	result["max_speed"] = e->GetDisplayMaxSpeed();

	/* Cargo */
	CargoType cargo = e->GetDefaultCargoType();
	if (IsValidCargoType(cargo)) {
		const CargoSpec *cs = CargoSpec::Get(cargo);
		if (cs != nullptr && cs->IsValid()) {
			result["cargo_type"] = cargo;
			result["cargo_name"] = StrMakeValid(GetString(cs->name));
		}
	}

	/* Capacity */
	uint16_t mail_cap = 0;
	uint capacity = e->GetDisplayDefaultCapacity(&mail_cap);
	result["capacity"] = capacity;
	if (mail_cap > 0) {
		result["mail_capacity"] = mail_cap;
	}

	/* Reliability */
	result["reliability"] = e->reliability * 100 / 0x10000;
	result["reliability_max"] = e->reliability_max * 100 / 0x10000;

	/* Power and weight for ground vehicles */
	if (e->type == VEH_TRAIN || e->type == VEH_ROAD) {
		result["power"] = e->GetPower();
		result["weight"] = e->GetDisplayWeight();
		result["tractive_effort"] = e->GetDisplayMaxTractiveEffort();
	}

	/* Life length */
	result["lifespan_days"] = e->GetLifeLengthInDays().base();
	result["intro_date"] = e->intro_date.base();

	/* For trains */
	if (e->type == VEH_TRAIN) {
		bool is_wagon = (e->GetPower() == 0);
		result["is_wagon"] = is_wagon;
	}

	/* Refit capabilities - list cargo types this engine can be refitted to */
	nlohmann::json refit_cargos = nlohmann::json::array();
	CargoTypes refit_mask = GetUnionOfArticulatedRefitMasks(e->index, true);
	for (CargoType ct = 0; ct < NUM_CARGO; ct++) {
		if (HasBit(refit_mask, ct)) {
			const CargoSpec *cs = CargoSpec::Get(ct);
			if (cs != nullptr && cs->IsValid()) {
				nlohmann::json cargo_json;
				cargo_json["cargo_type"] = ct;
				cargo_json["cargo_name"] = StrMakeValid(GetString(cs->name));
				refit_cargos.push_back(cargo_json);
			}
		}
	}
	if (!refit_cargos.empty()) {
		result["refit_cargos"] = refit_cargos;
	}

	return result;
}

/**
 * Handler for subsidy.list - List all available and awarded subsidies.
 *
 * Returns array of subsidies with source, destination, cargo, and expiry info.
 */
static nlohmann::json HandleSubsidyList([[maybe_unused]] const nlohmann::json &params)
{
	nlohmann::json result = nlohmann::json::array();

	for (const Subsidy *s : Subsidy::Iterate()) {
		if (!IsValidCargoType(s->cargo_type)) continue;

		nlohmann::json subsidy_json;
		subsidy_json["id"] = s->index.base();
		subsidy_json["remaining_months"] = s->remaining;
		subsidy_json["is_awarded"] = s->IsAwarded();

		if (s->IsAwarded()) {
			subsidy_json["awarded_to"] = s->awarded.base();
		}

		/* Cargo info */
		const CargoSpec *cs = CargoSpec::Get(s->cargo_type);
		if (cs != nullptr && cs->IsValid()) {
			subsidy_json["cargo_type"] = s->cargo_type;
			subsidy_json["cargo_name"] = StrMakeValid(GetString(cs->name));
		}

		/* Source info */
		nlohmann::json source_json;
		if (s->src.type == SourceType::Industry) {
			source_json["type"] = "industry";
			source_json["id"] = s->src.id;
			const Industry *ind = Industry::GetIfValid(s->src.ToIndustryID());
			if (ind != nullptr) {
				source_json["name"] = StrMakeValid(GetString(STR_INDUSTRY_NAME, ind->index));
				source_json["location"] = {{"x", TileX(ind->location.tile)}, {"y", TileY(ind->location.tile)}};
			}
		} else if (s->src.type == SourceType::Town) {
			source_json["type"] = "town";
			source_json["id"] = s->src.id;
			const Town *t = Town::GetIfValid(s->src.ToTownID());
			if (t != nullptr) {
				source_json["name"] = StrMakeValid(GetString(STR_TOWN_NAME, t->index));
				source_json["location"] = {{"x", TileX(t->xy)}, {"y", TileY(t->xy)}};
			}
		}
		subsidy_json["source"] = source_json;

		/* Destination info */
		nlohmann::json dest_json;
		if (s->dst.type == SourceType::Industry) {
			dest_json["type"] = "industry";
			dest_json["id"] = s->dst.id;
			const Industry *ind = Industry::GetIfValid(s->dst.ToIndustryID());
			if (ind != nullptr) {
				dest_json["name"] = StrMakeValid(GetString(STR_INDUSTRY_NAME, ind->index));
				dest_json["location"] = {{"x", TileX(ind->location.tile)}, {"y", TileY(ind->location.tile)}};
			}
		} else if (s->dst.type == SourceType::Town) {
			dest_json["type"] = "town";
			dest_json["id"] = s->dst.id;
			const Town *t = Town::GetIfValid(s->dst.ToTownID());
			if (t != nullptr) {
				dest_json["name"] = StrMakeValid(GetString(STR_TOWN_NAME, t->index));
				dest_json["location"] = {{"x", TileX(t->xy)}, {"y", TileY(t->xy)}};
			}
		}
		subsidy_json["destination"] = dest_json;

		result.push_back(subsidy_json);
	}

	return result;
}

/**
 * Handler for cargo.list - List all cargo types with their properties.
 */
static nlohmann::json HandleCargoList([[maybe_unused]] const nlohmann::json &params)
{
	nlohmann::json result = nlohmann::json::array();

	for (const CargoSpec *cs : CargoSpec::Iterate()) {
		if (!cs->IsValid()) continue;

		nlohmann::json cargo_json;
		cargo_json["id"] = cs->Index();
		cargo_json["name"] = StrMakeValid(GetString(cs->name));
		cargo_json["is_freight"] = cs->is_freight;

		/* Cargo label (4-char code like PASS, COAL) */
		std::string label;
		for (uint i = 0; i < sizeof(cs->label); i++) {
			label.push_back(GB(cs->label.base(), (sizeof(cs->label) - i - 1) * 8, 8));
		}
		cargo_json["label"] = label;

		/* Town effect */
		const char *effect = "none";
		switch (cs->town_acceptance_effect) {
			case TAE_PASSENGERS: effect = "passengers"; break;
			case TAE_MAIL: effect = "mail"; break;
			case TAE_GOODS: effect = "goods"; break;
			case TAE_WATER: effect = "water"; break;
			case TAE_FOOD: effect = "food"; break;
			default: break;
		}
		cargo_json["town_effect"] = effect;

		result.push_back(cargo_json);
	}

	return result;
}

/**
 * Handler for cargo.getIncome - Calculate revenue for transporting cargo.
 *
 * Parameters:
 *   cargo_type: Cargo type ID (required)
 *   distance: Manhattan distance in tiles (required)
 *   days_in_transit: Days the cargo is in transit (required)
 *   amount: Number of cargo units (default: 1)
 *
 * Returns the income in pounds.
 */
static nlohmann::json HandleCargoGetIncome(const nlohmann::json &params)
{
	if (!params.contains("cargo_type") || !params.contains("distance") || !params.contains("days_in_transit")) {
		throw std::runtime_error("Missing required parameters: cargo_type, distance, days_in_transit");
	}

	CargoType cargo_type = params["cargo_type"].get<int>();
	if (cargo_type >= NUM_CARGO || !CargoSpec::Get(cargo_type)->IsValid()) {
		throw std::runtime_error("Invalid cargo type");
	}

	uint32_t distance = params["distance"].get<uint32_t>();
	uint16_t days_in_transit = params["days_in_transit"].get<uint16_t>();
	uint32_t amount = params.value("amount", 1);

	/* Convert days to the internal format (ticks_in_transit is days * 2 / 5 internally) */
	uint16_t ticks = std::min<uint16_t>(days_in_transit * 2 / 5, UINT16_MAX);

	Money income = GetTransportedGoodsIncome(amount, distance, ticks, cargo_type);

	nlohmann::json result;
	result["cargo_type"] = cargo_type;
	result["distance"] = distance;
	result["days_in_transit"] = days_in_transit;
	result["amount"] = amount;
	result["income"] = income.base();

	return result;
}

/**
 * Handler for cargomonitor.getDelivery - Get/start monitoring cargo deliveries.
 *
 * Parameters:
 *   company: Company ID (required)
 *   cargo_type: Cargo type (required)
 *   industry_id: Industry ID (required if monitoring industry)
 *   town_id: Town ID (required if monitoring town)
 *   keep_monitoring: Continue monitoring after this call (default: true)
 *
 * Returns the amount delivered since last query.
 */
static nlohmann::json HandleCargoMonitorGetDelivery(const nlohmann::json &params)
{
	if (!params.contains("company") || !params.contains("cargo_type")) {
		throw std::runtime_error("Missing required parameters: company, cargo_type");
	}
	if (!params.contains("industry_id") && !params.contains("town_id")) {
		throw std::runtime_error("Missing required parameter: industry_id or town_id");
	}

	CompanyID company = static_cast<CompanyID>(params["company"].get<int>());
	CargoType cargo = params["cargo_type"].get<int>();
	bool keep_monitoring = params.value("keep_monitoring", true);

	CargoMonitorID monitor;
	nlohmann::json result;

	if (params.contains("industry_id")) {
		IndustryID ind = static_cast<IndustryID>(params["industry_id"].get<int>());
		if (!Industry::IsValidID(ind)) {
			throw std::runtime_error("Invalid industry ID");
		}
		monitor = EncodeCargoIndustryMonitor(company, cargo, ind);
		result["industry_id"] = ind.base();
	} else {
		TownID town = static_cast<TownID>(params["town_id"].get<int>());
		if (!Town::IsValidID(town)) {
			throw std::runtime_error("Invalid town ID");
		}
		monitor = EncodeCargoTownMonitor(company, cargo, town);
		result["town_id"] = town.base();
	}

	int32_t amount = GetDeliveryAmount(monitor, keep_monitoring);

	result["company"] = company.base();
	result["cargo_type"] = cargo;
	result["amount"] = amount;
	result["keep_monitoring"] = keep_monitoring;

	return result;
}

/**
 * Handler for cargomonitor.getPickup - Get/start monitoring cargo pickups.
 *
 * Parameters same as cargomonitor.getDelivery
 */
static nlohmann::json HandleCargoMonitorGetPickup(const nlohmann::json &params)
{
	if (!params.contains("company") || !params.contains("cargo_type")) {
		throw std::runtime_error("Missing required parameters: company, cargo_type");
	}
	if (!params.contains("industry_id") && !params.contains("town_id")) {
		throw std::runtime_error("Missing required parameter: industry_id or town_id");
	}

	CompanyID company = static_cast<CompanyID>(params["company"].get<int>());
	CargoType cargo = params["cargo_type"].get<int>();
	bool keep_monitoring = params.value("keep_monitoring", true);

	CargoMonitorID monitor;
	nlohmann::json result;

	if (params.contains("industry_id")) {
		IndustryID ind = static_cast<IndustryID>(params["industry_id"].get<int>());
		if (!Industry::IsValidID(ind)) {
			throw std::runtime_error("Invalid industry ID");
		}
		monitor = EncodeCargoIndustryMonitor(company, cargo, ind);
		result["industry_id"] = ind.base();
	} else {
		TownID town = static_cast<TownID>(params["town_id"].get<int>());
		if (!Town::IsValidID(town)) {
			throw std::runtime_error("Invalid town ID");
		}
		monitor = EncodeCargoTownMonitor(company, cargo, town);
		result["town_id"] = town.base();
	}

	int32_t amount = GetPickupAmount(monitor, keep_monitoring);

	result["company"] = company.base();
	result["cargo_type"] = cargo;
	result["amount"] = amount;
	result["keep_monitoring"] = keep_monitoring;

	return result;
}

/**
 * Handler for industry.getStockpile - Get cargo stockpiled at an industry.
 *
 * Parameters:
 *   id: Industry ID (required)
 *
 * Returns stockpiled cargo amounts for each accepted cargo type.
 */
static nlohmann::json HandleIndustryGetStockpile(const nlohmann::json &params)
{
	if (!params.contains("id")) {
		throw std::runtime_error("Missing required parameter: id");
	}

	IndustryID iid = static_cast<IndustryID>(params["id"].get<int>());
	const Industry *ind = Industry::GetIfValid(iid);
	if (ind == nullptr) {
		throw std::runtime_error("Invalid industry ID");
	}

	nlohmann::json result;
	result["id"] = ind->index.base();
	result["name"] = StrMakeValid(GetString(STR_INDUSTRY_NAME, ind->index));

	nlohmann::json stockpile = nlohmann::json::array();
	for (const auto &a : ind->accepted) {
		if (!IsValidCargoType(a.cargo)) continue;
		const CargoSpec *cs = CargoSpec::Get(a.cargo);
		if (!cs->IsValid()) continue;

		nlohmann::json cargo_json;
		cargo_json["cargo_id"] = a.cargo;
		cargo_json["cargo_name"] = StrMakeValid(GetString(cs->name));
		cargo_json["stockpiled"] = a.waiting;
		stockpile.push_back(cargo_json);
	}
	result["stockpile"] = stockpile;

	return result;
}

/**
 * Handler for industry.getAcceptance - Check cargo acceptance state for an industry.
 *
 * Parameters:
 *   id: Industry ID (required)
 *   cargo_type: Optional cargo type to check (if omitted, returns all)
 *
 * Returns acceptance state: "accepted", "refused", or "not_accepted"
 */
static nlohmann::json HandleIndustryGetAcceptance(const nlohmann::json &params)
{
	if (!params.contains("id")) {
		throw std::runtime_error("Missing required parameter: id");
	}

	IndustryID iid = static_cast<IndustryID>(params["id"].get<int>());
	const Industry *ind = Industry::GetIfValid(iid);
	if (ind == nullptr) {
		throw std::runtime_error("Invalid industry ID");
	}

	nlohmann::json result;
	result["id"] = ind->index.base();
	result["name"] = StrMakeValid(GetString(STR_INDUSTRY_NAME, ind->index));

	auto get_acceptance_state = [&ind](CargoType cargo) -> const char* {
		for (const auto &a : ind->accepted) {
			if (a.cargo == cargo) {
				/* Check if industry is currently accepting */
				if (IsValidCargoType(a.cargo)) {
					/* Industries that are "waiting" have waiting > 0 which means they're processing */
					/* For now, just check if it's in the accepted list */
					return "accepted";
				}
			}
		}
		return "not_accepted";
	};

	if (params.contains("cargo_type")) {
		CargoType cargo = params["cargo_type"].get<int>();
		if (cargo >= NUM_CARGO) {
			throw std::runtime_error("Invalid cargo type");
		}
		result["cargo_type"] = cargo;
		result["state"] = get_acceptance_state(cargo);
	} else {
		nlohmann::json acceptance = nlohmann::json::array();
		for (const auto &a : ind->accepted) {
			if (!IsValidCargoType(a.cargo)) continue;
			const CargoSpec *cs = CargoSpec::Get(a.cargo);
			if (!cs->IsValid()) continue;

			nlohmann::json cargo_json;
			cargo_json["cargo_id"] = a.cargo;
			cargo_json["cargo_name"] = StrMakeValid(GetString(cs->name));
			cargo_json["state"] = "accepted";
			acceptance.push_back(cargo_json);
		}
		result["acceptance"] = acceptance;
	}

	return result;
}

/**
 * Handler for station.getCargoPlanned - Get planned cargo flow through a station.
 *
 * Parameters:
 *   id: Station ID (required)
 *   cargo_type: Optional cargo type filter
 *
 * Returns planned monthly cargo flow per cargo type.
 */
static nlohmann::json HandleStationGetCargoPlanned(const nlohmann::json &params)
{
	if (!params.contains("id")) {
		throw std::runtime_error("Missing required parameter: id");
	}

	StationID sid = static_cast<StationID>(params["id"].get<int>());
	const Station *st = Station::GetIfValid(sid);
	if (st == nullptr) {
		throw std::runtime_error("Invalid station ID");
	}

	nlohmann::json result;
	result["id"] = st->index.base();
	result["name"] = StrMakeValid(st->GetCachedName());

	nlohmann::json planned = nlohmann::json::array();

	CargoType filter_cargo = INVALID_CARGO;
	if (params.contains("cargo_type")) {
		filter_cargo = params["cargo_type"].get<int>();
	}

	for (CargoType c = 0; c < NUM_CARGO; c++) {
		if (filter_cargo != INVALID_CARGO && c != filter_cargo) continue;

		const GoodsEntry &ge = st->goods[c];
		if (!ge.HasRating() && ge.TotalCount() == 0) continue;

		const CargoSpec *cs = CargoSpec::Get(c);
		if (!cs->IsValid()) continue;

		/* Get link graph capacity/usage data */
		uint total_capacity = 0;
		uint total_usage = 0;
		if (ge.link_graph != LinkGraphID::Invalid() && ge.node != INVALID_NODE) {
			const LinkGraph *lg = LinkGraph::GetIfValid(ge.link_graph);
			if (lg != nullptr && ge.node < lg->Size()) {
				/* Sum outgoing edge capacities */
				for (auto &edge : (*lg)[ge.node].edges) {
					total_capacity += edge.capacity;
					total_usage += edge.usage;
				}
			}
		}

		nlohmann::json cargo_json;
		cargo_json["cargo_id"] = c;
		cargo_json["cargo_name"] = StrMakeValid(GetString(cs->name));
		cargo_json["waiting"] = ge.TotalCount();
		cargo_json["rating"] = ge.HasRating() ? ge.rating * 100 / 255 : -1;
		cargo_json["link_capacity"] = total_capacity;
		cargo_json["link_usage"] = total_usage;
		planned.push_back(cargo_json);
	}
	result["cargo"] = planned;

	return result;
}

/**
 * Handler for station.getCoverage - Get industries and towns covered by a station.
 *
 * Parameters:
 *   id: Station ID (required)
 *
 * Returns:
 *   station_id: The station ID
 *   station_name: The station name
 *   catchment_radius: The catchment radius
 *   industries: Array of industries within catchment
 *   towns: Array of towns within catchment
 *   accepts: Cargo types accepted at this station
 *   supplies: Cargo types that can be picked up here
 */
static nlohmann::json HandleStationGetCoverage(const nlohmann::json &params)
{
	if (!params.contains("id")) {
		throw std::runtime_error("Missing required parameter: id");
	}

	StationID sid = static_cast<StationID>(params["id"].get<int>());
	const Station *st = Station::GetIfValid(sid);
	if (st == nullptr) {
		throw std::runtime_error("Invalid station ID");
	}

	nlohmann::json result;
	result["station_id"] = st->index.base();
	result["station_name"] = StrMakeValid(GetString(STR_STATION_NAME, st->index));
	result["catchment_radius"] = st->GetCatchmentRadius();

	/* Get industries within catchment */
	nlohmann::json industries = nlohmann::json::array();
	for (const IndustryListEntry &entry : st->industries_near) {
		const Industry *ind = entry.industry;
		nlohmann::json ind_json;
		ind_json["id"] = ind->index.base();
		ind_json["type"] = StrMakeValid(GetString(GetIndustrySpec(ind->type)->name));
		ind_json["tile"] = ind->location.tile.base();
		ind_json["distance"] = entry.distance;

		/* What cargo does this industry accept? */
		nlohmann::json accepts_arr = nlohmann::json::array();
		for (const auto &a : ind->accepted) {
			if (!IsValidCargoType(a.cargo)) continue;
			const CargoSpec *cs = CargoSpec::Get(a.cargo);
			if (cs == nullptr || !cs->IsValid()) continue;
			accepts_arr.push_back(StrMakeValid(GetString(cs->name)));
		}
		ind_json["accepts"] = accepts_arr;

		/* What cargo does this industry produce? */
		nlohmann::json produces_arr = nlohmann::json::array();
		for (const auto &p : ind->produced) {
			if (!IsValidCargoType(p.cargo)) continue;
			const CargoSpec *cs = CargoSpec::Get(p.cargo);
			if (cs == nullptr || !cs->IsValid()) continue;
			produces_arr.push_back(StrMakeValid(GetString(cs->name)));
		}
		ind_json["produces"] = produces_arr;

		industries.push_back(ind_json);
	}
	result["industries"] = industries;

	/* Get towns within catchment */
	nlohmann::json towns = nlohmann::json::array();
	for (const Town *t : Town::Iterate()) {
		if (st->CatchmentCoversTown(t->index)) {
			nlohmann::json town_json;
			town_json["id"] = t->index.base();
			town_json["name"] = StrMakeValid(GetString(STR_TOWN_NAME, t->index));
			town_json["population"] = t->cache.population;
			towns.push_back(town_json);
		}
	}
	result["towns"] = towns;

	/* What cargo types does this station accept? */
	nlohmann::json accepts = nlohmann::json::array();
	nlohmann::json supplies = nlohmann::json::array();

	for (CargoType c = 0; c < NUM_CARGO; c++) {
		if (!IsValidCargoType(c)) continue;
		const CargoSpec *cs = CargoSpec::Get(c);
		if (cs == nullptr || !cs->IsValid()) continue;

		const GoodsEntry &ge = st->goods[c];

		/* Check if station accepts this cargo */
		if (IsCargoInClass(c, CargoClass::Passengers) || IsCargoInClass(c, CargoClass::Mail)) {
			/* Passengers and mail are accepted if any town is in catchment */
			if (!towns.empty()) {
				accepts.push_back(StrMakeValid(GetString(cs->name)));
			}
		} else {
			/* Other cargo - check if any industry accepts it */
			for (const IndustryListEntry &entry : st->industries_near) {
				const Industry *ind = entry.industry;
				for (const auto &a : ind->accepted) {
					if (a.cargo == c) {
						accepts.push_back(StrMakeValid(GetString(cs->name)));
						goto next_cargo_accept;
					}
				}
			}
			next_cargo_accept:;
		}

		/* Check if cargo is waiting (means it can be picked up here) */
		if (ge.HasRating() || ge.TotalCount() > 0) {
			supplies.push_back(StrMakeValid(GetString(cs->name)));
		}
	}
	result["accepts"] = accepts;
	result["supplies"] = supplies;

	return result;
}

/**
 * Handler for vehicle.getCargoByType - Get detailed cargo breakdown for a vehicle.
 *
 * Parameters:
 *   id: Vehicle ID (required)
 *
 * Returns cargo load and capacity for each cargo type the vehicle can carry.
 */
static nlohmann::json HandleVehicleGetCargoByType(const nlohmann::json &params)
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
	result["name"] = StrMakeValid(GetString(STR_VEHICLE_NAME, v->index));
	result["type"] = RpcVehicleTypeToString(v->type);

	/* Aggregate cargo across all parts of the vehicle */
	std::map<CargoType, std::pair<uint, uint>> cargo_data;  /* cargo -> (loaded, capacity) */

	for (const Vehicle *u = v; u != nullptr; u = u->Next()) {
		if (u->cargo_cap > 0 && IsValidCargoType(u->cargo_type)) {
			auto &data = cargo_data[u->cargo_type];
			data.first += u->cargo.StoredCount();
			data.second += u->cargo_cap;
		}
	}

	nlohmann::json cargo_list = nlohmann::json::array();
	for (const auto &pair : cargo_data) {
		CargoType ct = pair.first;
		const CargoSpec *cs = CargoSpec::Get(ct);
		if (cs == nullptr || !cs->IsValid()) continue;

		nlohmann::json cargo_json;
		cargo_json["cargo_id"] = ct;
		cargo_json["cargo_name"] = StrMakeValid(GetString(cs->name));
		cargo_json["loaded"] = pair.second.first;
		cargo_json["capacity"] = pair.second.second;
		cargo_json["utilization_pct"] = pair.second.second > 0 ? (pair.second.first * 100 / pair.second.second) : 0;
		cargo_list.push_back(cargo_json);
	}
	result["cargo"] = cargo_list;

	/* Total utilization */
	uint total_loaded = 0, total_capacity = 0;
	for (const auto &pair : cargo_data) {
		total_loaded += pair.second.first;
		total_capacity += pair.second.second;
	}
	result["total_loaded"] = total_loaded;
	result["total_capacity"] = total_capacity;
	result["total_utilization_pct"] = total_capacity > 0 ? (total_loaded * 100 / total_capacity) : 0;

	return result;
}

/**
 * Handler for airport.info - Get information about airport types.
 *
 * Parameters:
 *   type: Airport type name (optional, returns all if omitted)
 *
 * Returns airport specifications including size, cost, and availability.
 */
static nlohmann::json HandleAirportInfo(const nlohmann::json &params)
{
	auto get_airport_type_name = [](AirportTypes type) -> const char* {
		switch (type) {
			case AT_SMALL: return "small";
			case AT_LARGE: return "large";
			case AT_METROPOLITAN: return "metropolitan";
			case AT_INTERNATIONAL: return "international";
			case AT_COMMUTER: return "commuter";
			case AT_INTERCON: return "intercontinental";
			case AT_HELIPORT: return "heliport";
			case AT_HELISTATION: return "helistation";
			case AT_HELIDEPOT: return "helidepot";
			case AT_OILRIG: return "oilrig";
			default: return "unknown";
		}
	};

	auto airport_type_from_string = [](const std::string &name) -> AirportTypes {
		if (name == "small") return AT_SMALL;
		if (name == "large") return AT_LARGE;
		if (name == "metropolitan") return AT_METROPOLITAN;
		if (name == "international") return AT_INTERNATIONAL;
		if (name == "commuter") return AT_COMMUTER;
		if (name == "intercontinental") return AT_INTERCON;
		if (name == "heliport") return AT_HELIPORT;
		if (name == "helistation") return AT_HELISTATION;
		if (name == "helidepot") return AT_HELIDEPOT;
		if (name == "oilrig") return AT_OILRIG;
		return AT_INVALID;
	};

	auto get_airport_info = [&](AirportTypes type) -> nlohmann::json {
		nlohmann::json info;
		info["type"] = get_airport_type_name(type);
		info["type_id"] = static_cast<int>(type);

		const AirportSpec *as = AirportSpec::Get(type);
		if (as == nullptr || !as->enabled) {
			info["available"] = false;
			return info;
		}

		info["available"] = true;
		info["width"] = as->size_x;
		info["height"] = as->size_y;
		info["catchment_radius"] = as->catchment;
		info["noise_level"] = as->noise_level;
		info["num_hangars"] = static_cast<int>(as->depots.size());

		/* Check if it's helicopter-only */
		bool heli_only = (type == AT_HELIPORT || type == AT_HELISTATION || type == AT_HELIDEPOT);
		info["helicopter_only"] = heli_only;

		/* Get maintenance cost */
		info["maintenance_cost_factor"] = as->maintenance_cost;

		return info;
	};

	if (params.contains("type")) {
		std::string type_name = params["type"].get<std::string>();
		AirportTypes type = airport_type_from_string(type_name);
		if (type == AT_INVALID) {
			throw std::runtime_error("Invalid airport type: " + type_name);
		}
		return get_airport_info(type);
	}

	/* Return all standard airport types (0-9) */
	nlohmann::json result = nlohmann::json::array();
	static const AirportTypes standard_airports[] = {
		AT_SMALL, AT_LARGE, AT_HELIPORT, AT_METROPOLITAN,
		AT_INTERNATIONAL, AT_COMMUTER, AT_HELIDEPOT,
		AT_INTERCON, AT_HELISTATION, AT_OILRIG
	};
	for (AirportTypes type : standard_airports) {
		const AirportSpec *as = AirportSpec::Get(type);
		if (as != nullptr && as->enabled) {
			result.push_back(get_airport_info(type));
		}
	}
	return result;
}

/**
 * Convert NewsType to a string.
 */
static const char *NewsTypeToString(NewsType type)
{
	switch (type) {
		case NewsType::ArrivalCompany: return "arrival_company";
		case NewsType::ArrivalOther: return "arrival_other";
		case NewsType::Accident: return "accident";
		case NewsType::AccidentOther: return "accident_other";
		case NewsType::CompanyInfo: return "company_info";
		case NewsType::IndustryOpen: return "industry_open";
		case NewsType::IndustryClose: return "industry_close";
		case NewsType::Economy: return "economy";
		case NewsType::IndustryCompany: return "industry_company";
		case NewsType::IndustryOther: return "industry_other";
		case NewsType::IndustryNobody: return "industry_nobody";
		case NewsType::Advice: return "advice";
		case NewsType::NewVehicles: return "new_vehicles";
		case NewsType::Acceptance: return "acceptance";
		case NewsType::Subsidies: return "subsidies";
		case NewsType::General: return "general";
		default: return "unknown";
	}
}

/**
 * Convert AdviceType to a string.
 */
static const char *AdviceTypeToString(AdviceType type)
{
	switch (type) {
		case AdviceType::AircraftDestinationTooFar: return "aircraft_destination_too_far";
		case AdviceType::AutorenewFailed: return "autorenew_failed";
		case AdviceType::Order: return "order_problem";
		case AdviceType::RefitFailed: return "refit_failed";
		case AdviceType::TrainStuck: return "train_stuck";
		case AdviceType::VehicleLost: return "vehicle_lost";
		case AdviceType::VehicleOld: return "vehicle_old";
		case AdviceType::VehicleUnprofitable: return "vehicle_unprofitable";
		case AdviceType::VehicleWaiting: return "vehicle_waiting";
		default: return "unknown";
	}
}

/**
 * Handler for company.alerts - Get recent news/alerts relevant to the company.
 *
 * Parameters:
 *   company: Company ID (default: 0)
 *   limit: Maximum number of alerts to return (default: 20)
 *   types: Optional array of types to filter by (default: all advice/accident types)
 *
 * Returns:
 *   alerts: Array of alert objects with type, message, date, and references
 */
static nlohmann::json HandleCompanyAlerts(const nlohmann::json &params)
{
	CompanyID company = static_cast<CompanyID>(params.value("company", 0));
	int limit = params.value("limit", 20);

	nlohmann::json result;
	nlohmann::json alerts = nlohmann::json::array();

	const NewsContainer &news = GetNews();
	int count = 0;

	for (const NewsItem &item : news) {
		if (count >= limit) break;

		/* Filter to relevant news types for company alerts */
		bool relevant = false;
		switch (item.type) {
			case NewsType::Advice:
			case NewsType::Accident:
				relevant = true;
				break;
			case NewsType::IndustryCompany:
			case NewsType::ArrivalCompany:
				relevant = true;
				break;
			default:
				break;
		}

		if (!relevant) continue;

		/* Check if this news item is relevant to the specified company */
		/* For vehicle news, check if the vehicle belongs to the company */
		if (std::holds_alternative<VehicleID>(item.ref1)) {
			VehicleID vid = std::get<VehicleID>(item.ref1);
			const Vehicle *v = Vehicle::GetIfValid(vid);
			if (v == nullptr || v->owner != company) continue;
		}

		nlohmann::json alert;
		alert["type"] = NewsTypeToString(item.type);
		alert["advice_type"] = AdviceTypeToString(item.advice_type);
		alert["date"] = item.date.base();
		alert["message"] = item.GetStatusText();

		/* Add reference information */
		if (std::holds_alternative<VehicleID>(item.ref1)) {
			alert["vehicle_id"] = std::get<VehicleID>(item.ref1).base();
		}
		if (std::holds_alternative<StationID>(item.ref1)) {
			alert["station_id"] = std::get<StationID>(item.ref1).base();
		}
		if (std::holds_alternative<TileIndex>(item.ref1)) {
			alert["tile"] = std::get<TileIndex>(item.ref1).base();
		}
		if (std::holds_alternative<IndustryID>(item.ref1)) {
			alert["industry_id"] = std::get<IndustryID>(item.ref1).base();
		}

		alerts.push_back(alert);
		count++;
	}

	result["alerts"] = alerts;
	result["count"] = count;

	return result;
}

/**
 * Check if a road tile is connected to the road network.
 * Uses flood fill to find if we can reach from start to target.
 */
static bool IsRoadConnected(TileIndex start, TileIndex target, int max_tiles = 1000)
{
	if (start == target) return true;
	if (!IsValidTile(start) || !IsValidTile(target)) return false;

	/* Simple BFS to check connectivity */
	std::vector<TileIndex> queue;
	std::set<uint32_t> visited;

	queue.push_back(start);
	visited.insert(start.base());

	int tiles_checked = 0;

	while (!queue.empty() && tiles_checked < max_tiles) {
		TileIndex current = queue.back();
		queue.pop_back();
		tiles_checked++;

		if (current == target) return true;

		/* Check all 4 directions */
		for (DiagDirection dir = DIAGDIR_BEGIN; dir < DIAGDIR_END; dir++) {
			TileIndex next = TileAddByDiagDir(current, dir);
			if (!IsValidTile(next)) continue;
			if (visited.count(next.base())) continue;

			/* Check if the next tile has road */
			bool has_road = false;
			if (IsTileType(next, MP_ROAD)) {
				has_road = true;
			} else if (IsTileType(next, MP_STATION)) {
				/* Road stops are also valid road tiles */
				if (IsStationRoadStopTile(next) || IsRoadWaypointTile(next)) {
					has_road = true;
				}
			}

			if (has_road) {
				visited.insert(next.base());
				queue.push_back(next);
			}
		}
	}

	return false;
}

/**
 * Check if a rail tile is connected to another via rail.
 */
static bool IsRailConnected(TileIndex start, TileIndex target, int max_tiles = 1000)
{
	if (start == target) return true;
	if (!IsValidTile(start) || !IsValidTile(target)) return false;

	std::vector<TileIndex> queue;
	std::set<uint32_t> visited;

	queue.push_back(start);
	visited.insert(start.base());

	int tiles_checked = 0;

	while (!queue.empty() && tiles_checked < max_tiles) {
		TileIndex current = queue.back();
		queue.pop_back();
		tiles_checked++;

		if (current == target) return true;

		/* Check all 4 directions */
		for (DiagDirection dir = DIAGDIR_BEGIN; dir < DIAGDIR_END; dir++) {
			TileIndex next = TileAddByDiagDir(current, dir);
			if (!IsValidTile(next)) continue;
			if (visited.count(next.base())) continue;

			/* Check if the next tile has rail */
			bool has_rail = false;
			if (IsTileType(next, MP_RAILWAY)) {
				has_rail = true;
			} else if (IsTileType(next, MP_STATION)) {
				if (IsRailStationTile(next)) {
					has_rail = true;
				}
			}

			if (has_rail) {
				visited.insert(next.base());
				queue.push_back(next);
			}
		}
	}

	return false;
}

/**
 * Handler for route.check - Check if two tiles are connected for a given transport type.
 *
 * Parameters:
 *   start_tile or start_x/start_y: Starting tile (required)
 *   end_tile or end_x/end_y: Ending tile (required)
 *   transport_type: "road", "rail", "water" (required)
 *
 * Returns:
 *   connected: Whether the tiles are connected
 *   start_tile: The starting tile
 *   end_tile: The ending tile
 */
static nlohmann::json HandleRouteCheck(const nlohmann::json &params)
{
	/* Get start tile */
	TileIndex start_tile;
	if (params.contains("start_tile")) {
		start_tile = static_cast<TileIndex>(params["start_tile"].get<uint32_t>());
	} else if (params.contains("start_x") && params.contains("start_y")) {
		uint x = params["start_x"].get<uint>();
		uint y = params["start_y"].get<uint>();
		if (x >= Map::SizeX() || y >= Map::SizeY()) {
			throw std::runtime_error("Start coordinates out of bounds");
		}
		start_tile = TileXY(x, y);
	} else {
		throw std::runtime_error("Missing required parameter: start_tile or start_x/start_y");
	}

	/* Get end tile */
	TileIndex end_tile;
	if (params.contains("end_tile")) {
		end_tile = static_cast<TileIndex>(params["end_tile"].get<uint32_t>());
	} else if (params.contains("end_x") && params.contains("end_y")) {
		uint x = params["end_x"].get<uint>();
		uint y = params["end_y"].get<uint>();
		if (x >= Map::SizeX() || y >= Map::SizeY()) {
			throw std::runtime_error("End coordinates out of bounds");
		}
		end_tile = TileXY(x, y);
	} else {
		throw std::runtime_error("Missing required parameter: end_tile or end_x/end_y");
	}

	/* Get transport type */
	if (!params.contains("transport_type")) {
		throw std::runtime_error("Missing required parameter: transport_type");
	}
	std::string transport_type = params["transport_type"].get<std::string>();

	nlohmann::json result;
	result["start_tile"] = start_tile.base();
	result["end_tile"] = end_tile.base();
	result["transport_type"] = transport_type;

	bool connected = false;
	std::string error;

	if (transport_type == "road") {
		/* Check if start tile has road */
		bool start_has_road = IsTileType(start_tile, MP_ROAD) ||
			(IsTileType(start_tile, MP_STATION) && (IsStationRoadStopTile(start_tile) || IsRoadWaypointTile(start_tile)));
		bool end_has_road = IsTileType(end_tile, MP_ROAD) ||
			(IsTileType(end_tile, MP_STATION) && (IsStationRoadStopTile(end_tile) || IsRoadWaypointTile(end_tile)));

		if (!start_has_road) {
			error = "Start tile does not have road";
		} else if (!end_has_road) {
			error = "End tile does not have road";
		} else {
			connected = IsRoadConnected(start_tile, end_tile);
			if (!connected) {
				error = "Tiles are not connected by road";
			}
		}
	} else if (transport_type == "rail") {
		bool start_has_rail = IsTileType(start_tile, MP_RAILWAY) ||
			(IsTileType(start_tile, MP_STATION) && IsRailStationTile(start_tile));
		bool end_has_rail = IsTileType(end_tile, MP_RAILWAY) ||
			(IsTileType(end_tile, MP_STATION) && IsRailStationTile(end_tile));

		if (!start_has_rail) {
			error = "Start tile does not have rail";
		} else if (!end_has_rail) {
			error = "End tile does not have rail";
		} else {
			connected = IsRailConnected(start_tile, end_tile);
			if (!connected) {
				error = "Tiles are not connected by rail";
			}
		}
	} else if (transport_type == "water") {
		/* Water is always connected if both tiles are water */
		bool start_is_water = IsTileType(start_tile, MP_WATER) ||
			(IsTileType(start_tile, MP_STATION) && IsDockTile(start_tile));
		bool end_is_water = IsTileType(end_tile, MP_WATER) ||
			(IsTileType(end_tile, MP_STATION) && IsDockTile(end_tile));

		if (!start_is_water) {
			error = "Start tile is not water/dock";
		} else if (!end_is_water) {
			error = "End tile is not water/dock";
		} else {
			/* For water, assume connected if both are water tiles */
			/* Full water pathfinding would be complex */
			connected = true;
		}
	} else {
		throw std::runtime_error("Invalid transport_type - must be: road, rail, water");
	}

	result["connected"] = connected;
	if (!error.empty()) {
		result["error"] = error;
	}

	return result;
}

void RpcRegisterQueryHandlers(RpcServer &server)
{
	server.RegisterHandler("ping", HandlePing);
	server.RegisterHandler("game.status", HandleGameStatus);
	server.RegisterHandler("company.list", HandleCompanyList);
	server.RegisterHandler("vehicle.list", HandleVehicleList);
	server.RegisterHandler("vehicle.get", HandleVehicleGet);
	server.RegisterHandler("station.list", HandleStationList);
	server.RegisterHandler("station.get", HandleStationGet);
	server.RegisterHandler("industry.list", HandleIndustryList);
	server.RegisterHandler("industry.get", HandleIndustryGet);
	server.RegisterHandler("industry.nearest", HandleIndustryNearest);
	server.RegisterHandler("map.info", HandleMapInfo);
	server.RegisterHandler("map.distance", HandleMapDistance);
	server.RegisterHandler("map.scan", HandleMapScan);
	server.RegisterHandler("map.terrain", HandleMapTerrain);
	server.RegisterHandler("tile.get", HandleTileGet);
	server.RegisterHandler("town.list", HandleTownList);
	server.RegisterHandler("town.get", HandleTownGet);
	server.RegisterHandler("town.nearest", HandleTownNearest);
	server.RegisterHandler("order.list", HandleOrderList);
	server.RegisterHandler("engine.list", HandleEngineList);
	server.RegisterHandler("engine.get", HandleEngineGet);
	server.RegisterHandler("subsidy.list", HandleSubsidyList);
	server.RegisterHandler("cargo.list", HandleCargoList);
	server.RegisterHandler("cargo.getIncome", HandleCargoGetIncome);
	server.RegisterHandler("cargomonitor.getDelivery", HandleCargoMonitorGetDelivery);
	server.RegisterHandler("cargomonitor.getPickup", HandleCargoMonitorGetPickup);
	server.RegisterHandler("industry.getStockpile", HandleIndustryGetStockpile);
	server.RegisterHandler("industry.getAcceptance", HandleIndustryGetAcceptance);
	server.RegisterHandler("station.getCargoPlanned", HandleStationGetCargoPlanned);
	server.RegisterHandler("station.getCoverage", HandleStationGetCoverage);
	server.RegisterHandler("vehicle.getCargoByType", HandleVehicleGetCargoByType);
	server.RegisterHandler("airport.info", HandleAirportInfo);
	server.RegisterHandler("company.alerts", HandleCompanyAlerts);
	server.RegisterHandler("route.check", HandleRouteCheck);
}
