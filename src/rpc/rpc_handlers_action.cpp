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
#include "../train_cmd.h"
#include "../roadveh.h"
#include "../cargotype.h"
#include "../company_base.h"
#include "../misc_cmd.h"
#include "../town.h"
#include "../town_cmd.h"
#include "../station_cmd.h"
#include "../rail_cmd.h"
#include "../road_cmd.h"
#include "../landscape_cmd.h"
#include "../station_map.h"
#include "../rail_map.h"
#include "../road_map.h"
#include "../tile_map.h"

#include "../safeguards.h"

/**
 * Extract error message from a failed CommandCost.
 * @param cost The CommandCost to extract the error from.
 * @return Human-readable error message string.
 */
static std::string GetCommandErrorMessage(const CommandCost &cost)
{
	if (cost.Succeeded()) return "";

	StringID msg = cost.GetErrorMessage();
	if (msg == INVALID_STRING_ID) {
		return "Unknown error";
	}

	std::string error = StrMakeValid(GetString(msg));

	/* Check for extra message */
	StringID extra = cost.GetExtraErrorMessage();
	if (extra != INVALID_STRING_ID) {
		error += ": " + StrMakeValid(GetString(extra));
	}

	return error;
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
		result["error"] = GetCommandErrorMessage(cost);
	} else {
		/* Re-fetch vehicle state after command */
		v = Vehicle::GetIfValid(vid);
		if (v != nullptr) {
			/* Use First() since IsStoppedInDepot() requires primary vehicle */
			result["stopped"] = v->First()->IsStoppedInDepot() || v->vehstatus.Test(VehState::Stopped);
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
		result["error"] = GetCommandErrorMessage(cost);
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
		result["error"] = GetCommandErrorMessage(cost);
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
		result["error"] = GetCommandErrorMessage(cost);
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

	if (cost.Succeeded()) {
		RpcRecordActivity(depot_tile, "vehicle.build");
	}

	nlohmann::json result;
	result["success"] = cost.Succeeded();

	if (cost.Succeeded()) {
		result["vehicle_id"] = new_veh_id.base();
		result["cost"] = cost.GetCost().base();
		result["engine_id"] = engine_id.base();
		result["engine_name"] = StrMakeValid(GetString(STR_ENGINE_NAME, engine_id));
		result["vehicle_type"] = RpcVehicleTypeToString(e->type);

		/* Fetch the newly created vehicle for more details */
		const Vehicle *v = Vehicle::GetIfValid(new_veh_id);
		if (v != nullptr) {
			/* Use First() since IsStoppedInDepot() requires primary vehicle (wagons aren't primary) */
			result["stopped"] = v->First()->IsStoppedInDepot() || v->vehstatus.Test(VehState::Stopped);
		}
	} else {
		result["error"] = GetCommandErrorMessage(cost);
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
	/* Use First() since IsStoppedInDepot() requires primary vehicle */
	if (!v->First()->IsStoppedInDepot()) {
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
		result["error"] = GetCommandErrorMessage(cost);
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

	if (cost.Succeeded()) {
		RpcRecordActivity(depot_tile, "vehicle.clone");
	}

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
		result["error"] = GetCommandErrorMessage(cost);
	}

	return result;
}

/**
 * Handler for company.setLoan - Set the company's loan amount.
 *
 * Parameters:
 *   amount: Target loan amount (required)
 *   company: Company ID (default: 0)
 *
 * Returns:
 *   success: Whether the loan change succeeded
 *   new_loan: The new loan amount
 *   old_loan: The previous loan amount
 */
static nlohmann::json HandleCompanySetLoan(const nlohmann::json &params)
{
	if (!params.contains("amount")) {
		throw std::runtime_error("Missing required parameter: amount");
	}

	Money target_amount = static_cast<Money>(params["amount"].get<int64_t>());
	if (target_amount < 0) {
		throw std::runtime_error("Loan amount must be non-negative");
	}

	CompanyID company = static_cast<CompanyID>(params.value("company", 0));
	if (!Company::IsValidID(company)) {
		throw std::runtime_error("Invalid company ID");
	}

	const Company *c = Company::Get(company);
	Money old_loan = c->current_loan;

	/* Switch to company context */
	Backup<CompanyID> cur_company(_current_company, company);

	DoCommandFlags flags;
	flags.Set(DoCommandFlag::Execute);

	CommandCost cost;
	if (target_amount > old_loan) {
		/* Need to increase loan */
		Money increase = target_amount - old_loan;
		cost = Command<CMD_INCREASE_LOAN>::Do(flags, LoanCommand::Amount, increase);
	} else if (target_amount < old_loan) {
		/* Need to decrease loan */
		Money decrease = old_loan - target_amount;
		cost = Command<CMD_DECREASE_LOAN>::Do(flags, LoanCommand::Amount, decrease);
	}
	/* If target == old_loan, nothing to do, cost remains default (success) */

	cur_company.Restore();

	/* Re-fetch company for updated loan amount */
	c = Company::Get(company);

	nlohmann::json result;
	result["success"] = cost.Succeeded();
	result["company"] = company.base();
	result["old_loan"] = old_loan.base();
	result["new_loan"] = c->current_loan.base();
	result["max_loan"] = _economy.max_loan.base();

	if (cost.Failed()) {
		result["error"] = GetCommandErrorMessage(cost);
	}

	return result;
}

/**
 * Handler for vehicle.refit - Refit a vehicle to a different cargo type.
 *
 * Parameters:
 *   vehicle_id: The vehicle ID to refit (required)
 *   cargo: The cargo type ID to refit to (required)
 *
 * Note: Vehicle must be stopped in a depot to be refitted.
 *
 * Returns:
 *   success: Whether the refit succeeded
 *   capacity: New cargo capacity after refit
 *   cargo_name: Name of the new cargo type
 */
static nlohmann::json HandleVehicleRefit(const nlohmann::json &params)
{
	if (!params.contains("vehicle_id")) {
		throw std::runtime_error("Missing required parameter: vehicle_id");
	}
	if (!params.contains("cargo")) {
		throw std::runtime_error("Missing required parameter: cargo");
	}

	VehicleID vid = static_cast<VehicleID>(params["vehicle_id"].get<int>());
	const Vehicle *v = Vehicle::GetIfValid(vid);
	if (v == nullptr) {
		throw std::runtime_error("Invalid vehicle ID");
	}

	CargoType cargo = static_cast<CargoType>(params["cargo"].get<int>());
	if (!IsValidCargoType(cargo)) {
		throw std::runtime_error("Invalid cargo type");
	}

	/* Vehicle should be in depot for refit */
	/* Use First() since IsStoppedInDepot() requires primary vehicle */
	if (!v->First()->IsStoppedInDepot()) {
		throw std::runtime_error("Vehicle must be stopped in a depot to be refitted");
	}

	/* Switch to vehicle owner's company context */
	Backup<CompanyID> cur_company(_current_company, v->owner);

	DoCommandFlags flags;
	flags.Set(DoCommandFlag::Execute);

	/* Refit the vehicle - auto_refit=false, only_this=false (refit whole chain for trains), num_vehicles=255 (all) */
	auto [cost, capacity, mail_capacity, cargo_capacities] =
		Command<CMD_REFIT_VEHICLE>::Do(flags, vid, cargo, 0, false, false, 255);

	cur_company.Restore();

	nlohmann::json result;
	result["vehicle_id"] = vid.base();
	result["success"] = cost.Succeeded();
	result["cargo"] = cargo;

	const CargoSpec *cs = CargoSpec::Get(cargo);
	if (cs != nullptr) {
		result["cargo_name"] = StrMakeValid(GetString(cs->name));
	}

	if (cost.Succeeded()) {
		result["capacity"] = capacity;
		result["cost"] = cost.GetCost().base();
	} else {
		result["error"] = GetCommandErrorMessage(cost);
	}

	return result;
}

/**
 * Handler for vehicle.attach - Attach a wagon to a train.
 *
 * Parameters:
 *   wagon_id: The wagon to attach (required)
 *   train_id: The train to attach to (required, use the locomotive ID)
 *   move_chain: Whether to move the entire chain (default: true)
 *   company: Company ID (default: 0)
 *
 * Returns:
 *   success: Whether the attach succeeded
 *   wagon_id: The wagon that was attached
 *   train_id: The train it was attached to
 */
static nlohmann::json HandleVehicleAttach(const nlohmann::json &params)
{
	if (!params.contains("wagon_id")) {
		throw std::runtime_error("Missing required parameter: wagon_id");
	}
	if (!params.contains("train_id")) {
		throw std::runtime_error("Missing required parameter: train_id");
	}

	VehicleID wagon_id = static_cast<VehicleID>(params["wagon_id"].get<int>());
	VehicleID train_id = static_cast<VehicleID>(params["train_id"].get<int>());
	bool move_chain = params.value("move_chain", true);

	/* Get the wagon */
	const Vehicle *wagon = Vehicle::GetIfValid(wagon_id);
	if (wagon == nullptr) {
		throw std::runtime_error("Invalid wagon ID");
	}
	if (wagon->type != VEH_TRAIN) {
		throw std::runtime_error("Vehicle is not a train/wagon");
	}

	/* Get the train (locomotive) */
	const Vehicle *train = Vehicle::GetIfValid(train_id);
	if (train == nullptr) {
		throw std::runtime_error("Invalid train ID");
	}
	if (train->type != VEH_TRAIN) {
		throw std::runtime_error("Target vehicle is not a train");
	}

	/* Switch to company context */
	CompanyID company = static_cast<CompanyID>(params.value("company", 0));
	if (!Company::IsValidID(company)) {
		throw std::runtime_error("Invalid company ID");
	}

	/* Verify ownership */
	if (wagon->owner != company || train->owner != company) {
		throw std::runtime_error("Vehicles are not owned by specified company");
	}

	Backup<CompanyID> cur_company(_current_company, company);

	DoCommandFlags flags;
	flags.Set(DoCommandFlag::Execute);

	/* Use CMD_MOVE_RAIL_VEHICLE to attach wagon to train
	 * src_veh = wagon to move
	 * dest_veh = vehicle to attach to (the last wagon of the train)
	 */
	const Train *t = Train::From(train);
	VehicleID dest_id = t->Last()->index;

	CommandCost cost = Command<CMD_MOVE_RAIL_VEHICLE>::Do(flags, wagon_id, dest_id, move_chain);

	cur_company.Restore();

	nlohmann::json result;
	result["wagon_id"] = wagon_id.base();
	result["train_id"] = train_id.base();
	result["success"] = cost.Succeeded();

	if (cost.Failed()) {
		result["error"] = GetCommandErrorMessage(cost);
	}

	return result;
}

/**
 * Handler for order.insert - Insert an order at a specific position.
 *
 * Parameters:
 *   vehicle_id: The vehicle ID (required)
 *   order_index: Position to insert at (required)
 *   destination: Station ID (required)
 *   load: Load type (default, full, full_any, none)
 *   unload: Unload type (default, unload, transfer, none)
 *   non_stop: Whether to skip intermediate stations
 *
 * Returns:
 *   success: Whether the insert succeeded
 *   order_index: The position where order was inserted
 */
static nlohmann::json HandleOrderInsert(const nlohmann::json &params)
{
	if (!params.contains("vehicle_id")) {
		throw std::runtime_error("Missing required parameter: vehicle_id");
	}
	if (!params.contains("order_index")) {
		throw std::runtime_error("Missing required parameter: order_index");
	}
	if (!params.contains("destination")) {
		throw std::runtime_error("Missing required parameter: destination (station_id)");
	}

	VehicleID vid = static_cast<VehicleID>(params["vehicle_id"].get<int>());
	VehicleOrderID insert_pos = static_cast<VehicleOrderID>(params["order_index"].get<int>());

	const Vehicle *v = Vehicle::GetIfValid(vid);
	if (v == nullptr || !v->IsPrimaryVehicle()) {
		throw std::runtime_error("Invalid vehicle ID");
	}

	StationID dest_station = static_cast<StationID>(params["destination"].get<int>());
	const Station *st = Station::GetIfValid(dest_station);
	if (st == nullptr) {
		throw std::runtime_error("Invalid destination station ID");
	}

	if (!CanVehicleUseStation(v, st)) {
		throw std::runtime_error("Vehicle cannot use this station");
	}

	/* Switch to vehicle owner's company context */
	Backup<CompanyID> cur_company(_current_company, v->owner);

	/* Build the order */
	Order order;
	order.MakeGoToStation(dest_station);
	order.SetStopLocation(OrderStopLocation::FarEnd);

	/* Set load/unload flags */
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

	if (params.value("non_stop", false)) {
		OrderNonStopFlags nsf;
		nsf.Set(OrderNonStopFlag::NoIntermediate);
		order.SetNonStopType(nsf);
	}

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
		result["error"] = GetCommandErrorMessage(cost);
	}

	return result;
}

/**
 * Handler for order.setFlags - Modify an existing order's flags.
 *
 * Parameters:
 *   vehicle_id: The vehicle ID (required)
 *   order_index: Order position to modify (required)
 *   load: Load type (default, full, full_any, none)
 *   unload: Unload type (default, unload, transfer, none)
 *   non_stop: Non-stop flag (true/false)
 *
 * Returns:
 *   success: Whether the modification succeeded
 */
static nlohmann::json HandleOrderSetFlags(const nlohmann::json &params)
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

	bool any_succeeded = false;
	bool any_failed = false;

	/* Apply load type if specified */
	if (params.contains("load")) {
		std::string load_type = params["load"].get<std::string>();
		uint16_t load_val = 0;
		if (load_type == "default") load_val = static_cast<uint16_t>(OrderLoadType::LoadIfPossible);
		else if (load_type == "full") load_val = static_cast<uint16_t>(OrderLoadType::FullLoad);
		else if (load_type == "full_any") load_val = static_cast<uint16_t>(OrderLoadType::FullLoadAny);
		else if (load_type == "none") load_val = static_cast<uint16_t>(OrderLoadType::NoLoad);

		CommandCost cost = Command<CMD_MODIFY_ORDER>::Do(flags, vid, order_idx, MOF_LOAD, load_val);
		if (cost.Succeeded()) any_succeeded = true;
		else any_failed = true;
	}

	/* Apply unload type if specified */
	if (params.contains("unload")) {
		std::string unload_type = params["unload"].get<std::string>();
		uint16_t unload_val = 0;
		if (unload_type == "default") unload_val = static_cast<uint16_t>(OrderUnloadType::UnloadIfPossible);
		else if (unload_type == "unload") unload_val = static_cast<uint16_t>(OrderUnloadType::Unload);
		else if (unload_type == "transfer") unload_val = static_cast<uint16_t>(OrderUnloadType::Transfer);
		else if (unload_type == "none") unload_val = static_cast<uint16_t>(OrderUnloadType::NoUnload);

		CommandCost cost = Command<CMD_MODIFY_ORDER>::Do(flags, vid, order_idx, MOF_UNLOAD, unload_val);
		if (cost.Succeeded()) any_succeeded = true;
		else any_failed = true;
	}

	/* Apply non-stop if specified */
	if (params.contains("non_stop")) {
		bool non_stop = params["non_stop"].get<bool>();
		uint16_t ns_val = non_stop ? static_cast<uint16_t>(OrderNonStopFlag::NoIntermediate) : 0;

		CommandCost cost = Command<CMD_MODIFY_ORDER>::Do(flags, vid, order_idx, MOF_NON_STOP, ns_val);
		if (cost.Succeeded()) any_succeeded = true;
		else any_failed = true;
	}

	cur_company.Restore();

	nlohmann::json result;
	result["vehicle_id"] = vid.base();
	result["order_index"] = order_idx;
	result["success"] = any_succeeded && !any_failed;
	result["partial_success"] = any_succeeded && any_failed;

	if (any_failed && !any_succeeded) {
		result["error"] = "Failed to modify order flags";  /* No single CommandCost to extract from */
	}

	return result;
}

