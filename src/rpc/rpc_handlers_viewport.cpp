/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file rpc_handlers_viewport.cpp JSON-RPC handlers for viewport/camera control and activity tracking. */

#include "../stdafx.h"
#include "rpc_handlers.h"
#include "../viewport_func.h"
#include "../window_func.h"
#include "../vehicle_base.h"
#include "../tile_map.h"
#include "../strings_func.h"
#include "../string_func.h"
#include "../table/strings.h"

#include <deque>
#include <chrono>
#include <mutex>

#include "../safeguards.h"

/** Activity record for tracking where actions happen. */
struct ActivityRecord {
	TileIndex tile;
	std::string action;
	std::chrono::steady_clock::time_point timestamp;
};

/** Maximum number of activity records to keep. */
static constexpr size_t MAX_ACTIVITY_RECORDS = 100;

/** How long activity records are considered "recent" (in seconds). */
static constexpr int ACTIVITY_WINDOW_SECONDS = 60;

/** Ring buffer of recent activity. */
static std::deque<ActivityRecord> _activity_log;

/** Mutex for thread-safe activity log access. */
static std::mutex _activity_mutex;

/**
 * Record an activity at a tile location.
 * Call this from action handlers to track where changes are happening.
 */
void RpcRecordActivity(TileIndex tile, const std::string &action)
{
	if (!IsValidTile(tile)) return;

	std::lock_guard<std::mutex> lock(_activity_mutex);

	ActivityRecord record;
	record.tile = tile;
	record.action = action;
	record.timestamp = std::chrono::steady_clock::now();

	_activity_log.push_back(record);

	/* Trim old records */
	while (_activity_log.size() > MAX_ACTIVITY_RECORDS) {
		_activity_log.pop_front();
	}
}

/**
 * Record activity at x,y coordinates.
 */
void RpcRecordActivityXY(uint x, uint y, const std::string &action)
{
	if (x < Map::SizeX() && y < Map::SizeY()) {
		RpcRecordActivity(TileXY(x, y), action);
	}
}

/**
 * Handler for viewport.goto - Scroll the main viewport to a tile.
 *
 * Parameters:
 *   tile: Tile index to scroll to (optional if x,y provided)
 *   x, y: Tile coordinates to scroll to (optional if tile provided)
 *   instant: Whether to jump instantly (default: false for smooth scroll)
 *
 * Returns:
 *   success: Whether the scroll was initiated
 *   tile: The tile scrolled to
 */
static nlohmann::json HandleViewportGoto(const nlohmann::json &params)
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
		throw std::runtime_error("Missing required parameter: tile or x,y coordinates");
	}

	if (!IsValidTile(tile)) {
		throw std::runtime_error("Invalid tile");
	}

	bool instant = params.value("instant", false);

	/* Scroll the main window to the tile */
	bool success = ScrollMainWindowToTile(tile, instant);

	nlohmann::json result;
	result["success"] = success;
	result["tile"] = tile.base();
	result["x"] = TileX(tile);
	result["y"] = TileY(tile);

	return result;
}

/**
 * Handler for viewport.follow - Follow a vehicle with the main viewport.
 *
 * Parameters:
 *   vehicle_id: The vehicle ID to follow
 *   stop: Set to true to stop following (optional)
 *
 * Returns:
 *   success: Whether following was initiated
 *   vehicle_id: The vehicle being followed (or -1 if stopped)
 */
static nlohmann::json HandleViewportFollow(const nlohmann::json &params)
{
	nlohmann::json result;

	if (params.value("stop", false)) {
		/* Stop following - scroll to current position */
		Window *w = GetMainWindow();
		if (w != nullptr && w->viewport != nullptr) {
			/* Reset focus to the current center tile */
			Point pt = GetTileBelowCursor();
			if (IsValidTile(TileVirtXY(pt.x, pt.y))) {
				ScrollMainWindowToTile(TileVirtXY(pt.x, pt.y), true);
			}
		}
		result["success"] = true;
		result["vehicle_id"] = -1;
		result["following"] = false;
		return result;
	}

	if (!params.contains("vehicle_id")) {
		throw std::runtime_error("Missing required parameter: vehicle_id");
	}

	VehicleID vid = static_cast<VehicleID>(params["vehicle_id"].get<int>());
	const Vehicle *v = Vehicle::GetIfValid(vid);
	if (v == nullptr) {
		throw std::runtime_error("Invalid vehicle ID");
	}

	/* Find the main window and set it to follow the vehicle */
	Window *w = GetMainWindow();
	if (w == nullptr) {
		throw std::runtime_error("No main window available");
	}

	/* Scroll to vehicle's current tile and let the game's normal vehicle following work */
	ScrollMainWindowToTile(v->tile, false);

	result["success"] = true;
	result["vehicle_id"] = vid.base();
	result["vehicle_name"] = StrMakeValid(GetString(STR_VEHICLE_NAME, v->index));
	result["tile"] = v->tile.base();

	return result;
}

