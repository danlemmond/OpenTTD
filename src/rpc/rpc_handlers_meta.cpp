/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file rpc_handlers_meta.cpp JSON-RPC meta handlers for game control. */

#include "../stdafx.h"
#include "rpc_handlers.h"
#include "../genworld.h"

#include "../safeguards.h"

/**
 * Handler for game.newgame - Start a new game with default settings.
 * This triggers world generation and starts a fresh game.
 */
static nlohmann::json HandleGameNewGame(const nlohmann::json &params)
{
	uint32_t seed = GENERATE_NEW_SEED;  /* Random seed */

	if (params.contains("seed")) {
		seed = params["seed"].get<uint32_t>();
	}

	/* Trigger new game generation */
	StartNewGameWithoutGUI(seed);

	nlohmann::json result;
	result["success"] = true;
	result["message"] = "New game generation started";
	if (seed == GENERATE_NEW_SEED) {
		result["seed"] = "random";
	} else {
		result["seed"] = seed;
	}

	return result;
}

void RpcRegisterMetaHandlers(RpcServer &server)
{
	server.RegisterHandler("game.newgame", HandleGameNewGame);
}