/**
 * Handler for order.share - Share orders between vehicles.
 *
 * Parameters:
 *   vehicle_id: The vehicle to copy orders TO (required)
 *   source_vehicle_id: The vehicle to copy orders FROM (required)
 *   mode: "share" (default), "copy", or "unshare"
 *
 * Returns:
 *   success: Whether the operation succeeded
 */
static nlohmann::json HandleOrderShare(const nlohmann::json &params)
{
	if (!params.contains("vehicle_id")) {
		throw std::runtime_error("Missing required parameter: vehicle_id");
	}
	if (!params.contains("source_vehicle_id")) {
		throw std::runtime_error("Missing required parameter: source_vehicle_id");
	}

	VehicleID dest_vid = static_cast<VehicleID>(params["vehicle_id"].get<int>());
	VehicleID src_vid = static_cast<VehicleID>(params["source_vehicle_id"].get<int>());

	const Vehicle *dest_v = Vehicle::GetIfValid(dest_vid);
	const Vehicle *src_v = Vehicle::GetIfValid(src_vid);

	if (dest_v == nullptr || !dest_v->IsPrimaryVehicle()) {
		throw std::runtime_error("Invalid destination vehicle ID");
	}
	if (src_v == nullptr || !src_v->IsPrimaryVehicle()) {
		throw std::runtime_error("Invalid source vehicle ID");
	}

	if (dest_v->type != src_v->type) {
		throw std::runtime_error("Vehicles must be of the same type to share orders");
	}

	std::string mode = params.value("mode", "share");
	CloneOptions clone_opt;
	if (mode == "share") {
		clone_opt = CO_SHARE;
	} else if (mode == "copy") {
		clone_opt = CO_COPY;
	} else if (mode == "unshare") {
		clone_opt = CO_UNSHARE;
	} else {
		throw std::runtime_error("Invalid mode - must be 'share', 'copy', or 'unshare'");
	}

	/* Switch to destination vehicle owner's company context */
	Backup<CompanyID> cur_company(_current_company, dest_v->owner);

	DoCommandFlags flags;
	flags.Set(DoCommandFlag::Execute);
	CommandCost cost = Command<CMD_CLONE_ORDER>::Do(flags, clone_opt, dest_vid, src_vid);

	cur_company.Restore();

	nlohmann::json result;
	result["vehicle_id"] = dest_vid.base();
	result["source_vehicle_id"] = src_vid.base();
	result["mode"] = mode;
	result["success"] = cost.Succeeded();

	if (cost.Failed()) {
		result["error"] = GetCommandErrorMessage(cost);
	}

	return result;
}