/**
 * Handler for activity.hotspot - Get the most active area in recent time.
 *
 * Parameters:
 *   seconds: How many seconds back to look (default: 30, max: 60)
 *
 * Returns:
 *   hotspot_tile: The tile with most activity
 *   hotspot_x, hotspot_y: Coordinates of the hotspot
 *   activity_count: Number of actions in the hotspot area
 *   recent_actions: List of recent action locations
 */
static nlohmann::json HandleActivityHotspot(const nlohmann::json &params)
{
	int seconds = (params.is_object() && params.contains("seconds")) ? params["seconds"].get<int>() : 30;
	if (seconds > ACTIVITY_WINDOW_SECONDS) seconds = ACTIVITY_WINDOW_SECONDS;
	if (seconds < 1) seconds = 1;

	auto cutoff = std::chrono::steady_clock::now() - std::chrono::seconds(seconds);

	std::lock_guard<std::mutex> lock(_activity_mutex);

	/* Count activity per tile region (group by 16x16 areas for hotspot detection) */
	std::map<std::pair<int, int>, int> region_counts;
	std::map<std::pair<int, int>, TileIndex> region_tiles;
	nlohmann::json recent_actions = nlohmann::json::array();

	for (auto it = _activity_log.rbegin(); it != _activity_log.rend(); ++it) {
		if (it->timestamp < cutoff) break;

		int region_x = TileX(it->tile) / 16;
		int region_y = TileY(it->tile) / 16;
		auto key = std::make_pair(region_x, region_y);

		region_counts[key]++;
		if (region_tiles.find(key) == region_tiles.end()) {
			region_tiles[key] = it->tile;
		}

		/* Add to recent actions list (limit to 20) */
		if (recent_actions.size() < 20) {
			nlohmann::json action;
			action["tile"] = it->tile.base();
			action["x"] = TileX(it->tile);
			action["y"] = TileY(it->tile);
			action["action"] = it->action;
			recent_actions.push_back(action);
		}
	}

	nlohmann::json result;
	result["seconds"] = seconds;
	result["recent_actions"] = recent_actions;

	if (region_counts.empty()) {
		result["has_activity"] = false;
		result["hotspot_tile"] = 0;
		result["hotspot_x"] = 0;
		result["hotspot_y"] = 0;
		result["activity_count"] = 0;
	} else {
		/* Find the hotspot (region with most activity) */
		auto hotspot = std::max_element(region_counts.begin(), region_counts.end(),
			[](const auto &a, const auto &b) { return a.second < b.second; });

		TileIndex hotspot_tile = region_tiles[hotspot->first];

		result["has_activity"] = true;
		result["hotspot_tile"] = hotspot_tile.base();
		result["hotspot_x"] = TileX(hotspot_tile);
		result["hotspot_y"] = TileY(hotspot_tile);
		result["activity_count"] = hotspot->second;
	}

	return result;
}

/**
 * Handler for activity.clear - Clear the activity log.
 *
 * Returns:
 *   cleared: Number of records cleared
 */
static nlohmann::json HandleActivityClear(const nlohmann::json &)
{
	std::lock_guard<std::mutex> lock(_activity_mutex);
	size_t count = _activity_log.size();
	_activity_log.clear();

	nlohmann::json result;
	result["cleared"] = count;
	return result;
}

void RpcRegisterViewportHandlers(RpcServer &server)
{
	server.RegisterHandler("viewport.goto", HandleViewportGoto);
	server.RegisterHandler("viewport.follow", HandleViewportFollow);
	server.RegisterHandler("activity.hotspot", HandleActivityHotspot);
	server.RegisterHandler("activity.clear", HandleActivityClear);
}
