/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file rpc_handlers_action.cpp JSON-RPC action handlers for vehicle and order control. */

#include "../stdafx.h"
#include "rpc_handlers.h"
#include "../vehicle_base.h"
#include "../vehicle_func.h"
#include "../station_base.h"
#include "../order_base.h"
#include "../command_func.h"
#include "../core/backup_type.hpp"
#include "../vehicle_cmd.h"
#include "../order_cmd.h"
#include "../strings_func.h"
#include "../string_func.h"
#include "../table/strings.h"
#include "../engine_base.h"
#include "../engine_func.h"
#include "../depot_map.h"
#include "../train.h"
#include "../roadveh.h"
#include "../cargotype.h"

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

static nlohmann::json HandleVehicleStartStop(const nlohmann::json &params)
{
	if (!params.contains("vehicle_id")) {
		throw std::runtime_error("Missing required parameter: vehicle_id");
	}

	VehicleID vid = static_cast<VehicleID>(params["vehicle_id"].get<int>());
	const Vehicle *v = Vehicle::GetIfValid(vid);
	if (v == nullptr) {
		throw std::runtime_error("Invalid vehicle ID");
	}

	/* Switch to vehicle owner's company context */
	Backup<CompanyID> cur_company(_current_company, v->owner);

	/* Execute the start/stop command */
	DoCommandFlags flags;
	flags.Set(DoCommandFlag::Execute);
	CommandCost cost = Command<CMD_START_STOP_VEHICLE>::Do(flags, vid, false);

	cur_company.Restore();

	nlohmann::json result;
	result["vehicle_id"] = vid.base();
	result["success"] = cost.Succeeded();

	if (cost.Failed()) {
		result["error"] = "Command failed";
	} else {
		/* Re-fetch vehicle state after command */
		v = Vehicle::GetIfValid(vid);
		if (v != nullptr) {
			result["stopped"] = v->IsStoppedInDepot() || v->vehstatus.Test(VehState::Stopped);
		}
	}

	return result;
}

static nlohmann::json HandleVehicleSendToDepot(const nlohmann::json &params)
{
	if (!params.contains("vehicle_id")) {
		throw std::runtime_error("Missing required parameter: vehicle_id");
	}

	VehicleID vid = static_cast<VehicleID>(params["vehicle_id"].get<int>());
	const Vehicle *v = Vehicle::GetIfValid(vid);
	if (v == nullptr) {
		throw std::runtime_error("Invalid vehicle ID");
	}

	/* Switch to vehicle owner's company context */
	Backup<CompanyID> cur_company(_current_company, v->owner);

	/* Check options */
	bool service_only = params.value("service", false);

	DepotCommandFlags depot_flags;
	if (service_only) {
		depot_flags.Set(DepotCommandFlag::Service);
	}

	/* Empty VehicleListIdentifier for single vehicle */
	VehicleListIdentifier vli;

	DoCommandFlags flags;
	flags.Set(DoCommandFlag::Execute);
	CommandCost cost = Command<CMD_SEND_VEHICLE_TO_DEPOT>::Do(flags, vid, depot_flags, vli);

	cur_company.Restore();

	nlohmann::json result;
	result["vehicle_id"] = vid.base();
	result["success"] = cost.Succeeded();
	result["service_only"] = service_only;

	if (cost.Failed()) {
		result["error"] = "Command failed - vehicle may already be heading to depot or no depot available";
	}

	return result;
}

static nlohmann::json HandleVehicleTurnAround(const nlohmann::json &params)
{
	if (!params.contains("vehicle_id")) {
		throw std::runtime_error("Missing required parameter: vehicle_id");
	}

	VehicleID vid = static_cast<VehicleID>(params["vehicle_id"].get<int>());
	const Vehicle *v = Vehicle::GetIfValid(vid);
	if (v == nullptr) {
		throw std::runtime_error("Invalid vehicle ID");
	}

	/* Switch to vehicle owner's company context */
	Backup<CompanyID> cur_company(_current_company, v->owner);

	/* Cancel depot order by sending to depot with no flags, then immediately canceling */
	DepotCommandFlags depot_flags;

	VehicleListIdentifier vli;

	DoCommandFlags flags;
	flags.Set(DoCommandFlag::Execute);

	/* Sending a vehicle to depot while already heading there cancels the depot order */
	CommandCost cost = Command<CMD_SEND_VEHICLE_TO_DEPOT>::Do(flags, vid, depot_flags, vli);

	cur_company.Restore();

	nlohmann::json result;
	result["vehicle_id"] = vid.base();
	result["success"] = cost.Succeeded();

	return result;
}

