/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file rpc_handlers_infra.cpp JSON-RPC handlers for infrastructure building. */

#include "../stdafx.h"
#include "rpc_handlers.h"
#include "../station_base.h"
#include "../map_func.h"
#include "../tile_map.h"
#include "../command_func.h"
#include "../core/backup_type.hpp"
#include "../road_cmd.h"
#include "../rail_cmd.h"
#include "../station_cmd.h"
#include "../road_map.h"
#include "../rail_map.h"
#include "../direction_func.h"
#include "../newgrf_station.h"
#include "../newgrf_roadstop.h"
#include "../strings_func.h"
#include "../string_func.h"

#include "../safeguards.h"

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

/**
 * Parse a direction string to DiagDirection.
 * Valid values: "ne", "se", "sw", "nw" (or 0-3)
 */
static DiagDirection ParseDiagDirection(const nlohmann::json &value)
{
	if (value.is_number()) {
		int dir = value.get<int>();
		if (dir < 0 || dir > 3) {
			throw std::runtime_error("Invalid direction: must be 0-3");
		}
		return static_cast<DiagDirection>(dir);
	}

	std::string dir_str = value.get<std::string>();
	if (dir_str == "ne") return DIAGDIR_NE;
	if (dir_str == "se") return DIAGDIR_SE;
	if (dir_str == "sw") return DIAGDIR_SW;
	if (dir_str == "nw") return DIAGDIR_NW;

	throw std::runtime_error("Invalid direction: must be ne, se, sw, nw (or 0-3)");
}

/**
 * Parse road bits from a string or integer.
 * Valid strings: "x", "y", "ne", "se", "sw", "nw", "all", or combinations like "ne+sw"
 */
static RoadBits ParseRoadBits(const nlohmann::json &value)
{
	if (value.is_number()) {
		int bits = value.get<int>();
		if (bits < 0 || bits > 15) {
			throw std::runtime_error("Invalid road bits: must be 0-15");
		}
		return static_cast<RoadBits>(bits);
	}

	std::string str = value.get<std::string>();

	/* Single value shortcuts */
	if (str == "x") return ROAD_X;
	if (str == "y") return ROAD_Y;
	if (str == "all") return ROAD_ALL;
	if (str == "none") return ROAD_NONE;

	/* Parse individual bits or combinations */
	RoadBits result = ROAD_NONE;
	if (str.find("ne") != std::string::npos) result = static_cast<RoadBits>(result | ROAD_NE);
	if (str.find("nw") != std::string::npos) result = static_cast<RoadBits>(result | ROAD_NW);
	if (str.find("se") != std::string::npos) result = static_cast<RoadBits>(result | ROAD_SE);
	if (str.find("sw") != std::string::npos) result = static_cast<RoadBits>(result | ROAD_SW);

	if (result == ROAD_NONE && str != "none") {
		throw std::runtime_error("Invalid road bits: use x, y, all, or ne/se/sw/nw combinations");
	}

	return result;
}

/**
 * Parse track direction from string or integer.
 */
static Track ParseTrack(const nlohmann::json &value)
{
	if (value.is_number()) {
		int track = value.get<int>();
		if (track < 0 || track > 5) {
			throw std::runtime_error("Invalid track: must be 0-5");
		}
		return static_cast<Track>(track);
	}

	std::string str = value.get<std::string>();
	if (str == "x") return TRACK_X;
	if (str == "y") return TRACK_Y;
	if (str == "upper" || str == "n") return TRACK_UPPER;
	if (str == "lower" || str == "s") return TRACK_LOWER;
	if (str == "left" || str == "w") return TRACK_LEFT;
	if (str == "right" || str == "e") return TRACK_RIGHT;

	throw std::runtime_error("Invalid track: use x, y, upper, lower, left, right (or n, s, w, e)");
}

/**
 * Parse axis from string or integer.
 */
static Axis ParseAxis(const nlohmann::json &value)
{
	if (value.is_number()) {
		int axis = value.get<int>();
		if (axis < 0 || axis > 1) {
			throw std::runtime_error("Invalid axis: must be 0 or 1");
		}
		return static_cast<Axis>(axis);
	}

	std::string str = value.get<std::string>();
	if (str == "x" || str == "horizontal") return AXIS_X;
	if (str == "y" || str == "vertical") return AXIS_Y;

	throw std::runtime_error("Invalid axis: use x, y, horizontal, or vertical");
}

