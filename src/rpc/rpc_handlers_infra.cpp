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
#include "../signal_type.h"
#include "../direction_func.h"
#include "../water_cmd.h"
#include "../tunnelbridge_cmd.h"
#include "../tunnelbridge.h"
#include "../bridge.h"
#include "../airport.h"
#include "../newgrf_station.h"
#include "../newgrf_roadstop.h"
#include "../strings_func.h"
#include "../string_func.h"

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

	if (!IsValidTile(tile)) {
		throw std::runtime_error("Invalid tile index (out of bounds or void tile)");
	}

	nlohmann::json result;
	result["tile"] = tile.base();
	result["x"] = TileX(tile);
	result["y"] = TileY(tile);

	TileType tile_type = GetTileType(tile);
	result["tile_type"] = RpcTileTypeToString(tile_type);

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
 *
 * By default, auto-connects to adjacent roads. Set auto_connect=false to disable.
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
	uint tile_x, tile_y;
	if (params.contains("tile")) {
		tile = static_cast<TileIndex>(params["tile"].get<uint32_t>());
		tile_x = TileX(tile);
		tile_y = TileY(tile);
	} else {
		tile_x = params["x"].get<uint>();
		tile_y = params["y"].get<uint>();
		tile = TileXY(tile_x, tile_y);
	}

	RoadBits pieces = ParseRoadBits(params["pieces"]);
	RoadType rt = static_cast<RoadType>(params.value("road_type", 0));
	DisallowedRoadDirections drd = DRD_NONE;
	TownID town_id = TownID::Invalid();
	bool auto_connect = params.value("auto_connect", true);

	/* Set company context */
	CompanyID company = static_cast<CompanyID>(params.value("company", 0));
	Backup<CompanyID> cur_company(_current_company, company);

	DoCommandFlags flags;
	flags.Set(DoCommandFlag::Execute);
	CommandCost cost = Command<CMD_BUILD_ROAD>::Do(flags, tile, pieces, rt, drd, town_id);

	Money total_cost = cost.GetCost();
	bool main_success = cost.Succeeded();

	if (main_success) {
		RpcRecordActivity(tile, "road.build");
	}

	/* Auto-connect to adjacent roads if enabled and main build succeeded */
	nlohmann::json connections = nlohmann::json::array();
	if (auto_connect && main_success) {
		/* Check all 4 adjacent tiles for roads that need connecting */
		struct AdjacentCheck {
			int dx, dy;
			RoadBits our_bit;    /* Bit we need on our tile pointing toward adjacent */
			RoadBits their_bit;  /* Bit they need pointing toward us */
		};
		const AdjacentCheck checks[] = {
			{ 0, -1, ROAD_NW, ROAD_SE },  /* Adjacent tile to NW (y-1) */
			{ 1,  0, ROAD_NE, ROAD_SW },  /* Adjacent tile to NE (x+1) */
			{ 0,  1, ROAD_SE, ROAD_NW },  /* Adjacent tile to SE (y+1) */
			{-1,  0, ROAD_SW, ROAD_NE },  /* Adjacent tile to SW (x-1) */
		};

		for (const auto &check : checks) {
			int adj_x = static_cast<int>(tile_x) + check.dx;
			int adj_y = static_cast<int>(tile_y) + check.dy;

			/* Skip if out of bounds */
			if (adj_x < 0 || adj_y < 0 ||
			    static_cast<uint>(adj_x) >= Map::SizeX() ||
			    static_cast<uint>(adj_y) >= Map::SizeY()) continue;

			TileIndex adj_tile = TileXY(adj_x, adj_y);
			if (!IsValidTile(adj_tile)) continue;

			/* Check if adjacent tile has road or is a road stop/station */
			bool has_road = false;
			RoadBits adj_bits = ROAD_NONE;

			if (IsTileType(adj_tile, MP_ROAD)) {
				has_road = true;
				adj_bits = GetRoadBits(adj_tile, RTT_ROAD);
			} else if (IsTileType(adj_tile, MP_STATION)) {
				/* Stations (bus/truck stops) can connect */
				has_road = IsStationRoadStop(adj_tile);
			}

			if (!has_road) continue;

			/* If adjacent has road bits pointing toward us, or is a station,
			 * ensure we have bits pointing toward them and they toward us */
			bool needs_our_bit = !(GetRoadBits(tile, RTT_ROAD) & check.our_bit);
			bool needs_their_bit = (adj_bits != ROAD_NONE) && !(adj_bits & check.their_bit);

			nlohmann::json conn_result;
			conn_result["adjacent_x"] = adj_x;
			conn_result["adjacent_y"] = adj_y;
			conn_result["direction"] = (check.dx == 0 && check.dy == -1) ? "nw" :
			                           (check.dx == 1 && check.dy == 0) ? "ne" :
			                           (check.dx == 0 && check.dy == 1) ? "se" : "sw";

			/* Add our bit if needed */
			if (needs_our_bit) {
				CommandCost c = Command<CMD_BUILD_ROAD>::Do(flags, tile, check.our_bit, rt, DRD_NONE, TownID::Invalid());
				if (c.Succeeded()) {
					total_cost += c.GetCost();
					conn_result["our_bit_added"] = true;
				}
			}

			/* Add their bit if needed (for road tiles, not stations) */
			if (needs_their_bit && IsTileType(adj_tile, MP_ROAD)) {
				CommandCost c = Command<CMD_BUILD_ROAD>::Do(flags, adj_tile, check.their_bit, rt, DRD_NONE, TownID::Invalid());
				if (c.Succeeded()) {
					total_cost += c.GetCost();
					conn_result["their_bit_added"] = true;
					RpcRecordActivity(adj_tile, "road.build.autoconnect");
				}
			}

			if (conn_result.contains("our_bit_added") || conn_result.contains("their_bit_added")) {
				connections.push_back(conn_result);
			}
		}
	}

	cur_company.Restore();

	nlohmann::json result;
	result["tile"] = tile.base();
	result["x"] = tile_x;
	result["y"] = tile_y;
	result["success"] = main_success;
	result["cost"] = static_cast<int64_t>(total_cost);

	if (!connections.empty()) {
		result["auto_connections"] = connections;
	}

	if (cost.Failed()) {
		result["error"] = GetCommandErrorMessage(cost);
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

	if (cost.Succeeded()) {
		RpcRecordActivity(tile, "road.buildDepot");
	}

	nlohmann::json result;
	result["tile"] = tile.base();
	result["direction"] = DiagDirectionToString(dir);
	result["success"] = cost.Succeeded();
	result["cost"] = cost.GetCost().base();

	if (cost.Failed()) {
		result["error"] = GetCommandErrorMessage(cost);
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

	if (cost.Succeeded()) {
		RpcRecordActivity(tile, "road.buildStop");
	}

	nlohmann::json result;
	result["tile"] = tile.base();
	result["direction"] = DiagDirectionToString(ddir);
	result["stop_type"] = stop_type_str;
	result["drive_through"] = is_drive_through;
	result["success"] = cost.Succeeded();
	result["cost"] = cost.GetCost().base();

	if (cost.Failed()) {
		result["error"] = GetCommandErrorMessage(cost);
	}

	return result;
}

/**
 * Handler for road.buildLine - build road from start to end tile.
 * Uses CMD_BUILD_LONG_ROAD to build along a straight line (horizontal or vertical only).
 *
 * Parameters:
 *   start_x, start_y: Starting coordinates
 *   end_x, end_y: Ending coordinates
 *   road_type: Road type (optional, default 0)
 *   one_way: Build one-way road (optional, default false)
 *   company: Company ID (optional, default 0)
 *
 * Note: Only supports horizontal (same Y) or vertical (same X) lines.
 */
static nlohmann::json HandleRoadBuildLine(const nlohmann::json &params)
{
	if (!params.contains("start_x") || !params.contains("start_y") ||
	    !params.contains("end_x") || !params.contains("end_y")) {
		throw std::runtime_error("Missing required parameters: start_x, start_y, end_x, end_y");
	}

	uint start_x = params["start_x"].get<uint>();
	uint start_y = params["start_y"].get<uint>();
	uint end_x = params["end_x"].get<uint>();
	uint end_y = params["end_y"].get<uint>();

	if (start_x >= Map::SizeX() || start_y >= Map::SizeY() ||
	    end_x >= Map::SizeX() || end_y >= Map::SizeY()) {
		throw std::runtime_error("Coordinates out of bounds");
	}

	TileIndex start_tile = TileXY(start_x, start_y);
	TileIndex end_tile = TileXY(end_x, end_y);

	RoadType roadtype = static_cast<RoadType>(params.value("road_type", 0));
	bool one_way = params.value("one_way", false);

	/* Determine axis based on direction */
	int dx = (int)end_x - (int)start_x;
	int dy = (int)end_y - (int)start_y;

	/* CMD_BUILD_LONG_ROAD only supports horizontal or vertical lines */
	if (dx != 0 && dy != 0) {
		throw std::runtime_error("road.buildLine only supports horizontal (same Y) or vertical (same X) lines. Use multiple calls for L-shaped routes.");
	}

	if (dx == 0 && dy == 0) {
		throw std::runtime_error("Start and end tiles are the same");
	}

	Axis axis = (dy == 0) ? AXIS_X : AXIS_Y;
	DisallowedRoadDirections drd = one_way ? DRD_NORTHBOUND : DRD_NONE;

	/* Set company context */
	CompanyID company = static_cast<CompanyID>(params.value("company", 0));
	Backup<CompanyID> cur_company(_current_company, company);

	DoCommandFlags flags;
	flags.Set(DoCommandFlag::Execute);

	/* Build the road line
	 * Parameters for CMD_BUILD_LONG_ROAD:
	 * - start_half: false = build full tile at start
	 * - end_half: true = build full tile at end (important for connecting to existing roads!)
	 * - is_ai: true = use AI behavior (consistent building without CanConnectToRoad checks)
	 */
	CommandCost cost = Command<CMD_BUILD_LONG_ROAD>::Do(flags, end_tile, start_tile, roadtype, axis, drd, false, true, true);

	Money total_cost = cost.GetCost();
	bool main_success = cost.Succeeded();

	if (main_success) {
		RpcRecordActivity(start_tile, "road.buildLine");
		RpcRecordActivity(end_tile, "road.buildLine");
	}

	/* Auto-connect at endpoints to adjacent roads */
	nlohmann::json connections = nlohmann::json::array();
	if (main_success) {
		/* For each endpoint, check perpendicular directions for adjacent roads */
		struct EndpointCheck {
			TileIndex tile;
			uint x, y;
			const char *name;
		};
		EndpointCheck endpoints[] = {
			{ start_tile, start_x, start_y, "start" },
			{ end_tile, end_x, end_y, "end" }
		};

		/* Directions perpendicular to the line axis */
		struct AdjacentCheck {
			int dx, dy;
			RoadBits our_bit;
			RoadBits their_bit;
			const char *dir_name;
		};

		/* For X-axis lines, check Y directions (NW/SE). For Y-axis, check X directions (NE/SW) */
		AdjacentCheck x_axis_checks[] = {
			{ 0, -1, ROAD_NW, ROAD_SE, "nw" },
			{ 0,  1, ROAD_SE, ROAD_NW, "se" }
		};
		AdjacentCheck y_axis_checks[] = {
			{ 1,  0, ROAD_NE, ROAD_SW, "ne" },
			{-1,  0, ROAD_SW, ROAD_NE, "sw" }
		};
		/* Also check in the direction of the line for connections beyond endpoints */
		AdjacentCheck start_end_x[] = {
			{-1,  0, ROAD_SW, ROAD_NE, "sw" },  /* Before start on X axis */
			{ 1,  0, ROAD_NE, ROAD_SW, "ne" }   /* After end on X axis */
		};
		AdjacentCheck start_end_y[] = {
			{ 0, -1, ROAD_NW, ROAD_SE, "nw" },  /* Before start on Y axis */
			{ 0,  1, ROAD_SE, ROAD_NW, "se" }   /* After end on Y axis */
		};

		const AdjacentCheck *perp_checks = (axis == AXIS_X) ? x_axis_checks : y_axis_checks;
		int perp_count = 2;

		for (auto &ep : endpoints) {
			/* Check perpendicular directions */
			for (int i = 0; i < perp_count; i++) {
				const auto &check = perp_checks[i];
				int adj_x = static_cast<int>(ep.x) + check.dx;
				int adj_y = static_cast<int>(ep.y) + check.dy;

				if (adj_x < 0 || adj_y < 0 ||
				    static_cast<uint>(adj_x) >= Map::SizeX() ||
				    static_cast<uint>(adj_y) >= Map::SizeY()) continue;

				TileIndex adj_tile = TileXY(adj_x, adj_y);
				if (!IsValidTile(adj_tile)) continue;

				bool has_road = IsTileType(adj_tile, MP_ROAD) ||
				                (IsTileType(adj_tile, MP_STATION) && IsStationRoadStop(adj_tile));
				if (!has_road) continue;

				/* Add connecting bits */
				RoadBits adj_bits = IsTileType(adj_tile, MP_ROAD) ? GetRoadBits(adj_tile, RTT_ROAD) : ROAD_NONE;
				RoadBits our_bits = GetRoadBits(ep.tile, RTT_ROAD);

				nlohmann::json conn;
				conn["endpoint"] = ep.name;
				conn["adjacent_x"] = adj_x;
				conn["adjacent_y"] = adj_y;
				conn["direction"] = check.dir_name;

				if (!(our_bits & check.our_bit)) {
					CommandCost c = Command<CMD_BUILD_ROAD>::Do(flags, ep.tile, check.our_bit, roadtype, DRD_NONE, TownID::Invalid());
					if (c.Succeeded()) {
						total_cost += c.GetCost();
						conn["our_bit_added"] = true;
					}
				}

				if (IsTileType(adj_tile, MP_ROAD) && !(adj_bits & check.their_bit)) {
					CommandCost c = Command<CMD_BUILD_ROAD>::Do(flags, adj_tile, check.their_bit, roadtype, DRD_NONE, TownID::Invalid());
					if (c.Succeeded()) {
						total_cost += c.GetCost();
						conn["their_bit_added"] = true;
						RpcRecordActivity(adj_tile, "road.buildLine.autoconnect");
					}
				}

				if (conn.contains("our_bit_added") || conn.contains("their_bit_added")) {
					connections.push_back(conn);
				}
			}
		}

		/* Also check beyond the line endpoints in the direction of travel */
		const AdjacentCheck *line_checks = (axis == AXIS_X) ? start_end_x : start_end_y;

		/* Check before start */
		{
			const auto &check = line_checks[0];
			int adj_x = static_cast<int>(start_x) + check.dx;
			int adj_y = static_cast<int>(start_y) + check.dy;

			if (adj_x >= 0 && adj_y >= 0 &&
			    static_cast<uint>(adj_x) < Map::SizeX() &&
			    static_cast<uint>(adj_y) < Map::SizeY()) {
				TileIndex adj_tile = TileXY(adj_x, adj_y);
				if (IsValidTile(adj_tile) && IsTileType(adj_tile, MP_ROAD)) {
					RoadBits adj_bits = GetRoadBits(adj_tile, RTT_ROAD);
					if (!(adj_bits & check.their_bit)) {
						CommandCost c = Command<CMD_BUILD_ROAD>::Do(flags, adj_tile, check.their_bit, roadtype, DRD_NONE, TownID::Invalid());
						if (c.Succeeded()) {
							total_cost += c.GetCost();
							nlohmann::json conn;
							conn["endpoint"] = "before_start";
							conn["adjacent_x"] = adj_x;
							conn["adjacent_y"] = adj_y;
							conn["their_bit_added"] = true;
							connections.push_back(conn);
							RpcRecordActivity(adj_tile, "road.buildLine.autoconnect");
						}
					}
				}
			}
		}

		/* Check after end */
		{
			const auto &check = line_checks[1];
			int adj_x = static_cast<int>(end_x) + check.dx;
			int adj_y = static_cast<int>(end_y) + check.dy;

			if (adj_x >= 0 && adj_y >= 0 &&
			    static_cast<uint>(adj_x) < Map::SizeX() &&
			    static_cast<uint>(adj_y) < Map::SizeY()) {
				TileIndex adj_tile = TileXY(adj_x, adj_y);
				if (IsValidTile(adj_tile) && IsTileType(adj_tile, MP_ROAD)) {
					RoadBits adj_bits = GetRoadBits(adj_tile, RTT_ROAD);
					if (!(adj_bits & check.their_bit)) {
						CommandCost c = Command<CMD_BUILD_ROAD>::Do(flags, adj_tile, check.their_bit, roadtype, DRD_NONE, TownID::Invalid());
						if (c.Succeeded()) {
							total_cost += c.GetCost();
							nlohmann::json conn;
							conn["endpoint"] = "after_end";
							conn["adjacent_x"] = adj_x;
							conn["adjacent_y"] = adj_y;
							conn["their_bit_added"] = true;
							connections.push_back(conn);
							RpcRecordActivity(adj_tile, "road.buildLine.autoconnect");
						}
					}
				}
			}
		}
	}

	cur_company.Restore();

	nlohmann::json result;
	result["start_tile"] = start_tile.base();
	result["end_tile"] = end_tile.base();
	result["start_x"] = start_x;
	result["start_y"] = start_y;
	result["end_x"] = end_x;
	result["end_y"] = end_y;
	result["axis"] = (axis == AXIS_X) ? "x" : "y";
	result["success"] = main_success;
	result["cost"] = static_cast<int64_t>(total_cost);

	if (!connections.empty()) {
		result["auto_connections"] = connections;
	}

	if (cost.Failed()) {
		result["error"] = GetCommandErrorMessage(cost);
	}

	return result;
}

/**
 * Handler for road.connect - build road connection between two adjacent tiles.
 *
 * This helper automatically builds the correct road bits on BOTH tiles
 * to form a proper connection. Useful for connecting to existing roads
 * where you need to add bits to both tiles (e.g., T-junction).
 *
 * Parameters:
 *   from_x, from_y: Source tile coordinates
 *   to_x, to_y: Target tile coordinates (must be adjacent to source)
 *   road_type: Road type (optional, default 0)
 *   company: Company ID (optional, default 0)
 */
static nlohmann::json HandleRoadConnect(const nlohmann::json &params)
{
	if (!params.contains("from_x") || !params.contains("from_y") ||
	    !params.contains("to_x") || !params.contains("to_y")) {
		throw std::runtime_error("Missing required parameters: from_x, from_y, to_x, to_y");
	}

	uint from_x = params["from_x"].get<uint>();
	uint from_y = params["from_y"].get<uint>();
	uint to_x = params["to_x"].get<uint>();
	uint to_y = params["to_y"].get<uint>();

	if (from_x >= Map::SizeX() || from_y >= Map::SizeY() ||
	    to_x >= Map::SizeX() || to_y >= Map::SizeY()) {
		throw std::runtime_error("Coordinates out of bounds");
	}

	/* Validate tiles are adjacent (Manhattan distance = 1) */
	int dx = (int)to_x - (int)from_x;
	int dy = (int)to_y - (int)from_y;
	if (std::abs(dx) + std::abs(dy) != 1) {
		throw std::runtime_error("Tiles must be adjacent (Manhattan distance = 1)");
	}

	TileIndex from_tile = TileXY(from_x, from_y);
	TileIndex to_tile = TileXY(to_x, to_y);

	RoadType roadtype = static_cast<RoadType>(params.value("road_type", 0));

	/* Determine direction from source to target and the road bits needed */
	RoadBits from_bits, to_bits;
	std::string direction;
	if (dx == 1) {
		/* Target is to the east (+X direction = SW in OpenTTD) */
		from_bits = ROAD_SW;
		to_bits = ROAD_NE;
		direction = "sw";
	} else if (dx == -1) {
		/* Target is to the west (-X direction = NE in OpenTTD) */
		from_bits = ROAD_NE;
		to_bits = ROAD_SW;
		direction = "ne";
	} else if (dy == 1) {
		/* Target is to the south (+Y direction = SE in OpenTTD) */
		from_bits = ROAD_SE;
		to_bits = ROAD_NW;
		direction = "se";
	} else { /* dy == -1 */
		/* Target is to the north (-Y direction = NW in OpenTTD) */
		from_bits = ROAD_NW;
		to_bits = ROAD_SE;
		direction = "nw";
	}

	/* Set company context */
	CompanyID company = static_cast<CompanyID>(params.value("company", 0));
	Backup<CompanyID> cur_company(_current_company, company);

	DoCommandFlags flags;
	flags.Set(DoCommandFlag::Execute);

	nlohmann::json result;
	result["from_tile"] = from_tile.base();
	result["to_tile"] = to_tile.base();
	result["from_x"] = from_x;
	result["from_y"] = from_y;
	result["to_x"] = to_x;
	result["to_y"] = to_y;
	result["direction"] = direction;

	Money total_cost = 0;
	bool from_success = false;
	bool to_success = false;
	std::string from_error, to_error;

	/* Build road bits on the source tile (pointing toward target) */
	CommandCost cost1 = Command<CMD_BUILD_ROAD>::Do(flags, from_tile, from_bits, roadtype, DRD_NONE, TownID::Invalid());
	from_success = cost1.Succeeded();
	if (from_success) {
		total_cost += cost1.GetCost();
		RpcRecordActivity(from_tile, "road.connect");
	} else {
		from_error = GetCommandErrorMessage(cost1);
		/* "Already built" is okay - the connection is there */
		if (from_error.find("already built") != std::string::npos) {
			from_success = true;
		}
	}

	/* Build road bits on the target tile (pointing toward source) */
	CommandCost cost2 = Command<CMD_BUILD_ROAD>::Do(flags, to_tile, to_bits, roadtype, DRD_NONE, TownID::Invalid());
	to_success = cost2.Succeeded();
	if (to_success) {
		total_cost += cost2.GetCost();
		RpcRecordActivity(to_tile, "road.connect");
	} else {
		to_error = GetCommandErrorMessage(cost2);
		/* "Already built" is okay - the connection is there */
		if (to_error.find("already built") != std::string::npos) {
			to_success = true;
		}
	}

	cur_company.Restore();

	result["success"] = from_success && to_success;
	result["cost"] = static_cast<int64_t>(total_cost);
	result["from_built"] = from_success;
	result["to_built"] = to_success;

	if (!from_success && !from_error.empty()) {
		result["from_error"] = from_error;
	}
	if (!to_success && !to_error.empty()) {
		result["to_error"] = to_error;
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

	if (cost.Succeeded()) {
		RpcRecordActivity(start_tile, "rail.buildTrack");
		if (end_tile != start_tile) {
			RpcRecordActivity(end_tile, "rail.buildTrack");
		}
	}

	nlohmann::json result;
	result["start_tile"] = start_tile.base();
	result["end_tile"] = end_tile.base();
	result["success"] = cost.Succeeded();
	result["cost"] = cost.GetCost().base();

	if (cost.Failed()) {
		result["error"] = GetCommandErrorMessage(cost);
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

	if (!IsValidTile(tile)) {
		throw std::runtime_error("Invalid tile index (out of bounds or void tile)");
	}

	DiagDirection dir = ParseDiagDirection(params["direction"]);
	RailType railtype = static_cast<RailType>(params.value("rail_type", 0));

	/* Set company context */
	CompanyID company = static_cast<CompanyID>(params.value("company", 0));
	Backup<CompanyID> cur_company(_current_company, company);

	/* First test if the command would succeed, to avoid signal update bugs */
	DoCommandFlags test_flags;
	CommandCost test_cost = Command<CMD_BUILD_TRAIN_DEPOT>::Do(test_flags, tile, railtype, dir);
	if (test_cost.Failed()) {
		cur_company.Restore();
		nlohmann::json result;
		result["tile"] = tile.base();
		result["direction"] = DiagDirectionToString(dir);
		result["success"] = false;
		result["cost"] = 0;
		result["error"] = GetCommandErrorMessage(test_cost);
		return result;
	}

	DoCommandFlags flags;
	flags.Set(DoCommandFlag::Execute);
	CommandCost cost = Command<CMD_BUILD_TRAIN_DEPOT>::Do(flags, tile, railtype, dir);

	cur_company.Restore();

	if (cost.Succeeded()) {
		RpcRecordActivity(tile, "rail.buildDepot");
	}

	nlohmann::json result;
	result["tile"] = tile.base();
	result["direction"] = DiagDirectionToString(dir);
	result["success"] = cost.Succeeded();
	result["cost"] = cost.GetCost().base();

	if (cost.Failed()) {
		result["error"] = GetCommandErrorMessage(cost);
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

	if (cost.Succeeded()) {
		RpcRecordActivity(tile, "rail.buildStation");
	}

	nlohmann::json result;
	result["tile"] = tile.base();
	result["axis"] = (axis == AXIS_X) ? "x" : "y";
	result["platforms"] = numtracks;
	result["length"] = plat_len;
	result["success"] = cost.Succeeded();
	result["cost"] = cost.GetCost().base();

	if (cost.Failed()) {
		result["error"] = GetCommandErrorMessage(cost);
	}

	return result;
}

/**
 * Parse a signal type string to SignalType enum.
 */
static SignalType ParseSignalType(const nlohmann::json &value)
{
	if (value.is_number()) {
		int sig = value.get<int>();
		if (sig < 0 || sig > SIGTYPE_LAST) {
			throw std::runtime_error("Invalid signal type: must be 0-5");
		}
		return static_cast<SignalType>(sig);
	}

	std::string str = value.get<std::string>();
	if (str == "block" || str == "normal") return SIGTYPE_BLOCK;
	if (str == "entry") return SIGTYPE_ENTRY;
	if (str == "exit") return SIGTYPE_EXIT;
	if (str == "combo") return SIGTYPE_COMBO;
	if (str == "pbs" || str == "path") return SIGTYPE_PBS;
	if (str == "pbs_oneway" || str == "path_oneway" || str == "no_entry") return SIGTYPE_PBS_ONEWAY;

	throw std::runtime_error("Invalid signal type: use block, entry, exit, combo, pbs, or pbs_oneway");
}

/**
 * Get signal type name as string.
 */
static const char *SignalTypeToString(SignalType type)
{
	switch (type) {
		case SIGTYPE_BLOCK: return "block";
		case SIGTYPE_ENTRY: return "entry";
		case SIGTYPE_EXIT: return "exit";
		case SIGTYPE_COMBO: return "combo";
		case SIGTYPE_PBS: return "pbs";
		case SIGTYPE_PBS_ONEWAY: return "pbs_oneway";
		default: return "unknown";
	}
}

/**
 * Handler for rail.buildSignal - build a rail signal.
 *
 * Parameters:
 *   tile: Tile index (optional if x,y provided)
 *   x, y: Tile coordinates (optional if tile provided)
 *   track: Track to place signal on (x, y, upper, lower, left, right)
 *   signal_type: Signal type (block, entry, exit, combo, pbs, pbs_oneway)
 *   variant: Signal variant (electric, semaphore) - default: electric
 *   two_way: Whether signal allows both directions (default: false)
 *   company: Company ID (default: 0)
 *
 * Returns:
 *   success: Whether the signal was built
 *   tile: The tile where signal was built
 *   track: The track the signal is on
 *   signal_type: The type of signal built
 *   cost: The cost of building the signal
 */
static nlohmann::json HandleRailBuildSignal(const nlohmann::json &params)
{
	/* Get tile */
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

	/* Get track */
	if (!params.contains("track")) {
		throw std::runtime_error("Missing required parameter: track (x, y, upper, lower, left, right)");
	}
	Track track = ParseTrack(params["track"]);

	/* Get signal type - default to block signal */
	SignalType sigtype = SIGTYPE_BLOCK;
	if (params.contains("signal_type")) {
		sigtype = ParseSignalType(params["signal_type"]);
	}

	/* Get signal variant (electric or semaphore) */
	SignalVariant sigvar = SIG_ELECTRIC;
	if (params.contains("variant")) {
		std::string var_str = params["variant"].get<std::string>();
		if (var_str == "semaphore" || var_str == "sem") {
			sigvar = SIG_SEMAPHORE;
		} else if (var_str != "electric" && var_str != "light") {
			throw std::runtime_error("Invalid variant: use electric or semaphore");
		}
	}

	/* Two-way signal flag */
	bool two_way = params.value("two_way", false);

	/*
	 * Calculate direction cycles based on signal type and two_way flag.
	 * For one-way signals (non-PBS), we need 1 direction cycle to make them one-way.
	 * For two-way signals, we use 0 cycles.
	 * For PBS signals, they're naturally one-way, so 0 cycles for one-way PBS.
	 */
	uint8_t num_dir_cycle = 0;
	if (!two_way) {
		/* One-way signal */
		if (sigtype != SIGTYPE_PBS && sigtype != SIGTYPE_PBS_ONEWAY) {
			/* Block/presignals need 1 cycle to be one-way */
			num_dir_cycle = 1;
		}
		/* PBS signals are already one-way by default */
	}
	/* two_way signals use 0 cycles */

	/* Set company context */
	CompanyID company = static_cast<CompanyID>(params.value("company", 0));
	Backup<CompanyID> cur_company(_current_company, company);

	DoCommandFlags flags;
	flags.Set(DoCommandFlag::Execute);

	/*
	 * CMD_BUILD_SINGLE_SIGNAL parameters:
	 * - tile: where to build
	 * - track: which track on the tile
	 * - sigtype: signal type (SIGTYPE_BLOCK, etc.)
	 * - sigvar: signal variant (SIG_ELECTRIC or SIG_SEMAPHORE)
	 * - convert_signal: false for new signals
	 * - skip_existing_signals: false
	 * - ctrl_pressed: false (would toggle semaphore/electric)
	 * - cycle_start, cycle_stop: not used for simple builds
	 * - num_dir_cycle: number of direction cycles (0-3)
	 * - signals_copy: 0 for new signals
	 */
	CommandCost cost = Command<CMD_BUILD_SINGLE_SIGNAL>::Do(
		flags,
		tile,
		track,
		sigtype,
		sigvar,
		false,              /* convert_signal */
		false,              /* skip_existing_signals */
		false,              /* ctrl_pressed */
		SIGTYPE_BLOCK,      /* cycle_start (unused) */
		SIGTYPE_BLOCK,      /* cycle_stop (unused) */
		num_dir_cycle,
		0                   /* signals_copy */
	);

	cur_company.Restore();

	if (cost.Succeeded()) {
		RpcRecordActivity(tile, "rail.buildSignal");
	}

	nlohmann::json result;
	result["tile"] = tile.base();
	result["x"] = TileX(tile);
	result["y"] = TileY(tile);
	result["success"] = cost.Succeeded();
	result["cost"] = cost.GetCost().base();

	if (cost.Succeeded()) {
		/* Return details about what was built */
		result["signal_type"] = SignalTypeToString(sigtype);
		result["variant"] = (sigvar == SIG_SEMAPHORE) ? "semaphore" : "electric";
		result["two_way"] = two_way;

		/* Return track info */
		switch (track) {
			case TRACK_X: result["track"] = "x"; break;
			case TRACK_Y: result["track"] = "y"; break;
			case TRACK_UPPER: result["track"] = "upper"; break;
			case TRACK_LOWER: result["track"] = "lower"; break;
			case TRACK_LEFT: result["track"] = "left"; break;
			case TRACK_RIGHT: result["track"] = "right"; break;
			default: result["track"] = static_cast<int>(track); break;
		}
	} else {
		result["error"] = GetCommandErrorMessage(cost);
	}

	return result;
}

/**
 * Handler for rail.removeSignal - remove a rail signal.
 *
 * Parameters:
 *   tile: Tile index (optional if x,y provided)
 *   x, y: Tile coordinates (optional if tile provided)
 *   track: Track to remove signal from (x, y, upper, lower, left, right)
 *   company: Company ID (default: 0)
 *
 * Returns:
 *   success: Whether the signal was removed
 *   tile: The tile where signal was removed
 *   refund: The refund amount
 */
static nlohmann::json HandleRailRemoveSignal(const nlohmann::json &params)
{
	/* Get tile */
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

	/* Get track */
	if (!params.contains("track")) {
		throw std::runtime_error("Missing required parameter: track (x, y, upper, lower, left, right)");
	}
	Track track = ParseTrack(params["track"]);

	/* Set company context */
	CompanyID company = static_cast<CompanyID>(params.value("company", 0));
	Backup<CompanyID> cur_company(_current_company, company);

	DoCommandFlags flags;
	flags.Set(DoCommandFlag::Execute);

	CommandCost cost = Command<CMD_REMOVE_SINGLE_SIGNAL>::Do(flags, tile, track);

	cur_company.Restore();

	nlohmann::json result;
	result["tile"] = tile.base();
	result["x"] = TileX(tile);
	result["y"] = TileY(tile);
	result["success"] = cost.Succeeded();

	/* Return track info */
	switch (track) {
		case TRACK_X: result["track"] = "x"; break;
		case TRACK_Y: result["track"] = "y"; break;
		case TRACK_UPPER: result["track"] = "upper"; break;
		case TRACK_LOWER: result["track"] = "lower"; break;
		case TRACK_LEFT: result["track"] = "left"; break;
		case TRACK_RIGHT: result["track"] = "right"; break;
		default: result["track"] = static_cast<int>(track); break;
	}

	if (cost.Succeeded()) {
		result["refund"] = -cost.GetCost().base(); /* Cost is negative for removal */
	} else {
		result["error"] = GetCommandErrorMessage(cost);
	}

	return result;
}

/**
 * Handler for rail.buildTrackLine - Build a line of track between two points.
 *
 * This is a convenience command that builds a straight line of track from
 * start to end, automatically determining the correct track type based on
 * the direction of travel.
 *
 * Parameters:
 *   start_x, start_y: Starting coordinates
 *   end_x, end_y: Ending coordinates
 *   rail_type: Rail type to build (default: 0)
 *   company: Company ID (default: 0)
 *
 * Returns:
 *   success: Whether the track line was built
 *   segments: Array of track segments built
 *   total_cost: Total cost of building
 */
static nlohmann::json HandleRailBuildTrackLine(const nlohmann::json &params)
{
	if (!params.contains("start_x") || !params.contains("start_y") ||
	    !params.contains("end_x") || !params.contains("end_y")) {
		throw std::runtime_error("Missing required parameters: start_x, start_y, end_x, end_y");
	}

	uint start_x = params["start_x"].get<uint>();
	uint start_y = params["start_y"].get<uint>();
	uint end_x = params["end_x"].get<uint>();
	uint end_y = params["end_y"].get<uint>();

	if (start_x >= Map::SizeX() || start_y >= Map::SizeY() ||
	    end_x >= Map::SizeX() || end_y >= Map::SizeY()) {
		throw std::runtime_error("Coordinates out of bounds");
	}

	TileIndex start_tile = TileXY(start_x, start_y);
	TileIndex end_tile = TileXY(end_x, end_y);

	RailType railtype = static_cast<RailType>(params.value("rail_type", 0));
	bool auto_remove_signals = params.value("auto_remove_signals", false);
	bool fail_on_obstacle = params.value("fail_on_obstacle", true);

	/* Set company context */
	CompanyID company = static_cast<CompanyID>(params.value("company", 0));
	Backup<CompanyID> cur_company(_current_company, company);

	DoCommandFlags flags;
	flags.Set(DoCommandFlag::Execute);

	nlohmann::json segments = nlohmann::json::array();
	bool all_success = true;

	/* Determine track type based on direction */
	int dx = (int)end_x - (int)start_x;
	int dy = (int)end_y - (int)start_y;

	/* Build horizontal segments (TRACK_X goes NE-SW, parallel to x-axis) */
	/* Build vertical segments (TRACK_Y goes NW-SE, parallel to y-axis) */
	if (dx != 0 && dy == 0) {
		/* Pure horizontal line - use TRACK_X */
		CommandCost cost = Command<CMD_BUILD_RAILROAD_TRACK>::Do(flags, end_tile, start_tile, railtype,
			TRACK_X, auto_remove_signals, fail_on_obstacle);

		nlohmann::json seg;
		seg["start_tile"] = start_tile.base();
		seg["end_tile"] = end_tile.base();
		seg["track"] = "x";
		seg["success"] = cost.Succeeded();
		seg["cost"] = cost.GetCost().base();
		segments.push_back(seg);

		if (!cost.Succeeded()) {
			all_success = false;
		}
	} else if (dx == 0 && dy != 0) {
		/* Pure vertical line - use TRACK_Y */
		CommandCost cost = Command<CMD_BUILD_RAILROAD_TRACK>::Do(flags, end_tile, start_tile, railtype,
			TRACK_Y, auto_remove_signals, fail_on_obstacle);

		nlohmann::json seg;
		seg["start_tile"] = start_tile.base();
		seg["end_tile"] = end_tile.base();
		seg["track"] = "y";
		seg["success"] = cost.Succeeded();
		seg["cost"] = cost.GetCost().base();
		segments.push_back(seg);

		if (!cost.Succeeded()) {
			all_success = false;
		}
	} else if (dx != 0 && dy != 0) {
		/* Diagonal or L-shaped - build as two segments */
		/* First build horizontal segment */
		TileIndex mid_tile = TileXY(end_x, start_y);

		CommandCost cost1 = Command<CMD_BUILD_RAILROAD_TRACK>::Do(flags, mid_tile, start_tile, railtype,
			TRACK_X, auto_remove_signals, fail_on_obstacle);

		nlohmann::json seg1;
		seg1["start_tile"] = start_tile.base();
		seg1["end_tile"] = mid_tile.base();
		seg1["track"] = "x";
		seg1["success"] = cost1.Succeeded();
		seg1["cost"] = cost1.GetCost().base();
		segments.push_back(seg1);

		if (!cost1.Succeeded()) {
			all_success = false;
		}

		/* Build connecting piece at the corner */
		Track corner_track = (dx > 0 && dy > 0) ? TRACK_LOWER :
		                     (dx > 0 && dy < 0) ? TRACK_UPPER :
		                     (dx < 0 && dy > 0) ? TRACK_RIGHT : TRACK_LEFT;

		CommandCost cost_corner = Command<CMD_BUILD_SINGLE_RAIL>::Do(flags, mid_tile, railtype,
			corner_track, auto_remove_signals);

		nlohmann::json seg_corner;
		seg_corner["tile"] = mid_tile.base();
		seg_corner["track"] = (corner_track == TRACK_LOWER) ? "lower" :
		                      (corner_track == TRACK_UPPER) ? "upper" :
		                      (corner_track == TRACK_RIGHT) ? "right" : "left";
		seg_corner["success"] = cost_corner.Succeeded();
		seg_corner["cost"] = cost_corner.GetCost().base();
		segments.push_back(seg_corner);

		if (!cost_corner.Succeeded()) {
			all_success = false;
		}

		/* Build vertical segment */
		CommandCost cost2 = Command<CMD_BUILD_RAILROAD_TRACK>::Do(flags, end_tile, mid_tile, railtype,
			TRACK_Y, auto_remove_signals, fail_on_obstacle);

		nlohmann::json seg2;
		seg2["start_tile"] = mid_tile.base();
		seg2["end_tile"] = end_tile.base();
		seg2["track"] = "y";
		seg2["success"] = cost2.Succeeded();
		seg2["cost"] = cost2.GetCost().base();
		segments.push_back(seg2);

		if (!cost2.Succeeded()) {
			all_success = false;
		}
	}

	cur_company.Restore();

	if (all_success) {
		RpcRecordActivity(start_tile, "rail.buildTrackLine");
		RpcRecordActivity(end_tile, "rail.buildTrackLine");
	}

	nlohmann::json result;
	result["success"] = all_success;
	result["segments"] = segments;
	result["start_x"] = start_x;
	result["start_y"] = start_y;
	result["end_x"] = end_x;
	result["end_y"] = end_y;

	return result;
}

/**
 * Handler for rail.signalLine - Build signals along an existing track.
 *
 * Parameters:
 *   start_x, start_y: Starting coordinates
 *   end_x, end_y: Ending coordinates
 *   track: Track to signal (x, y, upper, lower, left, right)
 *   signal_type: Type of signal (block, entry, exit, combo, pbs, pbs_oneway)
 *   variant: electric or semaphore (default: electric)
 *   signal_density: Spacing between signals (default: game setting)
 *   company: Company ID (default: 0)
 *
 * Returns:
 *   success: Whether signals were built
 *   tile: Starting tile
 *   end_tile: Ending tile
 *   cost: Total cost
 */
static nlohmann::json HandleRailSignalLine(const nlohmann::json &params)
{
	if (!params.contains("start_x") || !params.contains("start_y") ||
	    !params.contains("end_x") || !params.contains("end_y")) {
		throw std::runtime_error("Missing required parameters: start_x, start_y, end_x, end_y");
	}

	uint start_x = params["start_x"].get<uint>();
	uint start_y = params["start_y"].get<uint>();
	uint end_x = params["end_x"].get<uint>();
	uint end_y = params["end_y"].get<uint>();

	if (start_x >= Map::SizeX() || start_y >= Map::SizeY() ||
	    end_x >= Map::SizeX() || end_y >= Map::SizeY()) {
		throw std::runtime_error("Coordinates out of bounds");
	}

	TileIndex start_tile = TileXY(start_x, start_y);
	TileIndex end_tile = TileXY(end_x, end_y);

	/* Determine track type */
	Track track = TRACK_X;
	if (params.contains("track")) {
		track = ParseTrack(params["track"]);
	} else {
		/* Auto-detect based on direction */
		int dx = (int)end_x - (int)start_x;
		int dy = (int)end_y - (int)start_y;
		if (dx != 0 && dy == 0) {
			track = TRACK_X;
		} else if (dx == 0 && dy != 0) {
			track = TRACK_Y;
		} else {
			/* Diagonal - use the track that matches the direction */
			if (abs(dx) > abs(dy)) {
				track = TRACK_X;
			} else {
				track = TRACK_Y;
			}
		}
	}

	/* Signal type */
	SignalType sigtype = SIGTYPE_PBS_ONEWAY;  /* Default to PBS one-way for safety */
	if (params.contains("signal_type")) {
		sigtype = ParseSignalType(params["signal_type"]);
	}

	/* Signal variant */
	SignalVariant sigvar = SIG_ELECTRIC;
	if (params.contains("variant")) {
		std::string var_str = params["variant"].get<std::string>();
		if (var_str == "semaphore" || var_str == "sem") {
			sigvar = SIG_SEMAPHORE;
		}
	}

	/* Signal density - 0 means use game default */
	uint8_t signal_density = params.value("signal_density", 0);
	if (signal_density == 0) {
		signal_density = 4;  /* Reasonable default */
	}

	/* mode=false means build signals in both directions (two-way) */
	/* mode=true means build signals in one direction only */
	bool mode = params.value("one_direction", true);

	/* autofill=true makes it follow the track automatically */
	bool autofill = params.value("autofill", true);

	/* minimise_gaps helps with placing signals near stations */
	bool minimise_gaps = params.value("minimise_gaps", true);

	/* Set company context */
	CompanyID company = static_cast<CompanyID>(params.value("company", 0));
	Backup<CompanyID> cur_company(_current_company, company);

	DoCommandFlags flags;
	flags.Set(DoCommandFlag::Execute);

	CommandCost cost = Command<CMD_BUILD_SIGNAL_TRACK>::Do(flags, start_tile, end_tile, track,
		sigtype, sigvar, mode, autofill, minimise_gaps, signal_density);

	cur_company.Restore();

	if (cost.Succeeded()) {
		RpcRecordActivity(start_tile, "rail.signalLine");
		RpcRecordActivity(end_tile, "rail.signalLine");
	}

	nlohmann::json result;
	result["success"] = cost.Succeeded();
	result["start_tile"] = start_tile.base();
	result["end_tile"] = end_tile.base();
	result["start_x"] = start_x;
	result["start_y"] = start_y;
	result["end_x"] = end_x;
	result["end_y"] = end_y;
	result["cost"] = cost.GetCost().base();

	/* Return what was built */
	switch (track) {
		case TRACK_X: result["track"] = "x"; break;
		case TRACK_Y: result["track"] = "y"; break;
		case TRACK_UPPER: result["track"] = "upper"; break;
		case TRACK_LOWER: result["track"] = "lower"; break;
		case TRACK_LEFT: result["track"] = "left"; break;
		case TRACK_RIGHT: result["track"] = "right"; break;
		default: result["track"] = static_cast<int>(track); break;
	}
	result["signal_type"] = SignalTypeToString(sigtype);
	result["variant"] = (sigvar == SIG_SEMAPHORE) ? "semaphore" : "electric";
	result["signal_density"] = signal_density;

	if (cost.Failed()) {
		result["error"] = GetCommandErrorMessage(cost);
	}

	return result;
}

/**
 * Handler for marine.buildDock - Build a ship dock.
 *
 * Parameters:
 *   tile: Tile index (optional if x,y provided)
 *   x, y: Tile coordinates (optional if tile provided)
 *   station_id: Station to join (optional, creates new if not specified)
 *   adjacent: Allow adjacent station joining (default: true)
 *   company: Company ID (default: 0)
 *
 * Returns:
 *   success: Whether the dock was built
 *   tile: The tile where dock was built
 *   cost: The cost of building the dock
 */
static nlohmann::json HandleMarineBuildDock(const nlohmann::json &params)
{
	/* Get tile */
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

	/* Station to join */
	StationID station_to_join = StationID::Invalid();
	if (params.contains("station_id")) {
		station_to_join = static_cast<StationID>(params["station_id"].get<int>());
	}

	/* Adjacent joining */
	bool adjacent = params.value("adjacent", true);

	/* Set company context */
	CompanyID company = static_cast<CompanyID>(params.value("company", 0));
	Backup<CompanyID> cur_company(_current_company, company);

	DoCommandFlags flags;
	flags.Set(DoCommandFlag::Execute);

	CommandCost cost = Command<CMD_BUILD_DOCK>::Do(flags, tile, station_to_join, adjacent);

	cur_company.Restore();

	if (cost.Succeeded()) {
		RpcRecordActivity(tile, "marine.buildDock");
	}

	nlohmann::json result;
	result["tile"] = tile.base();
	result["x"] = TileX(tile);
	result["y"] = TileY(tile);
	result["success"] = cost.Succeeded();
	result["cost"] = cost.GetCost().base();

	if (cost.Failed()) {
		result["error"] = GetCommandErrorMessage(cost);
	}

	return result;
}

/**
 * Handler for marine.buildDepot - Build a ship depot.
 *
 * Parameters:
 *   tile: Tile index (optional if x,y provided)
 *   x, y: Tile coordinates (optional if tile provided)
 *   axis: Depot axis orientation (x or y)
 *   company: Company ID (default: 0)
 *
 * Returns:
 *   success: Whether the depot was built
 *   tile: The tile where depot was built
 *   axis: The axis orientation
 *   cost: The cost of building the depot
 */
static nlohmann::json HandleMarineBuildDepot(const nlohmann::json &params)
{
	/* Get tile */
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

	/* Get axis */
	Axis axis = AXIS_X;
	if (params.contains("axis")) {
		std::string axis_str = params["axis"].get<std::string>();
		if (axis_str == "x") {
			axis = AXIS_X;
		} else if (axis_str == "y") {
			axis = AXIS_Y;
		} else {
			throw std::runtime_error("Invalid axis: use x or y");
		}
	}

	/* Set company context */
	CompanyID company = static_cast<CompanyID>(params.value("company", 0));
	Backup<CompanyID> cur_company(_current_company, company);

	DoCommandFlags flags;
	flags.Set(DoCommandFlag::Execute);

	CommandCost cost = Command<CMD_BUILD_SHIP_DEPOT>::Do(flags, tile, axis);

	cur_company.Restore();

	if (cost.Succeeded()) {
		RpcRecordActivity(tile, "marine.buildDepot");
	}

	nlohmann::json result;
	result["tile"] = tile.base();
	result["x"] = TileX(tile);
	result["y"] = TileY(tile);
	result["axis"] = (axis == AXIS_X) ? "x" : "y";
	result["success"] = cost.Succeeded();
	result["cost"] = cost.GetCost().base();

	if (cost.Failed()) {
		result["error"] = GetCommandErrorMessage(cost);
	}

	return result;
}

/**
 * Parse an airport type string or number to AirportTypes enum.
 */
static uint8_t ParseAirportType(const nlohmann::json &value)
{
	if (value.is_number()) {
		return value.get<uint8_t>();
	}

	std::string str = value.get<std::string>();
	if (str == "small") return AT_SMALL;
	if (str == "large" || str == "city") return AT_LARGE;
	if (str == "heliport") return AT_HELIPORT;
	if (str == "metropolitan" || str == "metro") return AT_METROPOLITAN;
	if (str == "international" || str == "intl") return AT_INTERNATIONAL;
	if (str == "commuter") return AT_COMMUTER;
	if (str == "helidepot") return AT_HELIDEPOT;
	if (str == "intercontinental" || str == "intercon") return AT_INTERCON;
	if (str == "helistation") return AT_HELISTATION;

	throw std::runtime_error("Invalid airport type: use small, large, heliport, metropolitan, international, commuter, helidepot, intercontinental, or helistation");
}

/**
 * Get airport type name as string.
 */
static const char *AirportTypeToString(uint8_t type)
{
	switch (type) {
		case AT_SMALL: return "small";
		case AT_LARGE: return "large";
		case AT_HELIPORT: return "heliport";
		case AT_METROPOLITAN: return "metropolitan";
		case AT_INTERNATIONAL: return "international";
		case AT_COMMUTER: return "commuter";
		case AT_HELIDEPOT: return "helidepot";
		case AT_INTERCON: return "intercontinental";
		case AT_HELISTATION: return "helistation";
		default: return "unknown";
	}
}

/**
 * Handler for airport.build - Build an airport.
 *
 * Parameters:
 *   tile: Tile index (optional if x,y provided)
 *   x, y: Tile coordinates - northwest corner (optional if tile provided)
 *   type: Airport type (small, large, heliport, metropolitan, international, commuter, helidepot, intercontinental, helistation)
 *   layout: Airport layout variant (default: 0)
 *   station_id: Station to join (optional, creates new if not specified)
 *   adjacent: Allow adjacent station joining (default: true)
 *   company: Company ID (default: 0)
 *
 * Returns:
 *   success: Whether the airport was built
 *   tile: The tile where airport was built
 *   type: The airport type
 *   cost: The cost of building the airport
 */
static nlohmann::json HandleAirportBuild(const nlohmann::json &params)
{
	/* Get tile */
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

	/* Get airport type - default to small */
	uint8_t airport_type = AT_SMALL;
	if (params.contains("type")) {
		airport_type = ParseAirportType(params["type"]);
	}

	/* Layout variant */
	uint8_t layout = params.value("layout", 0);

	/* Station to join */
	StationID station_to_join = StationID::Invalid();
	if (params.contains("station_id")) {
		station_to_join = static_cast<StationID>(params["station_id"].get<int>());
	}

	/* Adjacent joining */
	bool adjacent = params.value("adjacent", true);

	/* Set company context */
	CompanyID company = static_cast<CompanyID>(params.value("company", 0));
	Backup<CompanyID> cur_company(_current_company, company);

	DoCommandFlags flags;
	flags.Set(DoCommandFlag::Execute);

	CommandCost cost = Command<CMD_BUILD_AIRPORT>::Do(flags, tile, airport_type, layout, station_to_join, adjacent);

	cur_company.Restore();

	if (cost.Succeeded()) {
		RpcRecordActivity(tile, "airport.build");
	}

	nlohmann::json result;
	result["tile"] = tile.base();
	result["x"] = TileX(tile);
	result["y"] = TileY(tile);
	result["type"] = AirportTypeToString(airport_type);
	result["success"] = cost.Succeeded();
	result["cost"] = cost.GetCost().base();

	if (cost.Failed()) {
		result["error"] = GetCommandErrorMessage(cost);
	}

	return result;
}

/**
 * Handler for bridge.list - List available bridge types.
 *
 * Parameters:
 *   length: Required bridge length in tiles (optional, filters by availability)
 *
 * Returns:
 *   bridges: Array of available bridge types with specs
 */
static nlohmann::json HandleBridgeList(const nlohmann::json &params)
{
	uint min_length = params.value("length", 0);

	nlohmann::json bridges = nlohmann::json::array();

	for (BridgeType i = 0; i < MAX_BRIDGES; i++) {
		const BridgeSpec *spec = GetBridgeSpec(i);
		if (spec == nullptr || spec->avail_year > TimerGameCalendar::year) continue;

		/* Check length constraints if specified */
		if (min_length > 0) {
			if (min_length < spec->min_length || min_length > spec->max_length) continue;
		}

		nlohmann::json bridge;
		bridge["id"] = i;
		bridge["name"] = StrMakeValid(GetString(spec->material));
		bridge["min_length"] = spec->min_length;
		bridge["max_length"] = spec->max_length == UINT16_MAX ? 0 : spec->max_length;  /* 0 = unlimited */
		bridge["speed"] = spec->speed;  /* km/h */
		bridge["available_year"] = spec->avail_year.base();

		bridges.push_back(bridge);
	}

	nlohmann::json result;
	result["bridges"] = bridges;
	return result;
}

/**
 * Handler for rail.buildBridge - Build a rail bridge.
 *
 * Parameters:
 *   start_x, start_y: Start tile coordinates (one end of bridge)
 *   end_x, end_y: End tile coordinates (other end of bridge)
 *   bridge_type: Bridge type ID (0-12, use bridge.list to see options)
 *   rail_type: Rail type (default: 0)
 *   company: Company ID (default: 0)
 *
 * Returns:
 *   success: Whether the bridge was built
 *   start_tile, end_tile: The bridge endpoints
 *   cost: Construction cost
 */
static nlohmann::json HandleRailBuildBridge(const nlohmann::json &params)
{
	if (!params.contains("start_x") || !params.contains("start_y") ||
	    !params.contains("end_x") || !params.contains("end_y")) {
		throw std::runtime_error("Missing required parameters: start_x, start_y, end_x, end_y");
	}

	uint start_x = params["start_x"].get<uint>();
	uint start_y = params["start_y"].get<uint>();
	uint end_x = params["end_x"].get<uint>();
	uint end_y = params["end_y"].get<uint>();

	if (start_x >= Map::SizeX() || start_y >= Map::SizeY() ||
	    end_x >= Map::SizeX() || end_y >= Map::SizeY()) {
		throw std::runtime_error("Coordinates out of bounds");
	}

	TileIndex start_tile = TileXY(start_x, start_y);
	TileIndex end_tile = TileXY(end_x, end_y);

	BridgeType bridge_type = params.value("bridge_type", 0);
	if (bridge_type >= MAX_BRIDGES) {
		throw std::runtime_error("Invalid bridge_type: must be 0-12");
	}

	RailType railtype = static_cast<RailType>(params.value("rail_type", 0));

	/* Set company context */
	CompanyID company = static_cast<CompanyID>(params.value("company", 0));
	Backup<CompanyID> cur_company(_current_company, company);

	DoCommandFlags flags;
	flags.Set(DoCommandFlag::Execute);

	CommandCost cost = Command<CMD_BUILD_BRIDGE>::Do(flags, end_tile, start_tile,
		TRANSPORT_RAIL, bridge_type, railtype);

	cur_company.Restore();

	if (cost.Succeeded()) {
		RpcRecordActivity(start_tile, "rail.buildBridge");
		RpcRecordActivity(end_tile, "rail.buildBridge");
	}

	nlohmann::json result;
	result["success"] = cost.Succeeded();
	result["start_tile"] = start_tile.base();
	result["end_tile"] = end_tile.base();
	result["start_x"] = start_x;
	result["start_y"] = start_y;
	result["end_x"] = end_x;
	result["end_y"] = end_y;
	result["cost"] = cost.GetCost().base();

	if (cost.Failed()) {
		result["error"] = GetCommandErrorMessage(cost);
	}

	return result;
}

/**
 * Handler for road.buildBridge - Build a road bridge.
 *
 * Parameters:
 *   start_x, start_y: Start tile coordinates (one end of bridge)
 *   end_x, end_y: End tile coordinates (other end of bridge)
 *   bridge_type: Bridge type ID (0-12, use bridge.list to see options)
 *   road_type: Road type (default: 0)
 *   company: Company ID (default: 0)
 *
 * Returns:
 *   success: Whether the bridge was built
 *   start_tile, end_tile: The bridge endpoints
 *   cost: Construction cost
 */
static nlohmann::json HandleRoadBuildBridge(const nlohmann::json &params)
{
	if (!params.contains("start_x") || !params.contains("start_y") ||
	    !params.contains("end_x") || !params.contains("end_y")) {
		throw std::runtime_error("Missing required parameters: start_x, start_y, end_x, end_y");
	}

	uint start_x = params["start_x"].get<uint>();
	uint start_y = params["start_y"].get<uint>();
	uint end_x = params["end_x"].get<uint>();
	uint end_y = params["end_y"].get<uint>();

	if (start_x >= Map::SizeX() || start_y >= Map::SizeY() ||
	    end_x >= Map::SizeX() || end_y >= Map::SizeY()) {
		throw std::runtime_error("Coordinates out of bounds");
	}

	TileIndex start_tile = TileXY(start_x, start_y);
	TileIndex end_tile = TileXY(end_x, end_y);

	BridgeType bridge_type = params.value("bridge_type", 0);
	if (bridge_type >= MAX_BRIDGES) {
		throw std::runtime_error("Invalid bridge_type: must be 0-12");
	}

	RoadType roadtype = static_cast<RoadType>(params.value("road_type", 0));

	/* Set company context */
	CompanyID company = static_cast<CompanyID>(params.value("company", 0));
	Backup<CompanyID> cur_company(_current_company, company);

	DoCommandFlags flags;
	flags.Set(DoCommandFlag::Execute);

	CommandCost cost = Command<CMD_BUILD_BRIDGE>::Do(flags, end_tile, start_tile,
		TRANSPORT_ROAD, bridge_type, roadtype);

	cur_company.Restore();

	if (cost.Succeeded()) {
		RpcRecordActivity(start_tile, "road.buildBridge");
		RpcRecordActivity(end_tile, "road.buildBridge");
	}

	nlohmann::json result;
	result["success"] = cost.Succeeded();
	result["start_tile"] = start_tile.base();
	result["end_tile"] = end_tile.base();
	result["start_x"] = start_x;
	result["start_y"] = start_y;
	result["end_x"] = end_x;
	result["end_y"] = end_y;
	result["cost"] = cost.GetCost().base();

	if (cost.Failed()) {
		result["error"] = GetCommandErrorMessage(cost);
	}

	return result;
}

/**
 * Handler for rail.buildTunnel - Build a rail tunnel.
 *
 * The tunnel automatically extends through the mountain to the other side.
 * The start tile must be at the base of a slope facing into the mountain.
 *
 * Parameters:
 *   x, y: Start tile coordinates (tunnel entrance)
 *   rail_type: Rail type (default: 0)
 *   company: Company ID (default: 0)
 *
 * Returns:
 *   success: Whether the tunnel was built
 *   start_tile: Tunnel entrance
 *   end_tile: Tunnel exit (automatically determined)
 *   cost: Construction cost
 */
static nlohmann::json HandleRailBuildTunnel(const nlohmann::json &params)
{
	if (!params.contains("x") || !params.contains("y")) {
		throw std::runtime_error("Missing required parameters: x, y");
	}

	uint x = params["x"].get<uint>();
	uint y = params["y"].get<uint>();

	if (x >= Map::SizeX() || y >= Map::SizeY()) {
		throw std::runtime_error("Coordinates out of bounds");
	}

	TileIndex start_tile = TileXY(x, y);
	RailType railtype = static_cast<RailType>(params.value("rail_type", 0));

	/* Set company context */
	CompanyID company = static_cast<CompanyID>(params.value("company", 0));
	Backup<CompanyID> cur_company(_current_company, company);

	DoCommandFlags flags;
	flags.Set(DoCommandFlag::Execute);

	CommandCost cost = Command<CMD_BUILD_TUNNEL>::Do(flags, start_tile, TRANSPORT_RAIL, railtype);

	cur_company.Restore();

	/* Get the end tile - it's stored in a global after successful build */
	TileIndex end_tile = _build_tunnel_endtile;

	if (cost.Succeeded()) {
		RpcRecordActivity(start_tile, "rail.buildTunnel");
		if (IsValidTile(end_tile)) {
			RpcRecordActivity(end_tile, "rail.buildTunnel");
		}
	}

	nlohmann::json result;
	result["success"] = cost.Succeeded();
	result["start_tile"] = start_tile.base();
	result["start_x"] = x;
	result["start_y"] = y;
	result["cost"] = cost.GetCost().base();

	if (cost.Succeeded() && IsValidTile(end_tile)) {
		result["end_tile"] = end_tile.base();
		result["end_x"] = TileX(end_tile);
		result["end_y"] = TileY(end_tile);
	}

	if (cost.Failed()) {
		result["error"] = GetCommandErrorMessage(cost);
	}

	return result;
}

/**
 * Handler for road.buildTunnel - Build a road tunnel.
 *
 * The tunnel automatically extends through the mountain to the other side.
 * The start tile must be at the base of a slope facing into the mountain.
 *
 * Parameters:
 *   x, y: Start tile coordinates (tunnel entrance)
 *   road_type: Road type (default: 0)
 *   company: Company ID (default: 0)
 *
 * Returns:
 *   success: Whether the tunnel was built
 *   start_tile: Tunnel entrance
 *   end_tile: Tunnel exit (automatically determined)
 *   cost: Construction cost
 */
static nlohmann::json HandleRoadBuildTunnel(const nlohmann::json &params)
{
	if (!params.contains("x") || !params.contains("y")) {
		throw std::runtime_error("Missing required parameters: x, y");
	}

	uint x = params["x"].get<uint>();
	uint y = params["y"].get<uint>();

	if (x >= Map::SizeX() || y >= Map::SizeY()) {
		throw std::runtime_error("Coordinates out of bounds");
	}

	TileIndex start_tile = TileXY(x, y);
	RoadType roadtype = static_cast<RoadType>(params.value("road_type", 0));

	/* Set company context */
	CompanyID company = static_cast<CompanyID>(params.value("company", 0));
	Backup<CompanyID> cur_company(_current_company, company);

	DoCommandFlags flags;
	flags.Set(DoCommandFlag::Execute);

	CommandCost cost = Command<CMD_BUILD_TUNNEL>::Do(flags, start_tile, TRANSPORT_ROAD, roadtype);

	cur_company.Restore();

	/* Get the end tile - it's stored in a global after successful build */
	TileIndex end_tile = _build_tunnel_endtile;

	if (cost.Succeeded()) {
		RpcRecordActivity(start_tile, "road.buildTunnel");
		if (IsValidTile(end_tile)) {
			RpcRecordActivity(end_tile, "road.buildTunnel");
		}
	}

	nlohmann::json result;
	result["success"] = cost.Succeeded();
	result["start_tile"] = start_tile.base();
	result["start_x"] = x;
	result["start_y"] = y;
	result["cost"] = cost.GetCost().base();

	if (cost.Succeeded() && IsValidTile(end_tile)) {
		result["end_tile"] = end_tile.base();
		result["end_x"] = TileX(end_tile);
		result["end_y"] = TileY(end_tile);
	}

	if (cost.Failed()) {
		result["error"] = GetCommandErrorMessage(cost);
	}

	return result;
}

void RpcRegisterInfraHandlers(RpcServer &server)
{
	server.RegisterHandler("tile.getRoadInfo", HandleTileGetRoadInfo);
	server.RegisterHandler("road.build", HandleRoadBuild);
	server.RegisterHandler("road.buildDepot", HandleRoadBuildDepot);
	server.RegisterHandler("road.buildStop", HandleRoadBuildStop);
	server.RegisterHandler("road.buildLine", HandleRoadBuildLine);
	server.RegisterHandler("road.connect", HandleRoadConnect);
	server.RegisterHandler("rail.buildTrack", HandleRailBuildTrack);
	server.RegisterHandler("rail.buildDepot", HandleRailBuildDepot);
	server.RegisterHandler("rail.buildStation", HandleRailBuildStation);
	server.RegisterHandler("rail.buildSignal", HandleRailBuildSignal);
	server.RegisterHandler("rail.removeSignal", HandleRailRemoveSignal);
	server.RegisterHandler("rail.buildTrackLine", HandleRailBuildTrackLine);
	server.RegisterHandler("rail.signalLine", HandleRailSignalLine);
	server.RegisterHandler("marine.buildDock", HandleMarineBuildDock);
	server.RegisterHandler("marine.buildDepot", HandleMarineBuildDepot);
	server.RegisterHandler("airport.build", HandleAirportBuild);
	server.RegisterHandler("bridge.list", HandleBridgeList);
	server.RegisterHandler("rail.buildBridge", HandleRailBuildBridge);
	server.RegisterHandler("road.buildBridge", HandleRoadBuildBridge);
	server.RegisterHandler("rail.buildTunnel", HandleRailBuildTunnel);
	server.RegisterHandler("road.buildTunnel", HandleRoadBuildTunnel);
}