static nlohmann::json HandleOrderAppend(const nlohmann::json &params)
{
	if (!params.contains("vehicle_id")) {
		throw std::runtime_error("Missing required parameter: vehicle_id");
	}
	if (!params.contains("destination")) {
		throw std::runtime_error("Missing required parameter: destination (station_id)");
	}

	VehicleID vid = static_cast<VehicleID>(params["vehicle_id"].get<int>());
	const Vehicle *v = Vehicle::GetIfValid(vid);
	if (v == nullptr || !v->IsPrimaryVehicle()) {
		throw std::runtime_error("Invalid vehicle ID");
	}

	/* Get order parameters */
	StationID dest_station = static_cast<StationID>(params["destination"].get<int>());
	const Station *st = Station::GetIfValid(dest_station);
	if (st == nullptr) {
		throw std::runtime_error("Invalid destination station ID");
	}

	/* Check if vehicle can use this station */
	if (!CanVehicleUseStation(v, st)) {
		throw std::runtime_error("Vehicle cannot use this station (incompatible facilities or road type)");
	}

	/* Switch to vehicle owner's company context */
	Backup<CompanyID> cur_company(_current_company, v->owner);

	/* Build the order */
	Order order;
	order.MakeGoToStation(dest_station);
	order.SetStopLocation(OrderStopLocation::FarEnd);  /* Required for non-train vehicles */

	/* Set load/unload flags if specified */
	std::string load_type = params.value("load", "default");
	std::string unload_type = params.value("unload", "default");

	if (load_type == "full") {
		order.SetLoadType(OrderLoadType::FullLoad);
	} else if (load_type == "full_any") {
		order.SetLoadType(OrderLoadType::FullLoadAny);
	} else if (load_type == "none") {
		order.SetLoadType(OrderLoadType::NoLoad);
	}

	if (unload_type == "unload") {
		order.SetUnloadType(OrderUnloadType::Unload);
	} else if (unload_type == "transfer") {
		order.SetUnloadType(OrderUnloadType::Transfer);
	} else if (unload_type == "none") {
		order.SetUnloadType(OrderUnloadType::NoUnload);
	}

	/* Non-stop flag */
	if (params.value("non_stop", false)) {
		OrderNonStopFlags nsf;
		nsf.Set(OrderNonStopFlag::NoIntermediate);
		order.SetNonStopType(nsf);
	}

	/* Append order at end */
	VehicleOrderID insert_pos = (v->orders != nullptr) ? v->orders->GetNumOrders() : 0;

	DoCommandFlags flags;
	flags.Set(DoCommandFlag::Execute);
	CommandCost cost = Command<CMD_INSERT_ORDER>::Do(flags, vid, insert_pos, order);

	cur_company.Restore();

	nlohmann::json result;
	result["vehicle_id"] = vid.base();
	result["success"] = cost.Succeeded();
	result["order_index"] = insert_pos;
	result["destination"] = dest_station.base();
	result["destination_name"] = StrMakeValid(st->GetCachedName());

	if (cost.Failed()) {
		result["error"] = "Failed to append order";
	}

	return result;
}

static nlohmann::json HandleOrderRemove(const nlohmann::json &params)
{
	if (!params.contains("vehicle_id")) {
		throw std::runtime_error("Missing required parameter: vehicle_id");
	}
	if (!params.contains("order_index")) {
		throw std::runtime_error("Missing required parameter: order_index");
	}

	VehicleID vid = static_cast<VehicleID>(params["vehicle_id"].get<int>());
	VehicleOrderID order_idx = static_cast<VehicleOrderID>(params["order_index"].get<int>());

	const Vehicle *v = Vehicle::GetIfValid(vid);
	if (v == nullptr || !v->IsPrimaryVehicle()) {
		throw std::runtime_error("Invalid vehicle ID");
	}

	if (v->orders == nullptr || order_idx >= v->orders->GetNumOrders()) {
		throw std::runtime_error("Invalid order index");
	}

	/* Switch to vehicle owner's company context */
	Backup<CompanyID> cur_company(_current_company, v->owner);

	DoCommandFlags flags;
	flags.Set(DoCommandFlag::Execute);
	CommandCost cost = Command<CMD_DELETE_ORDER>::Do(flags, vid, order_idx);

	cur_company.Restore();

	nlohmann::json result;
	result["vehicle_id"] = vid.base();
	result["success"] = cost.Succeeded();
	result["removed_index"] = order_idx;

	if (cost.Failed()) {
		result["error"] = "Failed to remove order";
	}

	return result;
}

/**
 * Handler for vehicle.build - Build a new vehicle at a depot.
 *
 * Parameters:
 *   depot_tile or depot_x/depot_y: The depot location (required)
 *   engine_id: The engine ID to build (required)
 *   company: Company ID (default: 0)
 *   cargo: Cargo type to refit to (optional)
 *
 * Returns:
 *   vehicle_id: The ID of the newly built vehicle
 *   success: Whether the build succeeded
 *   cost: The cost of the vehicle
 */