/**
 * Handler for town.performAction - Perform a town action.
 *
 * Parameters:
 *   town_id: The town ID (required)
 *   action: Action name (required) - one of:
 *           advertise_small, advertise_medium, advertise_large,
 *           road_rebuild, build_statue, fund_buildings, buy_rights, bribe
 *   company: Company ID (default: 0)
 *
 * Returns:
 *   success: Whether the action succeeded
 *   cost: The cost of the action
 */
static nlohmann::json HandleTownPerformAction(const nlohmann::json &params)
{
	if (!params.contains("town_id")) {
		throw std::runtime_error("Missing required parameter: town_id");
	}
	if (!params.contains("action")) {
		throw std::runtime_error("Missing required parameter: action");
	}

	TownID town_id = static_cast<TownID>(params["town_id"].get<int>());
	const Town *t = Town::GetIfValid(town_id);
	if (t == nullptr) {
		throw std::runtime_error("Invalid town ID");
	}

	std::string action_str = params["action"].get<std::string>();
	TownAction action;

	if (action_str == "advertise_small") {
		action = TownAction::AdvertiseSmall;
	} else if (action_str == "advertise_medium") {
		action = TownAction::AdvertiseMedium;
	} else if (action_str == "advertise_large") {
		action = TownAction::AdvertiseLarge;
	} else if (action_str == "road_rebuild") {
		action = TownAction::RoadRebuild;
	} else if (action_str == "build_statue") {
		action = TownAction::BuildStatue;
	} else if (action_str == "fund_buildings") {
		action = TownAction::FundBuildings;
	} else if (action_str == "buy_rights") {
		action = TownAction::BuyRights;
	} else if (action_str == "bribe") {
		action = TownAction::Bribe;
	} else {
		throw std::runtime_error("Invalid action - must be one of: advertise_small, advertise_medium, advertise_large, road_rebuild, build_statue, fund_buildings, buy_rights, bribe");
	}

	CompanyID company = static_cast<CompanyID>(params.value("company", 0));
	if (!Company::IsValidID(company)) {
		throw std::runtime_error("Invalid company ID");
	}

	/* Switch to company context */
	Backup<CompanyID> cur_company(_current_company, company);

	DoCommandFlags flags;
	flags.Set(DoCommandFlag::Execute);
	CommandCost cost = Command<CMD_DO_TOWN_ACTION>::Do(flags, town_id, action);

	cur_company.Restore();

	nlohmann::json result;
	result["town_id"] = town_id.base();
	result["town_name"] = StrMakeValid(t->GetCachedName());
	result["action"] = action_str;
	result["success"] = cost.Succeeded();

	if (cost.Succeeded()) {
		result["cost"] = cost.GetCost().base();
	} else {
		result["error"] = GetCommandErrorMessage(cost);
	}

	return result;
}

