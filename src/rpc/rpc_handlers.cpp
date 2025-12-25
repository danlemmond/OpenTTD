/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file rpc_handlers.cpp JSON-RPC handler registration for AI agent integration. */

#include "../stdafx.h"
#include "rpc_handlers.h"
#include "../tile_map.h"

#include "../safeguards.h"

/**
 * Convert a TileType to a human-readable string.
 * @param type The tile type to convert.
 * @return String representation of the tile type.
 */
const char *RpcTileTypeToString(TileType type)
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
 * Convert a VehicleType to a human-readable string.
 * @param type The vehicle type to convert.
 * @return String representation of the vehicle type.
 */
const char *RpcVehicleTypeToString(VehicleType type)
{
	switch (type) {
		case VEH_TRAIN: return "train";
		case VEH_ROAD: return "road";
		case VEH_SHIP: return "ship";
		case VEH_AIRCRAFT: return "aircraft";
		default: return "unknown";
	}
}

/**
 * Register all JSON-RPC handlers with the server.
 * Handlers are organized into modules by category:
 * - Query: Read-only data retrieval (game state, vehicles, stations, etc.)
 * - Action: Vehicle and order control commands
 * - Infra: Infrastructure building (roads, rails, stations, depots)
 * - Meta: Game control (new game, etc.)
 * - Viewport: Camera control and activity tracking
 */
void RpcRegisterHandlers(RpcServer &server)
{
	RpcRegisterQueryHandlers(server);
	RpcRegisterActionHandlers(server);
	RpcRegisterInfraHandlers(server);
	RpcRegisterMetaHandlers(server);
	RpcRegisterViewportHandlers(server);
}