static nlohmann::json HandleVehicleBuild(const nlohmann::json &params)
{
	/* Get depot tile */
	TileIndex depot_tile;
	if (params.contains("depot_tile")) {
		depot_tile = static_cast<TileIndex>(params["depot_tile"].get<uint32_t>());
	} else if (params.contains("depot_x") && params.contains("depot_y")) {
		uint x = params["depot_x"].get<uint>();
		uint y = params["depot_y"].get<uint>();
		if (x >= Map::SizeX() || y >= Map::SizeY()) {
			throw std::runtime_error("Depot coordinates out of bounds");
		}
		depot_tile = TileXY(x, y);
	} else {
		throw std::runtime_error("Missing required parameter: depot_tile or depot_x/depot_y");
	}

	/* Validate depot tile */
	if (!IsDepotTile(depot_tile)) {
		throw std::runtime_error("Specified tile is not a depot");
	}

	/* Get engine ID */
	if (!params.contains("engine_id")) {
		throw std::runtime_error("Missing required parameter: engine_id");
	}
	EngineID engine_id = static_cast<EngineID>(params["engine_id"].get<int>());

	const Engine *e = Engine::GetIfValid(engine_id);
	if (e == nullptr || !e->IsEnabled()) {
		throw std::runtime_error("Invalid or unavailable engine ID");
	}

	/* Verify engine type matches depot type */
	VehicleType depot_veh_type = GetDepotVehicleType(depot_tile);
	if (e->type != depot_veh_type) {
		throw std::runtime_error("Engine type does not match depot type");
	}

	/* Get company */
	CompanyID company = static_cast<CompanyID>(params.value("company", 0));
	if (!Company::IsValidID(company)) {
		throw std::runtime_error("Invalid company ID");
	}

	/* Check if engine is buildable for this company */
	if (!IsEngineBuildable(engine_id, e->type, company)) {
		throw std::runtime_error("Engine is not available for this company");
	}

	/* Optional cargo refit */
	CargoType cargo = INVALID_CARGO;
	if (params.contains("cargo")) {
		cargo = static_cast<CargoType>(params["cargo"].get<int>());
		if (!IsValidCargoType(cargo)) {
			throw std::runtime_error("Invalid cargo type");
		}
	}

	/* Switch to company context */
	Backup<CompanyID> cur_company(_current_company, company);

	DoCommandFlags flags;
	flags.Set(DoCommandFlag::Execute);

	auto [cost, new_veh_id, refit_capacity, refit_mail, cargo_capacities] =
		Command<CMD_BUILD_VEHICLE>::Do(flags, depot_tile, engine_id, true, cargo, INVALID_CLIENT_ID);

	cur_company.Restore();

	nlohmann::json result;
	result["success"] = cost.Succeeded();

	if (cost.Succeeded()) {
		result["vehicle_id"] = new_veh_id.base();
		result["cost"] = cost.GetCost().base();
		result["engine_id"] = engine_id.base();
		result["engine_name"] = StrMakeValid(GetString(STR_ENGINE_NAME, engine_id));
		result["vehicle_type"] = VehicleTypeToString(e->type);

		/* Fetch the newly created vehicle for more details */
		const Vehicle *v = Vehicle::GetIfValid(new_veh_id);
		if (v != nullptr) {
			result["stopped"] = v->IsStoppedInDepot() || v->vehstatus.Test(VehState::Stopped);
		}
	} else {
		result["error"] = "Failed to build vehicle - check depot, engine, and funds";
	}

	return result;
}

/**
 * Handler for vehicle.sell - Sell a vehicle.
 *
 * Parameters:
 *   vehicle_id: The vehicle ID to sell (required)
 *   sell_chain: For trains, whether to sell entire train (default: true)
 *
 * Note: Vehicle must be stopped in a depot to be sold.
 *
 * Returns:
 *   success: Whether the sale succeeded
 *   value: The sale value received
 */