/**
 * Get the direction name as a string.
 */
static const char *DiagDirectionToString(DiagDirection dir)
{
	switch (dir) {
		case DIAGDIR_NE: return "ne";
		case DIAGDIR_SE: return "se";
		case DIAGDIR_SW: return "sw";
		case DIAGDIR_NW: return "nw";
		default: return "unknown";
	}
}

/**
 * Handler for tile.getRoadInfo - get road/rail info for a tile.
 * Helps agents determine correct depot/station orientation.
 */
static nlohmann::json HandleTileGetRoadInfo(const nlohmann::json &params)
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

	TileType tile_type = GetTileType(tile);
	result["tile_type"] = TileTypeToString(tile_type);

	/* Road information */
	if (tile_type == MP_ROAD) {
		RoadTileType road_type = GetRoadTileType(tile);
		result["road_tile_type"] = road_type == RoadTileType::Normal ? "normal" :
		                          road_type == RoadTileType::Crossing ? "crossing" :
		                          road_type == RoadTileType::Depot ? "depot" : "unknown";

		if (road_type == RoadTileType::Normal) {
			RoadBits all_bits = GetAllRoadBits(tile);
			result["road_bits"] = static_cast<int>(all_bits);

			/* Provide human-readable road directions */
			nlohmann::json directions = nlohmann::json::array();
			if (all_bits & ROAD_NE) directions.push_back("ne");
			if (all_bits & ROAD_SE) directions.push_back("se");
			if (all_bits & ROAD_SW) directions.push_back("sw");
			if (all_bits & ROAD_NW) directions.push_back("nw");
			result["road_directions"] = directions;

			/* Suggest depot orientations - depot must face road */
			nlohmann::json depot_orientations = nlohmann::json::array();
			/* Check each adjacent tile for valid depot placement */
			for (DiagDirection dir = DIAGDIR_BEGIN; dir < DIAGDIR_END; dir++) {
				/* Depot at this tile facing 'dir' exits onto tile in direction 'dir' */
				/* So we need road in the opposite direction */
				RoadBits needed = DiagDirToRoadBits(dir);
				if (all_bits & needed) {
					nlohmann::json orient;
					orient["direction"] = DiagDirectionToString(dir);
					orient["direction_value"] = static_cast<int>(dir);
					orient["description"] = std::string("Depot facing ") + DiagDirectionToString(dir);
					depot_orientations.push_back(orient);
				}
			}
			result["valid_depot_orientations"] = depot_orientations;
		} else if (road_type == RoadTileType::Depot) {
			DiagDirection depot_dir = GetRoadDepotDirection(tile);
			result["depot_direction"] = DiagDirectionToString(depot_dir);
			result["depot_direction_value"] = static_cast<int>(depot_dir);
		}
	}

	/* Rail information */
	if (tile_type == MP_RAILWAY) {
		TrackBits track_bits = GetTrackBits(tile);
		result["track_bits"] = static_cast<int>(track_bits);

		nlohmann::json tracks = nlohmann::json::array();
		if (track_bits & TRACK_BIT_X) tracks.push_back("x");
		if (track_bits & TRACK_BIT_Y) tracks.push_back("y");
		if (track_bits & TRACK_BIT_UPPER) tracks.push_back("upper");
		if (track_bits & TRACK_BIT_LOWER) tracks.push_back("lower");
		if (track_bits & TRACK_BIT_LEFT) tracks.push_back("left");
		if (track_bits & TRACK_BIT_RIGHT) tracks.push_back("right");
		result["tracks"] = tracks;

		/* Check if depot */
		if (IsRailDepotTile(tile)) {
			DiagDirection depot_dir = GetRailDepotDirection(tile);
			result["depot_direction"] = DiagDirectionToString(depot_dir);
			result["depot_direction_value"] = static_cast<int>(depot_dir);
		}
	}

	/* Station information */
	if (tile_type == MP_STATION) {
		StationID sid = GetStationIndex(tile);
		const Station *st = Station::GetIfValid(sid);
		if (st != nullptr) {
			result["station_id"] = sid.base();
			result["station_name"] = StrMakeValid(st->GetCachedName());
		}
	}

	return result;
}