/**
 * Handler for station.remove - Remove a station tile or road stop.
 *
 * Parameters:
 *   tile or x/y: The tile to remove (required)
 *   company: Company ID (default: 0)
 *   keep_rail: For rail stations, whether to keep the rail (default: false)
 *   remove_road: For road stops, whether to remove the road too (default: false)
 *
 * Returns:
 *   success: Whether the removal succeeded
 */
static nlohmann::json HandleStationRemove(const nlohmann::json &params)
{
	/* Get tile */
	TileIndex tile;
	if (params.contains("tile")) {
		tile = static_cast<TileIndex>(params["tile"].get<uint32_t>());
	} else if (params.contains("x") && params.contains("y")) {
		uint x = params["x"].get<uint>();
		uint y = params["y"].get<uint>();
		if (x >= Map::SizeX() || y >= Map::SizeY()) {
			throw std::runtime_error("Tile coordinates out of bounds");
		}
		tile = TileXY(x, y);
	} else {
		throw std::runtime_error("Missing required parameter: tile or x/y");
	}

	/* Check tile type */
	if (!IsTileType(tile, MP_STATION)) {
		throw std::runtime_error("Specified tile is not a station");
	}

	/* Get company */
	CompanyID company = static_cast<CompanyID>(params.value("company", 0));
	if (!Company::IsValidID(company)) {
		throw std::runtime_error("Invalid company ID");
	}

	/* Check ownership */
	if (GetTileOwner(tile) != company) {
		throw std::runtime_error("Station is not owned by specified company");
	}

	/* Switch to company context */
	Backup<CompanyID> cur_company(_current_company, company);

	DoCommandFlags flags;
	flags.Set(DoCommandFlag::Execute);

	CommandCost cost;
	bool is_rail_station = IsRailStationTile(tile);
	bool is_road_stop = IsStationRoadStopTile(tile);

	if (is_rail_station) {
		/* Remove rail station tile - use same tile for start/end to remove single tile */
		bool keep_rail = params.value("keep_rail", false);
		cost = Command<CMD_REMOVE_FROM_RAIL_STATION>::Do(flags, tile, tile, keep_rail);
	} else if (is_road_stop) {
		/* Remove road stop */
		RoadStopType stop_type = GetRoadStopType(tile);
		bool remove_road = params.value("remove_road", false);
		cost = Command<CMD_REMOVE_ROAD_STOP>::Do(flags, tile, 1, 1, stop_type, remove_road);
	} else {
		cur_company.Restore();
		throw std::runtime_error("Unsupported station type for removal - use bulldoze for airports/docks");
	}

	cur_company.Restore();

	nlohmann::json result;
	result["tile"] = tile != INVALID_TILE ? tile.base() : 0;
	result["success"] = cost.Succeeded();
	result["station_type"] = is_rail_station ? "rail" : (is_road_stop ? "road_stop" : "other");

	if (cost.Succeeded()) {
		RpcRecordActivity(tile, "station.remove");
	} else {
		result["error"] = GetCommandErrorMessage(cost);
	}

	return result;
}

