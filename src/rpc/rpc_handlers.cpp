/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file rpc_handlers.cpp JSON-RPC handler registration for AI agent integration. */

#include "../stdafx.h"
#include "rpc_handlers.h"

#include "../safeguards.h"

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