/**
 * Handler for road.build - build road pieces on a tile.
 */
static nlohmann::json HandleRoadBuild(const nlohmann::json &params)
{
	if (!params.contains("tile") && !(params.contains("x") && params.contains("y"))) {
		throw std::runtime_error("Missing required parameter: tile or x/y");
	}
	if (!params.contains("pieces")) {
		throw std::runtime_error("Missing required parameter: pieces (road bits)");
	}

	TileIndex tile;
	if (params.contains("tile")) {
		tile = static_cast<TileIndex>(params["tile"].get<uint32_t>());
	} else {
		uint x = params["x"].get<uint>();
		uint y = params["y"].get<uint>();
		tile = TileXY(x, y);
	}

	RoadBits pieces = ParseRoadBits(params["pieces"]);
	RoadType rt = static_cast<RoadType>(params.value("road_type", 0));
	DisallowedRoadDirections drd = DRD_NONE;
	TownID town_id = TownID::Invalid();

	/* Set company context */
	CompanyID company = static_cast<CompanyID>(params.value("company", 0));
	Backup<CompanyID> cur_company(_current_company, company);

	DoCommandFlags flags;
	flags.Set(DoCommandFlag::Execute);
	CommandCost cost = Command<CMD_BUILD_ROAD>::Do(flags, tile, pieces, rt, drd, town_id);

	cur_company.Restore();

	nlohmann::json result;
	result["tile"] = tile.base();
	result["success"] = cost.Succeeded();
	result["cost"] = cost.GetCost().base();

	if (cost.Failed()) {
		result["error"] = "Failed to build road";
	}

	return result;
}

/**
 * Handler for road.buildDepot - build a road vehicle depot.
 */
static nlohmann::json HandleRoadBuildDepot(const nlohmann::json &params)
{
	if (!params.contains("tile") && !(params.contains("x") && params.contains("y"))) {
		throw std::runtime_error("Missing required parameter: tile or x/y");
	}
	if (!params.contains("direction")) {
		throw std::runtime_error("Missing required parameter: direction (ne, se, sw, nw)");
	}

	TileIndex tile;
	if (params.contains("tile")) {
		tile = static_cast<TileIndex>(params["tile"].get<uint32_t>());
	} else {
		uint x = params["x"].get<uint>();
		uint y = params["y"].get<uint>();
		tile = TileXY(x, y);
	}

	DiagDirection dir = ParseDiagDirection(params["direction"]);
	RoadType rt = static_cast<RoadType>(params.value("road_type", 0));

	/* Set company context */
	CompanyID company = static_cast<CompanyID>(params.value("company", 0));
	Backup<CompanyID> cur_company(_current_company, company);

	DoCommandFlags flags;
	flags.Set(DoCommandFlag::Execute);
	CommandCost cost = Command<CMD_BUILD_ROAD_DEPOT>::Do(flags, tile, rt, dir);

	cur_company.Restore();

	nlohmann::json result;
	result["tile"] = tile.base();
	result["direction"] = DiagDirectionToString(dir);
	result["success"] = cost.Succeeded();
	result["cost"] = cost.GetCost().base();

	if (cost.Failed()) {
		result["error"] = "Failed to build depot - check adjacent road and direction";
	}

	return result;
}

/**
 * Handler for road.buildStop - build a bus or truck stop.
 */