/**
 * Handler for depot.remove - Remove a depot.
 *
 * Parameters:
 *   tile or x/y: The depot tile (required)
 *   company: Company ID (default: 0)
 *
 * Returns:
 *   success: Whether the removal succeeded
 */
static nlohmann::json HandleDepotRemove(const nlohmann::json &params)
{
	/* Get tile */
	TileIndex tile;
	if (params.contains("tile")) {
		tile = static_cast<TileIndex>(params["tile"].get<uint32_t>());
	} else if (params.contains("x") && params.contains("y")) {
		uint x = params["x"].get<uint>();
		uint y = params["y"].get<uint>();
		if (x >= Map::SizeX() || y >= Map::SizeY()) {
			throw std::runtime_error("Tile coordinates out of bounds");
		}
		tile = TileXY(x, y);
	} else {
		throw std::runtime_error("Missing required parameter: tile or x/y");
	}

	/* Check if it's a depot */
	if (!IsDepotTile(tile)) {
		throw std::runtime_error("Specified tile is not a depot");
	}

	/* Get company */
	CompanyID company = static_cast<CompanyID>(params.value("company", 0));
	if (!Company::IsValidID(company)) {
		throw std::runtime_error("Invalid company ID");
	}

	/* Check ownership */
	if (GetTileOwner(tile) != company) {
		throw std::runtime_error("Depot is not owned by specified company");
	}

	/* Switch to company context */
	Backup<CompanyID> cur_company(_current_company, company);

	DoCommandFlags flags;
	flags.Set(DoCommandFlag::Execute);

	/* Use landscape clear to remove the depot */
	CommandCost cost = Command<CMD_LANDSCAPE_CLEAR>::Do(flags, tile);

	cur_company.Restore();

	nlohmann::json result;
	result["tile"] = tile != INVALID_TILE ? tile.base() : 0;
	result["success"] = cost.Succeeded();

	if (cost.Succeeded()) {
		RpcRecordActivity(tile, "depot.remove");
	} else {
		result["error"] = GetCommandErrorMessage(cost);
	}

	return result;
}

