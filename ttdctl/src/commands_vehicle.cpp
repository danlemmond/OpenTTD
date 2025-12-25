/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file commands_vehicle.cpp Vehicle management command handlers for ttdctl. */

#include "cli_common.h"
#include <iostream>

int HandleEngineList(RpcClient &client, const CliOptions &opts)
{
	try {
		nlohmann::json params = nlohmann::json::object();

		/* Type filter from first arg */
		if (!opts.args.empty() && opts.args[0][0] != '-') {
			params["type"] = opts.args[0];
		}

		/* Parse options */
		for (size_t i = 0; i < opts.args.size(); ++i) {
			if (opts.args[i] == "--company" && i + 1 < opts.args.size()) {
				params["company"] = std::stoi(opts.args[++i]);
			} else if (opts.args[i] == "--all") {
				params["buildable_only"] = false;
			}
		}

		auto result = client.Call("engine.list", params);

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		if (result.empty()) {
			std::cout << "No engines available.\n";
			return 0;
		}

		std::vector<std::vector<std::string>> rows;
		rows.push_back({"ID", "Name", "Type", "Cost", "Speed", "Capacity", "Power"});

		for (const auto &e : result) {
			std::string power = "-";
			if (e.contains("power")) {
				power = std::to_string(e["power"].get<int>()) + "hp";
			}
			std::string capacity = std::to_string(e["capacity"].get<int>());
			if (e.contains("cargo_name")) {
				capacity += " " + e["cargo_name"].get<std::string>();
			}

			rows.push_back({
				std::to_string(e["id"].get<int>()),
				e["name"].get<std::string>(),
				e["type"].get<std::string>(),
				std::to_string(e["cost"].get<int64_t>()),
				std::to_string(e["max_speed"].get<int>()),
				capacity,
				power
			});
		}

		PrintTable(rows);
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}

int HandleEngineGet(RpcClient &client, const CliOptions &opts)
{
	try {
		if (opts.args.empty()) {
			std::cerr << "Error: engine ID required\n";
			std::cerr << "Usage: ttdctl engine get <id>\n";
			return 1;
		}

		nlohmann::json params = nlohmann::json::object();
		params["id"] = std::stoi(opts.args[0]);

		auto result = client.Call("engine.get", params);

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		std::cout << "Engine #" << result["id"].get<int>() << "\n";
		std::cout << "---------------\n";
		std::cout << "Name:         " << result["name"].get<std::string>() << "\n";
		std::cout << "Type:         " << result["type"].get<std::string>() << "\n";
		std::cout << "Buildable:    " << (result["buildable"].get<bool>() ? "Yes" : "No") << "\n";
		std::cout << "Cost:         " << result["cost"].get<int64_t>() << "\n";
		std::cout << "Running Cost: " << result["running_cost"].get<int64_t>() << "/year\n";
		std::cout << "Max Speed:    " << result["max_speed"].get<int>() << "\n";
		std::cout << "Capacity:     " << result["capacity"].get<int>();
		if (result.contains("cargo_name")) {
			std::cout << " " << result["cargo_name"].get<std::string>();
		}
		std::cout << "\n";
		std::cout << "Reliability:  " << result["reliability"].get<int>() << "% (max " << result["reliability_max"].get<int>() << "%)\n";

		if (result.contains("power")) {
			std::cout << "Power:        " << result["power"].get<int>() << " hp\n";
			std::cout << "Weight:       " << result["weight"].get<int>() << " t\n";
		}

		if (result.contains("is_wagon")) {
			std::cout << "Is Wagon:     " << (result["is_wagon"].get<bool>() ? "Yes" : "No") << "\n";
		}

		if (result.contains("refit_cargos") && !result["refit_cargos"].empty()) {
			std::cout << "\nRefit Options:\n";
			for (const auto &c : result["refit_cargos"]) {
				std::cout << "  " << c["cargo_name"].get<std::string>() << "\n";
			}
		}

		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}

int HandleVehicleBuild(RpcClient &client, const CliOptions &opts)
{
	try {
		nlohmann::json params = nlohmann::json::object();

		/* Parse depot and engine from args */
		for (size_t i = 0; i < opts.args.size(); ++i) {
			if (opts.args[i] == "--depot" && i + 1 < opts.args.size()) {
				params["depot_tile"] = static_cast<uint32_t>(std::stoul(opts.args[++i]));
			} else if (opts.args[i] == "--depot_x" && i + 1 < opts.args.size()) {
				params["depot_x"] = std::stoi(opts.args[++i]);
			} else if (opts.args[i] == "--depot_y" && i + 1 < opts.args.size()) {
				params["depot_y"] = std::stoi(opts.args[++i]);
			} else if (opts.args[i] == "--engine" && i + 1 < opts.args.size()) {
				params["engine_id"] = std::stoi(opts.args[++i]);
			} else if (opts.args[i] == "--cargo" && i + 1 < opts.args.size()) {
				params["cargo"] = std::stoi(opts.args[++i]);
			} else if (opts.args[i] == "--company" && i + 1 < opts.args.size()) {
				params["company"] = std::stoi(opts.args[++i]);
			}
		}

		if (!params.contains("engine_id")) {
			std::cerr << "Error: --engine is required\n";
			std::cerr << "Usage: ttdctl vehicle build --engine <id> --depot <tile> [--cargo <id>]\n";
			std::cerr << "       ttdctl vehicle build --engine <id> --depot_x <x> --depot_y <y>\n";
			return 1;
		}

		if (!params.contains("depot_tile") && !(params.contains("depot_x") && params.contains("depot_y"))) {
			std::cerr << "Error: depot location required (--depot <tile> or --depot_x/--depot_y)\n";
			std::cerr << "Usage: ttdctl vehicle build --engine <id> --depot <tile>\n";
			return 1;
		}

		auto result = client.Call("vehicle.build", params);

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		if (result["success"].get<bool>()) {
			std::cout << "Built " << result["engine_name"].get<std::string>()
			          << " (Vehicle #" << result["vehicle_id"].get<int>() << ")"
			          << " for " << result["cost"].get<int64_t>() << "\n";
		} else {
			std::cerr << "Failed to build vehicle: " << result["error"].get<std::string>() << "\n";
			return 1;
		}
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}

int HandleVehicleSell(RpcClient &client, const CliOptions &opts)
{
	try {
		if (opts.args.empty()) {
			std::cerr << "Error: vehicle ID required\n";
			std::cerr << "Usage: ttdctl vehicle sell <id>\n";
			return 1;
		}

		nlohmann::json params = nlohmann::json::object();
		params["vehicle_id"] = std::stoi(opts.args[0]);

		/* Parse optional args */
		for (size_t i = 1; i < opts.args.size(); ++i) {
			if (opts.args[i] == "--no-chain") {
				params["sell_chain"] = false;
			}
		}

		auto result = client.Call("vehicle.sell", params);

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		if (result["success"].get<bool>()) {
			std::cout << "Sold vehicle #" << result["vehicle_id"].get<int>()
			          << " for " << result["value"].get<int64_t>() << "\n";
		} else {
			std::cerr << "Failed to sell vehicle: " << result["error"].get<std::string>() << "\n";
			return 1;
		}
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}

int HandleVehicleClone(RpcClient &client, const CliOptions &opts)
{
	try {
		if (opts.args.empty()) {
			std::cerr << "Error: vehicle ID required\n";
			std::cerr << "Usage: ttdctl vehicle clone <id> --depot <tile> [--share-orders]\n";
			return 1;
		}

		nlohmann::json params = nlohmann::json::object();
		params["vehicle_id"] = std::stoi(opts.args[0]);

		/* Parse options */
		for (size_t i = 1; i < opts.args.size(); ++i) {
			if (opts.args[i] == "--depot" && i + 1 < opts.args.size()) {
				params["depot_tile"] = static_cast<uint32_t>(std::stoul(opts.args[++i]));
			} else if (opts.args[i] == "--depot_x" && i + 1 < opts.args.size()) {
				params["depot_x"] = std::stoi(opts.args[++i]);
			} else if (opts.args[i] == "--depot_y" && i + 1 < opts.args.size()) {
				params["depot_y"] = std::stoi(opts.args[++i]);
			} else if (opts.args[i] == "--share-orders") {
				params["share_orders"] = true;
			}
		}

		if (!params.contains("depot_tile") && !(params.contains("depot_x") && params.contains("depot_y"))) {
			std::cerr << "Error: depot location required (--depot <tile> or --depot_x/--depot_y)\n";
			std::cerr << "Usage: ttdctl vehicle clone <id> --depot <tile> [--share-orders]\n";
			return 1;
		}

		auto result = client.Call("vehicle.clone", params);

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		if (result["success"].get<bool>()) {
			std::cout << "Cloned vehicle #" << result["source_vehicle_id"].get<int>()
			          << " -> #" << result["vehicle_id"].get<int>()
			          << " (" << result["vehicle_name"].get<std::string>() << ")"
			          << " for " << result["cost"].get<int64_t>();
			if (result["share_orders"].get<bool>()) {
				std::cout << " (orders shared)";
			}
			std::cout << "\n";
		} else {
			std::cerr << "Failed to clone vehicle: " << result["error"].get<std::string>() << "\n";
			return 1;
		}
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}

int HandleVehicleAttach(RpcClient &client, const CliOptions &opts)
{
	try {
		if (opts.args.size() < 2) {
			std::cerr << "Error: requires wagon_id and train_id\n";
			std::cerr << "Usage: ttdctl vehicle attach <wagon_id> <train_id>\n";
			std::cerr << "\nAttaches a wagon to a train (both must be in the same depot).\n";
			return 1;
		}

		nlohmann::json params = nlohmann::json::object();
		params["wagon_id"] = std::stoi(opts.args[0]);
		params["train_id"] = std::stoi(opts.args[1]);

		auto result = client.Call("vehicle.attach", params);

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return result["success"].get<bool>() ? 0 : 1;
		}

		if (result["success"].get<bool>()) {
			std::cout << "Successfully attached wagon #" << result["wagon_id"].get<int>()
			          << " to train #" << result["train_id"].get<int>() << "\n";
			return 0;
		} else {
			std::cerr << "Failed to attach wagon: " << result.value("error", "Unknown error") << "\n";
			return 1;
		}
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}