static nlohmann::json HandleVehicleSell(const nlohmann::json &params)
{
	if (!params.contains("vehicle_id")) {
		throw std::runtime_error("Missing required parameter: vehicle_id");
	}

	VehicleID vid = static_cast<VehicleID>(params["vehicle_id"].get<int>());
	const Vehicle *v = Vehicle::GetIfValid(vid);
	if (v == nullptr) {
		throw std::runtime_error("Invalid vehicle ID");
	}

	/* For the user's benefit, explain why it might fail */
	if (!v->IsStoppedInDepot()) {
		throw std::runtime_error("Vehicle must be stopped in a depot to be sold");
	}

	/* For trains, sell_chain determines if we sell the whole train */
	bool sell_chain = params.value("sell_chain", true);

	/* Switch to vehicle owner's company context */
	Backup<CompanyID> cur_company(_current_company, v->owner);

	/* Get estimated value before selling */
	[[maybe_unused]] Money estimated_value = v->value;

	DoCommandFlags flags;
	flags.Set(DoCommandFlag::Execute);
	CommandCost cost = Command<CMD_SELL_VEHICLE>::Do(flags, vid, sell_chain, false, INVALID_CLIENT_ID);

	cur_company.Restore();

	nlohmann::json result;
	result["vehicle_id"] = vid.base();
	result["success"] = cost.Succeeded();

	if (cost.Succeeded()) {
		/* The "cost" of selling is negative (we receive money) */
		Money value = -cost.GetCost();
		result["value"] = value < 0 ? 0 : value.base();
	} else {
		result["error"] = "Failed to sell vehicle - ensure it is stopped in a depot";
	}

	return result;
}

/**
 * Handler for vehicle.clone - Clone an existing vehicle including orders.
 *
 * Parameters:
 *   vehicle_id: The vehicle ID to clone (required)
 *   depot_tile or depot_x/depot_y: The depot to build in (required)
 *   share_orders: Whether to share orders with original (default: false)
 *
 * Returns:
 *   vehicle_id: The ID of the newly cloned vehicle
 *   success: Whether the clone succeeded
 *   cost: The cost of the cloned vehicle
 */
static nlohmann::json HandleVehicleClone(const nlohmann::json &params)
{
	if (!params.contains("vehicle_id")) {
		throw std::runtime_error("Missing required parameter: vehicle_id");
	}

	VehicleID source_vid = static_cast<VehicleID>(params["vehicle_id"].get<int>());
	const Vehicle *source_v = Vehicle::GetIfValid(source_vid);
	if (source_v == nullptr || !source_v->IsPrimaryVehicle()) {
		throw std::runtime_error("Invalid vehicle ID");
	}

	/* Get depot tile */
	TileIndex depot_tile;
	if (params.contains("depot_tile")) {
		depot_tile = static_cast<TileIndex>(params["depot_tile"].get<uint32_t>());
	} else if (params.contains("depot_x") && params.contains("depot_y")) {
		uint x = params["depot_x"].get<uint>();
		uint y = params["depot_y"].get<uint>();
		if (x >= Map::SizeX() || y >= Map::SizeY()) {
			throw std::runtime_error("Depot coordinates out of bounds");
		}
		depot_tile = TileXY(x, y);
	} else {
		throw std::runtime_error("Missing required parameter: depot_tile or depot_x/depot_y");
	}

	/* Validate depot tile */
	if (!IsDepotTile(depot_tile)) {
		throw std::runtime_error("Specified tile is not a depot");
	}

	/* Verify vehicle type matches depot type */
	VehicleType depot_veh_type = GetDepotVehicleType(depot_tile);
	if (source_v->type != depot_veh_type) {
		throw std::runtime_error("Vehicle type does not match depot type");
	}

	bool share_orders = params.value("share_orders", false);

	/* Switch to vehicle owner's company context */
	Backup<CompanyID> cur_company(_current_company, source_v->owner);

	DoCommandFlags flags;
	flags.Set(DoCommandFlag::Execute);

	auto [cost, new_veh_id] = Command<CMD_CLONE_VEHICLE>::Do(flags, depot_tile, source_vid, share_orders);

	cur_company.Restore();

	nlohmann::json result;
	result["success"] = cost.Succeeded();
	result["source_vehicle_id"] = source_vid.base();

	if (cost.Succeeded()) {
		result["vehicle_id"] = new_veh_id.base();
		result["cost"] = cost.GetCost().base();
		result["share_orders"] = share_orders;

		/* Fetch the newly cloned vehicle */
		const Vehicle *v = Vehicle::GetIfValid(new_veh_id);
		if (v != nullptr) {
			result["vehicle_name"] = StrMakeValid(GetString(STR_VEHICLE_NAME, v->index));
		}
	} else {
		result["error"] = "Failed to clone vehicle - check depot, funds, and vehicle availability";
	}

	return result;
}

void RpcRegisterActionHandlers(RpcServer &server)
{
	server.RegisterHandler("vehicle.startstop", HandleVehicleStartStop);
	server.RegisterHandler("vehicle.depot", HandleVehicleSendToDepot);
	server.RegisterHandler("vehicle.turnaround", HandleVehicleTurnAround);
	server.RegisterHandler("vehicle.build", HandleVehicleBuild);
	server.RegisterHandler("vehicle.sell", HandleVehicleSell);
	server.RegisterHandler("vehicle.clone", HandleVehicleClone);
	server.RegisterHandler("order.append", HandleOrderAppend);
	server.RegisterHandler("order.remove", HandleOrderRemove);
}