/**
 * Handler for rail.remove - Remove a rail track segment.
 *
 * Parameters:
 *   tile or x/y: The tile (required)
 *   track: Track direction to remove (required)
 *          Values: "x", "y", "upper", "lower", "left", "right"
 *   company: Company ID (default: 0)
 *
 * Returns:
 *   success: Whether the removal succeeded
 */
static nlohmann::json HandleRailRemove(const nlohmann::json &params)
{
	/* Get tile */
	TileIndex tile;
	if (params.contains("tile")) {
		tile = static_cast<TileIndex>(params["tile"].get<uint32_t>());
	} else if (params.contains("x") && params.contains("y")) {
		uint x = params["x"].get<uint>();
		uint y = params["y"].get<uint>();
		if (x >= Map::SizeX() || y >= Map::SizeY()) {
			throw std::runtime_error("Tile coordinates out of bounds");
		}
		tile = TileXY(x, y);
	} else {
		throw std::runtime_error("Missing required parameter: tile or x/y");
	}

	/* Get track */
	if (!params.contains("track")) {
		throw std::runtime_error("Missing required parameter: track");
	}

	std::string track_str = params["track"].get<std::string>();
	Track track;

	if (track_str == "x") {
		track = TRACK_X;
	} else if (track_str == "y") {
		track = TRACK_Y;
	} else if (track_str == "upper") {
		track = TRACK_UPPER;
	} else if (track_str == "lower") {
		track = TRACK_LOWER;
	} else if (track_str == "left") {
		track = TRACK_LEFT;
	} else if (track_str == "right") {
		track = TRACK_RIGHT;
	} else {
		throw std::runtime_error("Invalid track value - must be: x, y, upper, lower, left, right");
	}

	/* Check if tile has rail */
	if (!IsTileType(tile, MP_RAILWAY)) {
		throw std::runtime_error("Specified tile does not have railway");
	}

	/* Get company */
	CompanyID company = static_cast<CompanyID>(params.value("company", 0));
	if (!Company::IsValidID(company)) {
		throw std::runtime_error("Invalid company ID");
	}

	/* Check ownership */
	if (GetTileOwner(tile) != company) {
		throw std::runtime_error("Railway is not owned by specified company");
	}

	/* Switch to company context */
	Backup<CompanyID> cur_company(_current_company, company);

	DoCommandFlags flags;
	flags.Set(DoCommandFlag::Execute);

	CommandCost cost = Command<CMD_REMOVE_SINGLE_RAIL>::Do(flags, tile, track);

	cur_company.Restore();

	nlohmann::json result;
	result["tile"] = tile != INVALID_TILE ? tile.base() : 0;
	result["track"] = track_str;
	result["success"] = cost.Succeeded();

	if (cost.Succeeded()) {
		RpcRecordActivity(tile, "rail.remove");
	} else {
		result["error"] = GetCommandErrorMessage(cost);
	}

	return result;
}

