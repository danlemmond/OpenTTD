/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file commands_action.cpp Action command handlers for ttdctl. */

#include "cli_common.h"
#include <iostream>

int HandleGameNewGame(RpcClient &client, const CliOptions &opts)
{
	try {
		nlohmann::json params = nlohmann::json::object();

		/* Parse optional seed */
		for (size_t i = 0; i < opts.args.size(); ++i) {
			if (opts.args[i] == "--seed" && i + 1 < opts.args.size()) {
				params["seed"] = static_cast<uint32_t>(std::stoul(opts.args[++i]));
			}
		}

		auto result = client.Call("game.newgame", params);

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		if (result["seed"].is_string()) {
			std::cout << "New game started (seed: " << result["seed"].get<std::string>() << ")\n";
		} else {
			std::cout << "New game started (seed: " << result["seed"].get<uint32_t>() << ")\n";
		}
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}

int HandleVehicleStartStop(RpcClient &client, const CliOptions &opts)
{
	try {
		if (opts.args.empty()) {
			std::cerr << "Error: vehicle ID required\n";
			std::cerr << "Usage: ttdctl vehicle startstop <id>\n";
			return 1;
		}

		nlohmann::json params = nlohmann::json::object();
		params["vehicle_id"] = std::stoi(opts.args[0]);

		auto result = client.Call("vehicle.startstop", params);

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		bool success = result["success"].get<bool>();
		int vid = result["vehicle_id"].get<int>();

		if (success) {
			bool stopped = result["stopped"].get<bool>();
			std::cout << "Vehicle #" << vid << " is now " << (stopped ? "stopped" : "running") << "\n";
		} else {
			std::cerr << "Failed to toggle vehicle #" << vid << ": " << result["error"].get<std::string>() << "\n";
			return 1;
		}
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}

int HandleVehicleDepot(RpcClient &client, const CliOptions &opts)
{
	try {
		if (opts.args.empty()) {
			std::cerr << "Error: vehicle ID required\n";
			std::cerr << "Usage: ttdctl vehicle depot <id>\n";
			return 1;
		}

		nlohmann::json params = nlohmann::json::object();
		params["vehicle_id"] = std::stoi(opts.args[0]);

		auto result = client.Call("vehicle.depot", params);

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		bool success = result["success"].get<bool>();
		int vid = result["vehicle_id"].get<int>();

		if (success) {
			std::cout << "Vehicle #" << vid << " sent to depot\n";
		} else {
			std::cerr << "Failed to send vehicle #" << vid << " to depot: " << result["error"].get<std::string>() << "\n";
			return 1;
		}
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}

int HandleVehicleTurnaround(RpcClient &client, const CliOptions &opts)
{
	try {
		if (opts.args.empty()) {
			std::cerr << "Error: vehicle ID required\n";
			std::cerr << "Usage: ttdctl vehicle turnaround <id>\n";
			return 1;
		}

		nlohmann::json params = nlohmann::json::object();
		params["vehicle_id"] = std::stoi(opts.args[0]);

		auto result = client.Call("vehicle.turnaround", params);

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		bool success = result["success"].get<bool>();
		int vid = result["vehicle_id"].get<int>();

		if (success) {
			std::cout << "Vehicle #" << vid << " depot order cancelled\n";
		} else {
			std::cerr << "Failed to cancel depot order for vehicle #" << vid << ": " << result["error"].get<std::string>() << "\n";
			return 1;
		}
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}

int HandleOrderAppend(RpcClient &client, const CliOptions &opts)
{
	try {
		if (opts.args.empty()) {
			std::cerr << "Error: vehicle ID required\n";
			std::cerr << "Usage: ttdctl order append <vehicle_id> --station <id> [--load TYPE] [--unload TYPE] [--non-stop]\n";
			std::cerr << "  Load types: default, full, full_any, none\n";
			std::cerr << "  Unload types: default, unload, transfer, none\n";
			return 1;
		}

		nlohmann::json params = nlohmann::json::object();
		params["vehicle_id"] = std::stoi(opts.args[0]);

		/* Parse --station, --load, --unload, --non-stop from args */
		for (size_t i = 1; i < opts.args.size(); ++i) {
			if (opts.args[i] == "--station" && i + 1 < opts.args.size()) {
				params["destination"] = std::stoi(opts.args[++i]);
			} else if (opts.args[i] == "--load" && i + 1 < opts.args.size()) {
				params["load"] = opts.args[++i];
			} else if (opts.args[i] == "--unload" && i + 1 < opts.args.size()) {
				params["unload"] = opts.args[++i];
			} else if (opts.args[i] == "--non-stop") {
				params["non_stop"] = true;
			}
		}

		if (!params.contains("destination")) {
			std::cerr << "Error: --station is required\n";
			std::cerr << "Usage: ttdctl order append <vehicle_id> --station <id> [--load TYPE] [--unload TYPE] [--non-stop]\n";
			std::cerr << "  Load types: default, full, full_any, none\n";
			std::cerr << "  Unload types: default, unload, transfer, none\n";
			return 1;
		}

		auto result = client.Call("order.append", params);

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		bool success = result["success"].get<bool>();
		int vid = result["vehicle_id"].get<int>();

		if (success) {
			int order_index = result["order_index"].get<int>();
			std::cout << "Added order #" << order_index << " to vehicle #" << vid << "\n";
		} else {
			std::cerr << "Failed to add order to vehicle #" << vid << ": " << result["error"].get<std::string>() << "\n";
			return 1;
		}
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}

int HandleOrderRemove(RpcClient &client, const CliOptions &opts)
{
	try {
		if (opts.args.empty()) {
			std::cerr << "Error: vehicle ID required\n";
			std::cerr << "Usage: ttdctl order remove <vehicle_id> --index <order_index>\n";
			return 1;
		}

		nlohmann::json params = nlohmann::json::object();
		params["vehicle_id"] = std::stoi(opts.args[0]);

		/* Parse --index from args */
		for (size_t i = 1; i < opts.args.size(); ++i) {
			if (opts.args[i] == "--index" && i + 1 < opts.args.size()) {
				params["order_index"] = std::stoi(opts.args[++i]);
			}
		}

		if (!params.contains("order_index")) {
			std::cerr << "Error: --index is required\n";
			std::cerr << "Usage: ttdctl order remove <vehicle_id> --index <order_index>\n";
			return 1;
		}

		auto result = client.Call("order.remove", params);

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		bool success = result["success"].get<bool>();
		int vid = result["vehicle_id"].get<int>();

		if (success) {
			std::cout << "Removed order from vehicle #" << vid << "\n";
		} else {
			std::cerr << "Failed to remove order from vehicle #" << vid << ": " << result["error"].get<std::string>() << "\n";
			return 1;
		}
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}

int HandleOrderInsert(RpcClient &client, const CliOptions &opts)
{
	try {
		if (opts.args.empty()) {
			std::cerr << "Error: vehicle ID required\n";
			std::cerr << "Usage: ttdctl order insert <vehicle_id> --index <pos> --station <id> [--load TYPE] [--unload TYPE] [--non-stop]\n";
			return 1;
		}

		nlohmann::json params = nlohmann::json::object();
		params["vehicle_id"] = std::stoi(opts.args[0]);

		for (size_t i = 1; i < opts.args.size(); ++i) {
			if (opts.args[i] == "--index" && i + 1 < opts.args.size()) {
				params["order_index"] = std::stoi(opts.args[++i]);
			} else if (opts.args[i] == "--station" && i + 1 < opts.args.size()) {
				params["destination"] = std::stoi(opts.args[++i]);
			} else if (opts.args[i] == "--load" && i + 1 < opts.args.size()) {
				params["load"] = opts.args[++i];
			} else if (opts.args[i] == "--unload" && i + 1 < opts.args.size()) {
				params["unload"] = opts.args[++i];
			} else if (opts.args[i] == "--non-stop") {
				params["non_stop"] = true;
			}
		}

		if (!params.contains("order_index") || !params.contains("destination")) {
			std::cerr << "Error: --index and --station are required\n";
			std::cerr << "Usage: ttdctl order insert <vehicle_id> --index <pos> --station <id>\n";
			return 1;
		}

		auto result = client.Call("order.insert", params);

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		bool success = result["success"].get<bool>();
		int vid = result["vehicle_id"].get<int>();

		if (success) {
			int order_index = result["order_index"].get<int>();
			std::cout << "Inserted order at position #" << order_index << " for vehicle #" << vid << "\n";
		} else {
			std::cerr << "Failed to insert order: " << result["error"].get<std::string>() << "\n";
			return 1;
		}
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}

int HandleOrderSetFlags(RpcClient &client, const CliOptions &opts)
{
	try {
		if (opts.args.empty()) {
			std::cerr << "Error: vehicle ID required\n";
			std::cerr << "Usage: ttdctl order setflags <vehicle_id> --index <pos> [--load TYPE] [--unload TYPE] [--non-stop]\n";
			return 1;
		}

		nlohmann::json params = nlohmann::json::object();
		params["vehicle_id"] = std::stoi(opts.args[0]);

		for (size_t i = 1; i < opts.args.size(); ++i) {
			if (opts.args[i] == "--index" && i + 1 < opts.args.size()) {
				params["order_index"] = std::stoi(opts.args[++i]);
			} else if (opts.args[i] == "--load" && i + 1 < opts.args.size()) {
				params["load"] = opts.args[++i];
			} else if (opts.args[i] == "--unload" && i + 1 < opts.args.size()) {
				params["unload"] = opts.args[++i];
			} else if (opts.args[i] == "--non-stop") {
				params["non_stop"] = true;
			} else if (opts.args[i] == "--no-non-stop") {
				params["non_stop"] = false;
			}
		}

		if (!params.contains("order_index")) {
			std::cerr << "Error: --index is required\n";
			std::cerr << "Usage: ttdctl order setflags <vehicle_id> --index <pos> [--load TYPE] [--unload TYPE] [--non-stop]\n";
			return 1;
		}

		auto result = client.Call("order.setFlags", params);

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		bool success = result["success"].get<bool>();
		int vid = result["vehicle_id"].get<int>();
		int idx = result["order_index"].get<int>();

		if (success) {
			std::cout << "Updated flags for order #" << idx << " on vehicle #" << vid << "\n";
		} else {
			std::cerr << "Failed to update order flags: " << result["error"].get<std::string>() << "\n";
			return 1;
		}
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}

int HandleOrderShare(RpcClient &client, const CliOptions &opts)
{
	try {
		if (opts.args.size() < 2) {
			std::cerr << "Error: two vehicle IDs required\n";
			std::cerr << "Usage: ttdctl order share <vehicle_id> <source_vehicle_id> [--mode share|copy|unshare]\n";
			return 1;
		}

		nlohmann::json params = nlohmann::json::object();
		params["vehicle_id"] = std::stoi(opts.args[0]);
		params["source_vehicle_id"] = std::stoi(opts.args[1]);

		for (size_t i = 2; i < opts.args.size(); ++i) {
			if (opts.args[i] == "--mode" && i + 1 < opts.args.size()) {
				params["mode"] = opts.args[++i];
			}
		}

		auto result = client.Call("order.share", params);

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		bool success = result["success"].get<bool>();
		std::string mode = result["mode"].get<std::string>();

		if (success) {
			int vid = result["vehicle_id"].get<int>();
			int src = result["source_vehicle_id"].get<int>();
			std::cout << "Vehicle #" << vid << " now " << mode << "s orders with vehicle #" << src << "\n";
		} else {
			std::cerr << "Failed to " << mode << " orders: " << result["error"].get<std::string>() << "\n";
			return 1;
		}
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}

int HandleCompanySetLoan(RpcClient &client, const CliOptions &opts)
{
	try {
		if (opts.args.empty()) {
			std::cerr << "Error: loan amount required\n";
			std::cerr << "Usage: ttdctl company setloan <amount> [--company <id>]\n";
			return 1;
		}

		nlohmann::json params = nlohmann::json::object();
		params["amount"] = std::stoll(opts.args[0]);

		for (size_t i = 1; i < opts.args.size(); ++i) {
			if (opts.args[i] == "--company" && i + 1 < opts.args.size()) {
				params["company"] = std::stoi(opts.args[++i]);
			}
		}

		auto result = client.Call("company.setLoan", params);

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		bool success = result["success"].get<bool>();

		if (success) {
			int64_t old_loan = result["old_loan"].get<int64_t>();
			int64_t new_loan = result["new_loan"].get<int64_t>();
			std::cout << "Loan changed from " << old_loan << " to " << new_loan << "\n";
		} else {
			std::cerr << "Failed to set loan: " << result["error"].get<std::string>() << "\n";
			return 1;
		}
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}

int HandleVehicleRefit(RpcClient &client, const CliOptions &opts)
{
	try {
		if (opts.args.empty()) {
			std::cerr << "Error: vehicle ID required\n";
			std::cerr << "Usage: ttdctl vehicle refit <vehicle_id> --cargo <cargo_id>\n";
			return 1;
		}

		nlohmann::json params = nlohmann::json::object();
		params["vehicle_id"] = std::stoi(opts.args[0]);

		for (size_t i = 1; i < opts.args.size(); ++i) {
			if (opts.args[i] == "--cargo" && i + 1 < opts.args.size()) {
				params["cargo"] = std::stoi(opts.args[++i]);
			}
		}

		if (!params.contains("cargo")) {
			std::cerr << "Error: --cargo is required\n";
			std::cerr << "Usage: ttdctl vehicle refit <vehicle_id> --cargo <cargo_id>\n";
			return 1;
		}

		auto result = client.Call("vehicle.refit", params);

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		bool success = result["success"].get<bool>();
		int vid = result["vehicle_id"].get<int>();

		if (success) {
			std::string cargo_name = result["cargo_name"].get<std::string>();
			int capacity = result["capacity"].get<int>();
			std::cout << "Vehicle #" << vid << " refitted to " << cargo_name << " (capacity: " << capacity << ")\n";
		} else {
			std::cerr << "Failed to refit vehicle #" << vid << ": " << result["error"].get<std::string>() << "\n";
			return 1;
		}
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}

int HandleTownPerformAction(RpcClient &client, const CliOptions &opts)
{
	try {
		if (opts.args.empty()) {
			std::cerr << "Error: town ID required\n";
			std::cerr << "Usage: ttdctl town action <town_id> --action <action_name> [--company <id>]\n";
			std::cerr << "Actions: advertise_small, advertise_medium, advertise_large,\n";
			std::cerr << "         road_rebuild, build_statue, fund_buildings, buy_rights, bribe\n";
			return 1;
		}

		nlohmann::json params = nlohmann::json::object();
		params["town_id"] = std::stoi(opts.args[0]);

		for (size_t i = 1; i < opts.args.size(); ++i) {
			if (opts.args[i] == "--action" && i + 1 < opts.args.size()) {
				params["action"] = opts.args[++i];
			} else if (opts.args[i] == "--company" && i + 1 < opts.args.size()) {
				params["company"] = std::stoi(opts.args[++i]);
			}
		}

		if (!params.contains("action")) {
			std::cerr << "Error: --action is required\n";
			std::cerr << "Usage: ttdctl town action <town_id> --action <action_name>\n";
			return 1;
		}

		auto result = client.Call("town.performAction", params);

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		bool success = result["success"].get<bool>();
		std::string town_name = result["town_name"].get<std::string>();
		std::string action = result["action"].get<std::string>();

		if (success) {
			int64_t cost = result["cost"].get<int64_t>();
			std::cout << "Performed '" << action << "' in " << town_name << " (cost: " << cost << ")\n";
		} else {
			std::cerr << "Failed to perform '" << action << "' in " << town_name << ": " << result["error"].get<std::string>() << "\n";
			return 1;
		}
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}

int HandleViewportGoto(RpcClient &client, const CliOptions &opts)
{
	try {
		nlohmann::json params = nlohmann::json::object();

		/* Parse arguments - can be tile index or x y coordinates */
		if (opts.args.size() >= 2) {
			params["x"] = static_cast<uint>(std::stoul(opts.args[0]));
			params["y"] = static_cast<uint>(std::stoul(opts.args[1]));
		} else if (opts.args.size() == 1) {
			params["tile"] = static_cast<uint32_t>(std::stoul(opts.args[0]));
		} else {
			std::cerr << "Error: tile coordinates required\n";
			std::cerr << "Usage: ttdctl viewport goto <x> <y>\n";
			std::cerr << "       ttdctl viewport goto <tile_index>\n";
			return 1;
		}

		/* Check for --instant flag */
		for (const auto &arg : opts.args) {
			if (arg == "--instant") {
				params["instant"] = true;
			}
		}

		auto result = client.Call("viewport.goto", params);

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		if (result["success"].get<bool>()) {
			std::cout << "Scrolled to tile " << result["tile"].get<uint32_t>()
			          << " (x=" << result["x"].get<uint>()
			          << ", y=" << result["y"].get<uint>() << ")\n";
		} else {
			std::cout << "Failed to scroll viewport\n";
		}
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}

int HandleViewportFollow(RpcClient &client, const CliOptions &opts)
{
	try {
		nlohmann::json params = nlohmann::json::object();

		/* Check for --stop flag */
		bool stop = false;
		for (const auto &arg : opts.args) {
			if (arg == "--stop") {
				stop = true;
			}
		}

		if (stop) {
			params["stop"] = true;
		} else if (opts.args.empty()) {
			std::cerr << "Error: vehicle ID required\n";
			std::cerr << "Usage: ttdctl viewport follow <vehicle_id>\n";
			std::cerr << "       ttdctl viewport follow --stop\n";
			return 1;
		} else {
			params["vehicle_id"] = std::stoi(opts.args[0]);
		}

		auto result = client.Call("viewport.follow", params);

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		if (result["success"].get<bool>()) {
			if (result.contains("following") && !result["following"].get<bool>()) {
				std::cout << "Stopped following vehicle\n";
			} else {
				std::cout << "Following vehicle " << result["vehicle_id"].get<int>()
				          << " (" << result["vehicle_name"].get<std::string>() << ")\n";
			}
		} else {
			std::cout << "Failed to follow vehicle\n";
		}
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}

int HandleActivityHotspot(RpcClient &client, const CliOptions &opts)
{
	try {
		nlohmann::json params = nlohmann::json::object();

		/* Parse --seconds flag */
		for (size_t i = 0; i < opts.args.size(); ++i) {
			if (opts.args[i] == "--seconds" && i + 1 < opts.args.size()) {
				params["seconds"] = std::stoi(opts.args[++i]);
			}
		}

		auto result = client.Call("activity.hotspot", params);

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		if (result["has_activity"].get<bool>()) {
			std::cout << "Activity hotspot at tile " << result["hotspot_tile"].get<uint32_t>()
			          << " (x=" << result["hotspot_x"].get<uint>()
			          << ", y=" << result["hotspot_y"].get<uint>() << ")\n";
			std::cout << "Activity count: " << result["activity_count"].get<int>()
			          << " in last " << result["seconds"].get<int>() << " seconds\n";

			if (result["recent_actions"].is_array() && !result["recent_actions"].empty()) {
				std::cout << "\nRecent actions:\n";
				for (const auto &action : result["recent_actions"]) {
					std::cout << "  - " << action["action"].get<std::string>()
					          << " at (" << action["x"].get<uint>()
					          << ", " << action["y"].get<uint>() << ")\n";
				}
			}
		} else {
			std::cout << "No recent activity recorded\n";
		}
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}

int HandleActivityClear(RpcClient &client, const CliOptions &opts)
{
	try {
		nlohmann::json params = nlohmann::json::object();
		auto result = client.Call("activity.clear", params);

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		std::cout << "Cleared " << result["cleared"].get<int>() << " activity records\n";
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}
