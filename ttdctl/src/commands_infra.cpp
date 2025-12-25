/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file commands_infra.cpp Infrastructure command handlers for ttdctl. */

#include "cli_common.h"
#include <iostream>

int HandleTileRoadInfo(RpcClient &client, const CliOptions &opts)
{
	try {
		if (opts.args.size() < 2) {
			std::cerr << "Error: coordinates required\n";
			std::cerr << "Usage: ttdctl tile roadinfo <x> <y>\n";
			return 1;
		}

		nlohmann::json params;
		params["x"] = std::stoi(opts.args[0]);
		params["y"] = std::stoi(opts.args[1]);

		auto result = client.Call("tile.getRoadInfo", params);

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		std::cout << "Road/Rail Info at (" << result["x"].get<int>() << ", " << result["y"].get<int>() << ")\n";
		std::cout << "---------------\n";
		std::cout << "Tile type: " << result["tile_type"].get<std::string>() << "\n";

		if (result.contains("road_tile_type")) {
			std::cout << "Road type: " << result["road_tile_type"].get<std::string>() << "\n";
			if (result.contains("road_directions")) {
				std::cout << "Road directions: ";
				for (size_t i = 0; i < result["road_directions"].size(); ++i) {
					if (i > 0) std::cout << ", ";
					std::cout << result["road_directions"][i].get<std::string>();
				}
				std::cout << "\n";
			}
			if (result.contains("valid_depot_orientations") && !result["valid_depot_orientations"].empty()) {
				std::cout << "\nValid depot orientations:\n";
				for (const auto &orient : result["valid_depot_orientations"]) {
					std::cout << "  " << orient["direction"].get<std::string>()
					          << " (" << orient["direction_value"].get<int>() << "): "
					          << orient["description"].get<std::string>() << "\n";
				}
			}
			if (result.contains("depot_direction")) {
				std::cout << "Depot direction: " << result["depot_direction"].get<std::string>() << "\n";
			}
		}

		if (result.contains("tracks")) {
			std::cout << "Tracks: ";
			for (size_t i = 0; i < result["tracks"].size(); ++i) {
				if (i > 0) std::cout << ", ";
				std::cout << result["tracks"][i].get<std::string>();
			}
			std::cout << "\n";
		}

		if (result.contains("station_id")) {
			std::cout << "Station: #" << result["station_id"].get<int>()
			          << " (" << result["station_name"].get<std::string>() << ")\n";
		}

		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}

int HandleRoadBuild(RpcClient &client, const CliOptions &opts)
{
	try {
		if (opts.args.size() < 2) {
			std::cerr << "Error: coordinates required\n";
			std::cerr << "Usage: ttdctl road build <x> <y> --pieces <bits>\n";
			std::cerr << "  Pieces: x, y, all, or ne/se/sw/nw combinations\n";
			return 1;
		}

		nlohmann::json params;
		params["x"] = std::stoi(opts.args[0]);
		params["y"] = std::stoi(opts.args[1]);

		/* Parse options */
		for (size_t i = 2; i < opts.args.size(); ++i) {
			if (opts.args[i] == "--pieces" && i + 1 < opts.args.size()) {
				params["pieces"] = opts.args[++i];
			} else if (opts.args[i] == "--company" && i + 1 < opts.args.size()) {
				params["company"] = std::stoi(opts.args[++i]);
			}
		}

		if (!params.contains("pieces")) {
			std::cerr << "Error: --pieces is required\n";
			std::cerr << "Usage: ttdctl road build <x> <y> --pieces <bits>\n";
			return 1;
		}

		auto result = client.Call("road.build", params);

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		bool success = result["success"].get<bool>();
		if (success) {
			std::cout << "Built road at tile " << result["tile"].get<int>()
			          << " (cost: " << result["cost"].get<int64_t>() << ")\n";
		} else {
			std::cerr << "Failed to build road: " << result["error"].get<std::string>() << "\n";
			return 1;
		}
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}

int HandleRoadBuildDepot(RpcClient &client, const CliOptions &opts)
{
	try {
		if (opts.args.size() < 2) {
			std::cerr << "Error: coordinates required\n";
			std::cerr << "Usage: ttdctl road depot <x> <y> --direction <ne|se|sw|nw>\n";
			return 1;
		}

		nlohmann::json params;
		params["x"] = std::stoi(opts.args[0]);
		params["y"] = std::stoi(opts.args[1]);

		/* Parse options */
		for (size_t i = 2; i < opts.args.size(); ++i) {
			if (opts.args[i] == "--direction" && i + 1 < opts.args.size()) {
				params["direction"] = opts.args[++i];
			} else if (opts.args[i] == "--company" && i + 1 < opts.args.size()) {
				params["company"] = std::stoi(opts.args[++i]);
			}
		}

		if (!params.contains("direction")) {
			std::cerr << "Error: --direction is required\n";
			std::cerr << "Usage: ttdctl road depot <x> <y> --direction <ne|se|sw|nw>\n";
			std::cerr << "  Tip: Use 'ttdctl tile roadinfo <x> <y>' to find valid orientations\n";
			return 1;
		}

		auto result = client.Call("road.buildDepot", params);

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		bool success = result["success"].get<bool>();
		if (success) {
			std::cout << "Built road depot at tile " << result["tile"].get<int>()
			          << " facing " << result["direction"].get<std::string>()
			          << " (cost: " << result["cost"].get<int64_t>() << ")\n";
		} else {
			std::cerr << "Failed to build depot: " << result["error"].get<std::string>() << "\n";
			return 1;
		}
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}

int HandleRoadBuildStop(RpcClient &client, const CliOptions &opts)
{
	try {
		if (opts.args.size() < 2) {
			std::cerr << "Error: coordinates required\n";
			std::cerr << "Usage: ttdctl road stop <x> <y> --direction <ne|se|sw|nw> [--type bus|truck] [--drive-through]\n";
			return 1;
		}

		nlohmann::json params;
		params["x"] = std::stoi(opts.args[0]);
		params["y"] = std::stoi(opts.args[1]);

		/* Parse options */
		for (size_t i = 2; i < opts.args.size(); ++i) {
			if (opts.args[i] == "--direction" && i + 1 < opts.args.size()) {
				params["direction"] = opts.args[++i];
			} else if (opts.args[i] == "--type" && i + 1 < opts.args.size()) {
				params["stop_type"] = opts.args[++i];
			} else if (opts.args[i] == "--drive-through") {
				params["drive_through"] = true;
			} else if (opts.args[i] == "--company" && i + 1 < opts.args.size()) {
				params["company"] = std::stoi(opts.args[++i]);
			}
		}

		if (!params.contains("direction")) {
			std::cerr << "Error: --direction is required\n";
			std::cerr << "Usage: ttdctl road stop <x> <y> --direction <ne|se|sw|nw> [--type bus|truck] [--drive-through]\n";
			return 1;
		}

		auto result = client.Call("road.buildStop", params);

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		bool success = result["success"].get<bool>();
		if (success) {
			std::cout << "Built " << result["stop_type"].get<std::string>() << " stop at tile " << result["tile"].get<int>()
			          << " facing " << result["direction"].get<std::string>();
			if (result["drive_through"].get<bool>()) {
				std::cout << " (drive-through)";
			}
			std::cout << " (cost: " << result["cost"].get<int64_t>() << ")\n";
		} else {
			std::cerr << "Failed to build stop: " << result["error"].get<std::string>() << "\n";
			return 1;
		}
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}

int HandleRailBuildTrack(RpcClient &client, const CliOptions &opts)
{
	try {
		if (opts.args.size() < 2) {
			std::cerr << "Error: coordinates required\n";
			std::cerr << "Usage: ttdctl rail track <x> <y> --track <x|y|upper|lower|left|right> [--end_x X --end_y Y]\n";
			return 1;
		}

		nlohmann::json params;
		params["x"] = std::stoi(opts.args[0]);
		params["y"] = std::stoi(opts.args[1]);

		/* Parse options */
		for (size_t i = 2; i < opts.args.size(); ++i) {
			if (opts.args[i] == "--track" && i + 1 < opts.args.size()) {
				params["track"] = opts.args[++i];
			} else if (opts.args[i] == "--end_x" && i + 1 < opts.args.size()) {
				params["end_x"] = std::stoi(opts.args[++i]);
			} else if (opts.args[i] == "--end_y" && i + 1 < opts.args.size()) {
				params["end_y"] = std::stoi(opts.args[++i]);
			} else if (opts.args[i] == "--company" && i + 1 < opts.args.size()) {
				params["company"] = std::stoi(opts.args[++i]);
			}
		}

		if (!params.contains("track")) {
			std::cerr << "Error: --track is required\n";
			std::cerr << "Usage: ttdctl rail track <x> <y> --track <x|y|upper|lower|left|right>\n";
			return 1;
		}

		auto result = client.Call("rail.buildTrack", params);

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		bool success = result["success"].get<bool>();
		if (success) {
			std::cout << "Built rail track from tile " << result["start_tile"].get<int>()
			          << " to " << result["end_tile"].get<int>()
			          << " (cost: " << result["cost"].get<int64_t>() << ")\n";
		} else {
			std::cerr << "Failed to build track: " << result["error"].get<std::string>() << "\n";
			return 1;
		}
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}

int HandleRailBuildDepot(RpcClient &client, const CliOptions &opts)
{
	try {
		if (opts.args.size() < 2) {
			std::cerr << "Error: coordinates required\n";
			std::cerr << "Usage: ttdctl rail depot <x> <y> --direction <ne|se|sw|nw>\n";
			return 1;
		}

		nlohmann::json params;
		params["x"] = std::stoi(opts.args[0]);
		params["y"] = std::stoi(opts.args[1]);

		/* Parse options */
		for (size_t i = 2; i < opts.args.size(); ++i) {
			if (opts.args[i] == "--direction" && i + 1 < opts.args.size()) {
				params["direction"] = opts.args[++i];
			} else if (opts.args[i] == "--company" && i + 1 < opts.args.size()) {
				params["company"] = std::stoi(opts.args[++i]);
			}
		}

		if (!params.contains("direction")) {
			std::cerr << "Error: --direction is required\n";
			std::cerr << "Usage: ttdctl rail depot <x> <y> --direction <ne|se|sw|nw>\n";
			return 1;
		}

		auto result = client.Call("rail.buildDepot", params);

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		bool success = result["success"].get<bool>();
		if (success) {
			std::cout << "Built rail depot at tile " << result["tile"].get<int>()
			          << " facing " << result["direction"].get<std::string>()
			          << " (cost: " << result["cost"].get<int64_t>() << ")\n";
		} else {
			std::cerr << "Failed to build depot: " << result["error"].get<std::string>() << "\n";
			return 1;
		}
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}

int HandleRailBuildStation(RpcClient &client, const CliOptions &opts)
{
	try {
		if (opts.args.size() < 2) {
			std::cerr << "Error: coordinates required\n";
			std::cerr << "Usage: ttdctl rail station <x> <y> --axis <x|y> [--platforms N] [--length N]\n";
			return 1;
		}

		nlohmann::json params;
		params["x"] = std::stoi(opts.args[0]);
		params["y"] = std::stoi(opts.args[1]);

		/* Parse options */
		for (size_t i = 2; i < opts.args.size(); ++i) {
			if (opts.args[i] == "--axis" && i + 1 < opts.args.size()) {
				params["axis"] = opts.args[++i];
			} else if (opts.args[i] == "--platforms" && i + 1 < opts.args.size()) {
				params["platforms"] = std::stoi(opts.args[++i]);
			} else if (opts.args[i] == "--length" && i + 1 < opts.args.size()) {
				params["length"] = std::stoi(opts.args[++i]);
			} else if (opts.args[i] == "--company" && i + 1 < opts.args.size()) {
				params["company"] = std::stoi(opts.args[++i]);
			}
		}

		if (!params.contains("axis")) {
			std::cerr << "Error: --axis is required\n";
			std::cerr << "Usage: ttdctl rail station <x> <y> --axis <x|y> [--platforms N] [--length N]\n";
			return 1;
		}

		auto result = client.Call("rail.buildStation", params);

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		bool success = result["success"].get<bool>();
		if (success) {
			std::cout << "Built " << result["platforms"].get<int>() << "-platform station at tile " << result["tile"].get<int>()
			          << " (length " << result["length"].get<int>() << ", axis " << result["axis"].get<std::string>() << ")"
			          << " (cost: " << result["cost"].get<int64_t>() << ")\n";
		} else {
			std::cerr << "Failed to build station: " << result["error"].get<std::string>() << "\n";
			return 1;
		}
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}

int HandleRailBuildSignal(RpcClient &client, const CliOptions &opts)
{
	try {
		if (opts.args.size() < 2) {
			std::cerr << "Error: coordinates required\n";
			std::cerr << "Usage: ttdctl rail signal <x> <y> --track <x|y|upper|lower|left|right> [--type <type>] [--variant electric|semaphore] [--two-way]\n";
			std::cerr << "Signal types: block, entry, exit, combo, pbs, pbs_oneway\n";
			return 1;
		}

		nlohmann::json params;
		params["x"] = std::stoi(opts.args[0]);
		params["y"] = std::stoi(opts.args[1]);

		/* Parse options */
		for (size_t i = 2; i < opts.args.size(); ++i) {
			if (opts.args[i] == "--track" && i + 1 < opts.args.size()) {
				params["track"] = opts.args[++i];
			} else if (opts.args[i] == "--type" && i + 1 < opts.args.size()) {
				params["signal_type"] = opts.args[++i];
			} else if (opts.args[i] == "--variant" && i + 1 < opts.args.size()) {
				params["variant"] = opts.args[++i];
			} else if (opts.args[i] == "--two-way") {
				params["two_way"] = true;
			} else if (opts.args[i] == "--company" && i + 1 < opts.args.size()) {
				params["company"] = std::stoi(opts.args[++i]);
			}
		}

		if (!params.contains("track")) {
			std::cerr << "Error: --track is required\n";
			std::cerr << "Usage: ttdctl rail signal <x> <y> --track <x|y|upper|lower|left|right> [--type <type>]\n";
			return 1;
		}

		auto result = client.Call("rail.buildSignal", params);

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		bool success = result["success"].get<bool>();
		if (success) {
			std::cout << "Built " << result["signal_type"].get<std::string>()
			          << " " << result["variant"].get<std::string>() << " signal at tile " << result["tile"].get<int>()
			          << " on track " << result["track"].get<std::string>();
			if (result.contains("two_way") && result["two_way"].get<bool>()) {
				std::cout << " (two-way)";
			} else {
				std::cout << " (one-way)";
			}
			std::cout << " (cost: " << result["cost"].get<int64_t>() << ")\n";
		} else {
			std::cerr << "Failed to build signal: " << result["error"].get<std::string>() << "\n";
			return 1;
		}
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}

int HandleRailRemoveSignal(RpcClient &client, const CliOptions &opts)
{
	try {
		if (opts.args.size() < 2) {
			std::cerr << "Error: coordinates required\n";
			std::cerr << "Usage: ttdctl rail remove-signal <x> <y> --track <x|y|upper|lower|left|right>\n";
			return 1;
		}

		nlohmann::json params;
		params["x"] = std::stoi(opts.args[0]);
		params["y"] = std::stoi(opts.args[1]);

		/* Parse options */
		for (size_t i = 2; i < opts.args.size(); ++i) {
			if (opts.args[i] == "--track" && i + 1 < opts.args.size()) {
				params["track"] = opts.args[++i];
			} else if (opts.args[i] == "--company" && i + 1 < opts.args.size()) {
				params["company"] = std::stoi(opts.args[++i]);
			}
		}

		if (!params.contains("track")) {
			std::cerr << "Error: --track is required\n";
			std::cerr << "Usage: ttdctl rail remove-signal <x> <y> --track <x|y|upper|lower|left|right>\n";
			return 1;
		}

		auto result = client.Call("rail.removeSignal", params);

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		bool success = result["success"].get<bool>();
		if (success) {
			std::cout << "Removed signal from tile " << result["tile"].get<int>()
			          << " on track " << result["track"].get<std::string>() << "\n";
		} else {
			std::cerr << "Failed to remove signal: " << result["error"].get<std::string>() << "\n";
			return 1;
		}
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}

int HandleMarineBuildDock(RpcClient &client, const CliOptions &opts)
{
	try {
		if (opts.args.size() < 2) {
			std::cerr << "Error: coordinates required\n";
			std::cerr << "Usage: ttdctl marine dock <x> <y> [--station <id>]\n";
			return 1;
		}

		nlohmann::json params;
		params["x"] = std::stoi(opts.args[0]);
		params["y"] = std::stoi(opts.args[1]);

		/* Parse options */
		for (size_t i = 2; i < opts.args.size(); ++i) {
			if (opts.args[i] == "--station" && i + 1 < opts.args.size()) {
				params["station_id"] = std::stoi(opts.args[++i]);
			} else if (opts.args[i] == "--company" && i + 1 < opts.args.size()) {
				params["company"] = std::stoi(opts.args[++i]);
			}
		}

		auto result = client.Call("marine.buildDock", params);

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		bool success = result["success"].get<bool>();
		if (success) {
			std::cout << "Built dock at tile " << result["tile"].get<int>()
			          << " (" << result["x"].get<int>() << ", " << result["y"].get<int>() << ")"
			          << " (cost: " << result["cost"].get<int64_t>() << ")\n";
		} else {
			std::cerr << "Failed to build dock: " << result["error"].get<std::string>() << "\n";
			return 1;
		}
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}

int HandleMarineBuildDepot(RpcClient &client, const CliOptions &opts)
{
	try {
		if (opts.args.size() < 2) {
			std::cerr << "Error: coordinates required\n";
			std::cerr << "Usage: ttdctl marine depot <x> <y> [--axis x|y]\n";
			return 1;
		}

		nlohmann::json params;
		params["x"] = std::stoi(opts.args[0]);
		params["y"] = std::stoi(opts.args[1]);

		/* Parse options */
		for (size_t i = 2; i < opts.args.size(); ++i) {
			if (opts.args[i] == "--axis" && i + 1 < opts.args.size()) {
				params["axis"] = opts.args[++i];
			} else if (opts.args[i] == "--company" && i + 1 < opts.args.size()) {
				params["company"] = std::stoi(opts.args[++i]);
			}
		}

		auto result = client.Call("marine.buildDepot", params);

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		bool success = result["success"].get<bool>();
		if (success) {
			std::cout << "Built ship depot at tile " << result["tile"].get<int>()
			          << " axis " << result["axis"].get<std::string>()
			          << " (cost: " << result["cost"].get<int64_t>() << ")\n";
		} else {
			std::cerr << "Failed to build ship depot: " << result["error"].get<std::string>() << "\n";
			return 1;
		}
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}

int HandleAirportBuild(RpcClient &client, const CliOptions &opts)
{
	try {
		if (opts.args.size() < 2) {
			std::cerr << "Error: coordinates required\n";
			std::cerr << "Usage: ttdctl airport build <x> <y> [--type <type>] [--station <id>]\n";
			std::cerr << "Airport types: small, large, heliport, metropolitan, international,\n";
			std::cerr << "               commuter, helidepot, intercontinental, helistation\n";
			return 1;
		}

		nlohmann::json params;
		params["x"] = std::stoi(opts.args[0]);
		params["y"] = std::stoi(opts.args[1]);

		/* Parse options */
		for (size_t i = 2; i < opts.args.size(); ++i) {
			if (opts.args[i] == "--type" && i + 1 < opts.args.size()) {
				params["type"] = opts.args[++i];
			} else if (opts.args[i] == "--layout" && i + 1 < opts.args.size()) {
				params["layout"] = std::stoi(opts.args[++i]);
			} else if (opts.args[i] == "--station" && i + 1 < opts.args.size()) {
				params["station_id"] = std::stoi(opts.args[++i]);
			} else if (opts.args[i] == "--company" && i + 1 < opts.args.size()) {
				params["company"] = std::stoi(opts.args[++i]);
			}
		}

		auto result = client.Call("airport.build", params);

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		bool success = result["success"].get<bool>();
		if (success) {
			std::cout << "Built " << result["type"].get<std::string>() << " airport at tile " << result["tile"].get<int>()
			          << " (" << result["x"].get<int>() << ", " << result["y"].get<int>() << ")"
			          << " (cost: " << result["cost"].get<int64_t>() << ")\n";
		} else {
			std::cerr << "Failed to build airport: " << result["error"].get<std::string>() << "\n";
			return 1;
		}
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}

int HandleRailBuildTrackLine(RpcClient &client, const CliOptions &opts)
{
	try {
		if (opts.args.size() < 4) {
			std::cerr << "Error: start and end coordinates required\n";
			std::cerr << "Usage: ttdctl rail track-line <start_x> <start_y> <end_x> <end_y>\n";
			std::cerr << "Builds a line of track from start to end, including corners if needed.\n";
			return 1;
		}

		nlohmann::json params;
		params["start_x"] = std::stoi(opts.args[0]);
		params["start_y"] = std::stoi(opts.args[1]);
		params["end_x"] = std::stoi(opts.args[2]);
		params["end_y"] = std::stoi(opts.args[3]);

		/* Parse options */
		for (size_t i = 4; i < opts.args.size(); ++i) {
			if (opts.args[i] == "--company" && i + 1 < opts.args.size()) {
				params["company"] = std::stoi(opts.args[++i]);
			}
		}

		auto result = client.Call("rail.buildTrackLine", params);

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		bool success = result["success"].get<bool>();
		if (success) {
			std::cout << "Built track line from (" << result["start_x"].get<int>()
			          << ", " << result["start_y"].get<int>() << ") to ("
			          << result["end_x"].get<int>() << ", " << result["end_y"].get<int>() << ")\n";
			std::cout << "Segments built:\n";
			for (const auto &seg : result["segments"]) {
				if (seg.contains("tile")) {
					std::cout << "  Corner at tile " << seg["tile"].get<int>()
					          << " (" << seg["track"].get<std::string>() << "): "
					          << (seg["success"].get<bool>() ? "OK" : "FAILED") << "\n";
				} else {
					std::cout << "  Track " << seg["track"].get<std::string>()
					          << " from " << seg["start_tile"].get<int>()
					          << " to " << seg["end_tile"].get<int>() << ": "
					          << (seg["success"].get<bool>() ? "OK" : "FAILED") << "\n";
				}
			}
		} else {
			std::cerr << "Failed to build track line\n";
			return 1;
		}
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}

int HandleRailSignalLine(RpcClient &client, const CliOptions &opts)
{
	try {
		if (opts.args.size() < 4) {
			std::cerr << "Error: start and end coordinates required\n";
			std::cerr << "Usage: ttdctl rail signal-line <start_x> <start_y> <end_x> <end_y> [--type <signal_type>] [--density <spacing>]\n";
			std::cerr << "Signal types: block, entry, exit, combo, pbs, pbs_oneway (default)\n";
			return 1;
		}

		nlohmann::json params;
		params["start_x"] = std::stoi(opts.args[0]);
		params["start_y"] = std::stoi(opts.args[1]);
		params["end_x"] = std::stoi(opts.args[2]);
		params["end_y"] = std::stoi(opts.args[3]);

		/* Parse options */
		for (size_t i = 4; i < opts.args.size(); ++i) {
			if (opts.args[i] == "--type" && i + 1 < opts.args.size()) {
				params["signal_type"] = opts.args[++i];
			} else if (opts.args[i] == "--density" && i + 1 < opts.args.size()) {
				params["signal_density"] = std::stoi(opts.args[++i]);
			} else if (opts.args[i] == "--track" && i + 1 < opts.args.size()) {
				params["track"] = opts.args[++i];
			} else if (opts.args[i] == "--variant" && i + 1 < opts.args.size()) {
				params["variant"] = opts.args[++i];
			} else if (opts.args[i] == "--company" && i + 1 < opts.args.size()) {
				params["company"] = std::stoi(opts.args[++i]);
			}
		}

		auto result = client.Call("rail.signalLine", params);

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		bool success = result["success"].get<bool>();
		if (success) {
			std::cout << "Built " << result["signal_type"].get<std::string>()
			          << " signals from (" << result["start_x"].get<int>()
			          << ", " << result["start_y"].get<int>() << ") to ("
			          << result["end_x"].get<int>() << ", " << result["end_y"].get<int>() << ")"
			          << " on " << result["track"].get<std::string>() << " track"
			          << " (density: " << result["signal_density"].get<int>()
			          << ", cost: " << result["cost"].get<int64_t>() << ")\n";
		} else {
			std::cerr << "Failed to build signal line: " << result["error"].get<std::string>() << "\n";
			return 1;
		}
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}