/**
 * Handler for road.remove - Remove a road segment.
 *
 * Parameters:
 *   tile or x/y: The start tile (required)
 *   end_tile or end_x/end_y: The end tile (optional, defaults to start tile)
 *   road_type: Road type ID (default: 0)
 *   axis: "x" or "y" (required if spanning multiple tiles)
 *   company: Company ID (default: 0)
 *
 * Returns:
 *   success: Whether the removal succeeded
 *   refund: Money refunded for the removal
 */
static nlohmann::json HandleRoadRemove(const nlohmann::json &params)
{
	/* Get start tile */
	TileIndex start_tile;
	if (params.contains("tile")) {
		start_tile = static_cast<TileIndex>(params["tile"].get<uint32_t>());
	} else if (params.contains("x") && params.contains("y")) {
		uint x = params["x"].get<uint>();
		uint y = params["y"].get<uint>();
		if (x >= Map::SizeX() || y >= Map::SizeY()) {
			throw std::runtime_error("Tile coordinates out of bounds");
		}
		start_tile = TileXY(x, y);
	} else {
		throw std::runtime_error("Missing required parameter: tile or x/y");
	}

	/* Get end tile (default to same as start) */
	TileIndex end_tile = start_tile;
	if (params.contains("end_tile")) {
		end_tile = static_cast<TileIndex>(params["end_tile"].get<uint32_t>());
	} else if (params.contains("end_x") && params.contains("end_y")) {
		uint x = params["end_x"].get<uint>();
		uint y = params["end_y"].get<uint>();
		if (x >= Map::SizeX() || y >= Map::SizeY()) {
			throw std::runtime_error("End tile coordinates out of bounds");
		}
		end_tile = TileXY(x, y);
	}

	/* Get road type */
	RoadType rt = static_cast<RoadType>(params.value("road_type", 0));

	/* Get axis - determine from tiles if not specified */
	Axis axis;
	if (params.contains("axis")) {
		std::string axis_str = params["axis"].get<std::string>();
		if (axis_str == "x") {
			axis = AXIS_X;
		} else if (axis_str == "y") {
			axis = AXIS_Y;
		} else {
			throw std::runtime_error("Invalid axis value - must be 'x' or 'y'");
		}
	} else {
		/* Infer axis from tile positions */
		if (TileX(start_tile) == TileX(end_tile)) {
			axis = AXIS_Y;
		} else if (TileY(start_tile) == TileY(end_tile)) {
			axis = AXIS_X;
		} else {
			throw std::runtime_error("Tiles must be aligned on X or Y axis, or specify axis parameter");
		}
	}

	/* Check if start tile has road */
	if (!IsTileType(start_tile, MP_ROAD)) {
		throw std::runtime_error("Start tile does not have road");
	}

	/* Get company */
	CompanyID company = static_cast<CompanyID>(params.value("company", 0));
	if (!Company::IsValidID(company)) {
		throw std::runtime_error("Invalid company ID");
	}

	/* Check ownership */
	Owner tile_owner = GetTileOwner(start_tile);
	if (tile_owner != company && tile_owner != OWNER_TOWN && tile_owner != OWNER_NONE) {
		throw std::runtime_error("Road is not owned by specified company or town");
	}

	/* Switch to company context */
	Backup<CompanyID> cur_company(_current_company, company);

	DoCommandFlags flags;
	flags.Set(DoCommandFlag::Execute);

	auto [cost, refund] = Command<CMD_REMOVE_LONG_ROAD>::Do(flags, end_tile, start_tile, rt, axis, false, false);

	cur_company.Restore();

	nlohmann::json result;
	result["start_tile"] = start_tile != INVALID_TILE ? start_tile.base() : 0;
	result["end_tile"] = end_tile != INVALID_TILE ? end_tile.base() : 0;
	result["success"] = cost.Succeeded();

	if (cost.Succeeded()) {
		result["refund"] = refund.base();
		RpcRecordActivity(start_tile, "road.remove");
	} else {
		result["error"] = GetCommandErrorMessage(cost);
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
	server.RegisterHandler("vehicle.refit", HandleVehicleRefit);
	server.RegisterHandler("vehicle.attach", HandleVehicleAttach);
	server.RegisterHandler("order.append", HandleOrderAppend);
	server.RegisterHandler("order.remove", HandleOrderRemove);
	server.RegisterHandler("order.insert", HandleOrderInsert);
	server.RegisterHandler("order.setFlags", HandleOrderSetFlags);
	server.RegisterHandler("order.share", HandleOrderShare);
	server.RegisterHandler("company.setLoan", HandleCompanySetLoan);
	server.RegisterHandler("town.performAction", HandleTownPerformAction);
	server.RegisterHandler("station.remove", HandleStationRemove);
	server.RegisterHandler("depot.remove", HandleDepotRemove);
	server.RegisterHandler("rail.remove", HandleRailRemove);
	server.RegisterHandler("road.remove", HandleRoadRemove);
}
