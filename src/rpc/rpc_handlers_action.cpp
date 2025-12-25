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

#include "../safeguards.h"

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

void RpcRegisterActionHandlers(RpcServer &server)
{
	server.RegisterHandler("vehicle.startstop", HandleVehicleStartStop);
	server.RegisterHandler("vehicle.depot", HandleVehicleSendToDepot);
	server.RegisterHandler("vehicle.turnaround", HandleVehicleTurnAround);
	server.RegisterHandler("order.append", HandleOrderAppend);
	server.RegisterHandler("order.remove", HandleOrderRemove);
}