static nlohmann::json HandleRoadBuildStop(const nlohmann::json &params)
{
	if (!params.contains("tile") && !(params.contains("x") && params.contains("y"))) {
		throw std::runtime_error("Missing required parameter: tile or x/y");
	}
	if (!params.contains("direction")) {
		throw std::runtime_error("Missing required parameter: direction (ne, se, sw, nw)");
	}

	TileIndex tile;
	if (params.contains("tile")) {
		tile = static_cast<TileIndex>(params["tile"].get<uint32_t>());
	} else {
		uint x = params["x"].get<uint>();
		uint y = params["y"].get<uint>();
		tile = TileXY(x, y);
	}

	DiagDirection ddir = ParseDiagDirection(params["direction"]);
	RoadType rt = static_cast<RoadType>(params.value("road_type", 0));

	/* Stop type: "bus" or "truck" */
	std::string stop_type_str = params.value("stop_type", "bus");
	RoadStopType stop_type = (stop_type_str == "truck") ? RoadStopType::Truck : RoadStopType::Bus;

	/* Drive-through or terminal */
	bool is_drive_through = params.value("drive_through", false);

	/* Station dimensions */
	uint8_t width = params.value("width", 1);
	uint8_t length = params.value("length", 1);

	/* Station to join or create new */
	StationID station_to_join = StationID::Invalid();
	if (params.contains("station_id")) {
		station_to_join = static_cast<StationID>(params["station_id"].get<int>());
	}
	bool adjacent = params.value("adjacent", false);

	/* Set company context */
	CompanyID company = static_cast<CompanyID>(params.value("company", 0));
	Backup<CompanyID> cur_company(_current_company, company);

	DoCommandFlags flags;
	flags.Set(DoCommandFlag::Execute);
	CommandCost cost = Command<CMD_BUILD_ROAD_STOP>::Do(flags, tile, width, length, stop_type,
		is_drive_through, ddir, rt, ROADSTOP_CLASS_DFLT, 0, station_to_join, adjacent);

	cur_company.Restore();

	nlohmann::json result;
	result["tile"] = tile.base();
	result["direction"] = DiagDirectionToString(ddir);
	result["stop_type"] = stop_type_str;
	result["drive_through"] = is_drive_through;
	result["success"] = cost.Succeeded();
	result["cost"] = cost.GetCost().base();

	if (cost.Failed()) {
		result["error"] = "Failed to build stop - check location, direction, and road access";
	}

	return result;
}

/**
 * Handler for rail.buildTrack - build railway track.
 */
static nlohmann::json HandleRailBuildTrack(const nlohmann::json &params)
{
	if (!params.contains("tile") && !(params.contains("x") && params.contains("y"))) {
		throw std::runtime_error("Missing required parameter: tile or x/y (start tile)");
	}
	if (!params.contains("track")) {
		throw std::runtime_error("Missing required parameter: track (x, y, upper, lower, left, right)");
	}

	TileIndex start_tile;
	if (params.contains("tile")) {
		start_tile = static_cast<TileIndex>(params["tile"].get<uint32_t>());
	} else {
		uint x = params["x"].get<uint>();
		uint y = params["y"].get<uint>();
		start_tile = TileXY(x, y);
	}

	/* End tile for drag building (same as start for single tile) */
	TileIndex end_tile = start_tile;
	if (params.contains("end_tile")) {
		end_tile = static_cast<TileIndex>(params["end_tile"].get<uint32_t>());
	} else if (params.contains("end_x") && params.contains("end_y")) {
		uint ex = params["end_x"].get<uint>();
		uint ey = params["end_y"].get<uint>();
		end_tile = TileXY(ex, ey);
	}

	Track track = ParseTrack(params["track"]);
	RailType railtype = static_cast<RailType>(params.value("rail_type", 0));
	bool auto_remove_signals = params.value("auto_remove_signals", false);
	bool fail_on_obstacle = params.value("fail_on_obstacle", true);

	/* Set company context */
	CompanyID company = static_cast<CompanyID>(params.value("company", 0));
	Backup<CompanyID> cur_company(_current_company, company);

	DoCommandFlags flags;
	flags.Set(DoCommandFlag::Execute);
	CommandCost cost = Command<CMD_BUILD_RAILROAD_TRACK>::Do(flags, end_tile, start_tile, railtype,
		track, auto_remove_signals, fail_on_obstacle);

	cur_company.Restore();

	nlohmann::json result;
	result["start_tile"] = start_tile.base();
	result["end_tile"] = end_tile.base();
	result["success"] = cost.Succeeded();
	result["cost"] = cost.GetCost().base();

	if (cost.Failed()) {
		result["error"] = "Failed to build rail track - check terrain and obstacles";
	}

	return result;
}

/**
 * Handler for rail.buildDepot - build a train depot.
 */
