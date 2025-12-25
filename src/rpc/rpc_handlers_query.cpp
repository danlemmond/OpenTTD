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

static const char *TileTypeToString(TileType type)
{
	switch (type) {
		case MP_CLEAR: return "clear";
		case MP_RAILWAY: return "railway";
		case MP_ROAD: return "road";
		case MP_HOUSE: return "house";
		case MP_TREES: return "trees";
		case MP_INDUSTRY: return "industry";
		case MP_STATION: return "station";
		case MP_WATER: return "water";
		case MP_VOID: return "void";
		case MP_OBJECT: return "object";
		case MP_TUNNELBRIDGE: return "tunnelbridge";
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
	result["type"] = TileTypeToString(GetTileType(tile));
	result["height"] = TileHeight(tile);

	Slope slope = GetTileSlope(tile);
	result["slope"] = static_cast<int>(slope);
	result["is_flat"] = (slope == SLOPE_FLAT);

	Owner owner = GetTileOwner(tile);
	result["owner"] = owner != INVALID_OWNER ? owner.base() : -1;

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
	server.RegisterHandler("map.info", HandleMapInfo);
	server.RegisterHandler("map.distance", HandleMapDistance);
	server.RegisterHandler("map.scan", HandleMapScan);
	server.RegisterHandler("tile.get", HandleTileGet);
	server.RegisterHandler("town.list", HandleTownList);
	server.RegisterHandler("town.get", HandleTownGet);
	server.RegisterHandler("order.list", HandleOrderList);
}