static nlohmann::json HandleRailBuildDepot(const nlohmann::json &params)
{
	if (!params.contains("tile") && !(params.contains("x") && params.contains("y"))) {
		throw std::runtime_error("Missing required parameter: tile or x/y");
	}
	if (!params.contains("direction")) {
		throw std::runtime_error("Missing required parameter: direction (ne, se, sw, nw)");
	}

	TileIndex tile;
	if (params.contains("tile")) {
		tile = static_cast<TileIndex>(params["tile"].get<uint32_t>());
	} else {
		uint x = params["x"].get<uint>();
		uint y = params["y"].get<uint>();
		tile = TileXY(x, y);
	}

	DiagDirection dir = ParseDiagDirection(params["direction"]);
	RailType railtype = static_cast<RailType>(params.value("rail_type", 0));

	/* Set company context */
	CompanyID company = static_cast<CompanyID>(params.value("company", 0));
	Backup<CompanyID> cur_company(_current_company, company);

	DoCommandFlags flags;
	flags.Set(DoCommandFlag::Execute);
	CommandCost cost = Command<CMD_BUILD_TRAIN_DEPOT>::Do(flags, tile, railtype, dir);

	cur_company.Restore();

	nlohmann::json result;
	result["tile"] = tile.base();
	result["direction"] = DiagDirectionToString(dir);
	result["success"] = cost.Succeeded();
	result["cost"] = cost.GetCost().base();

	if (cost.Failed()) {
		result["error"] = "Failed to build depot - check terrain and track connection";
	}

	return result;
}

/**
 * Handler for rail.buildStation - build a train station.
 */
static nlohmann::json HandleRailBuildStation(const nlohmann::json &params)
{
	if (!params.contains("tile") && !(params.contains("x") && params.contains("y"))) {
		throw std::runtime_error("Missing required parameter: tile or x/y");
	}
	if (!params.contains("axis")) {
		throw std::runtime_error("Missing required parameter: axis (x or y)");
	}

	TileIndex tile;
	if (params.contains("tile")) {
		tile = static_cast<TileIndex>(params["tile"].get<uint32_t>());
	} else {
		uint x = params["x"].get<uint>();
		uint y = params["y"].get<uint>();
		tile = TileXY(x, y);
	}

	Axis axis = ParseAxis(params["axis"]);
	RailType railtype = static_cast<RailType>(params.value("rail_type", 0));
	uint8_t numtracks = params.value("platforms", 1);
	uint8_t plat_len = params.value("length", 1);

	/* Station to join or create new */
	StationID station_to_join = StationID::Invalid();
	if (params.contains("station_id")) {
		station_to_join = static_cast<StationID>(params["station_id"].get<int>());
	}
	bool adjacent = params.value("adjacent", false);

	/* Set company context */
	CompanyID company = static_cast<CompanyID>(params.value("company", 0));
	Backup<CompanyID> cur_company(_current_company, company);

	DoCommandFlags flags;
	flags.Set(DoCommandFlag::Execute);
	CommandCost cost = Command<CMD_BUILD_RAIL_STATION>::Do(flags, tile, railtype, axis,
		numtracks, plat_len, STAT_CLASS_DFLT, 0, station_to_join, adjacent);

	cur_company.Restore();

	nlohmann::json result;
	result["tile"] = tile.base();
	result["axis"] = (axis == AXIS_X) ? "x" : "y";
	result["platforms"] = numtracks;
	result["length"] = plat_len;
	result["success"] = cost.Succeeded();
	result["cost"] = cost.GetCost().base();

	if (cost.Failed()) {
		result["error"] = "Failed to build station - check location and track alignment";
	}

	return result;
}

void RpcRegisterInfraHandlers(RpcServer &server)
{
	server.RegisterHandler("tile.getRoadInfo", HandleTileGetRoadInfo);
	server.RegisterHandler("road.build", HandleRoadBuild);
	server.RegisterHandler("road.buildDepot", HandleRoadBuildDepot);
	server.RegisterHandler("road.buildStop", HandleRoadBuildStop);
	server.RegisterHandler("rail.buildTrack", HandleRailBuildTrack);
	server.RegisterHandler("rail.buildDepot", HandleRailBuildDepot);
	server.RegisterHandler("rail.buildStation", HandleRailBuildStation);
}
