/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file commands_query.cpp Query command handlers for ttdctl. */

#include "cli_common.h"
#include <iostream>
#include <iomanip>

int HandlePing(RpcClient &client, const CliOptions &opts)
{
	try {
		auto result = client.Call("ping", {});
		if (result.contains("pong") && result["pong"].get<bool>()) {
			std::cout << "Connected to OpenTTD at " << opts.host << ":" << opts.port << "\n";
			return 0;
		}
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
	}
	return 1;
}

int HandleGameStatus(RpcClient &client, const CliOptions &opts)
{
	try {
		auto result = client.Call("game.status", {});

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		std::cout << "Game Status\n";
		std::cout << "-----------\n";
		if (result.contains("calendar")) {
			auto &cal = result["calendar"];
			std::cout << "Calendar: " << cal["year"].get<int>() << "-"
			          << cal["month"].get<int>() << "-"
			          << cal["day"].get<int>() << "\n";
		}
		if (result.contains("economy")) {
			std::cout << "Economy Year: " << result["economy"]["year"].get<int>() << "\n";
		}
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}

int HandleCompanyList(RpcClient &client, const CliOptions &opts)
{
	try {
		auto result = client.Call("company.list", {});

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		if (result.empty()) {
			std::cout << "No companies found.\n";
			return 0;
		}

		std::vector<std::vector<std::string>> rows;
		rows.push_back({"ID", "AI", "Money", "Loan", "Value", "Perf"});

		for (const auto &c : result) {
			rows.push_back({
				std::to_string(c["id"].get<int>()),
				c["is_ai"].get<bool>() ? "Yes" : "No",
				std::to_string(c["money"].get<int64_t>()),
				std::to_string(c["current_loan"].get<int64_t>()),
				std::to_string(c["current_economy"]["company_value"].get<int64_t>()),
				std::to_string(c["current_economy"]["performance"].get<int>())
			});
		}

		PrintTable(rows);
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}

int HandleVehicleList(RpcClient &client, const CliOptions &opts)
{
	try {
		nlohmann::json params;
		if (!opts.args.empty()) {
			params["type"] = opts.args[0];
		}

		auto result = client.Call("vehicle.list", params);

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		if (result.empty()) {
			std::cout << "No vehicles found.\n";
			return 0;
		}

		std::vector<std::vector<std::string>> rows;
		rows.push_back({"ID", "Type", "Name", "State", "Speed", "Profit"});

		for (const auto &v : result) {
			rows.push_back({
				std::to_string(v["id"].get<int>()),
				v["type"].get<std::string>(),
				v["name"].get<std::string>(),
				v["state"].get<std::string>(),
				std::to_string(v["speed"].get<int>()) + "/" + std::to_string(v["max_speed"].get<int>()),
				std::to_string(v["profit_this_year"].get<int64_t>())
			});
		}

		PrintTable(rows);
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}

int HandleVehicleGet(RpcClient &client, const CliOptions &opts)
{
	try {
		if (opts.args.empty()) {
			std::cerr << "Error: vehicle ID required\n";
			std::cerr << "Usage: ttdctl vehicle get <id>\n";
			return 1;
		}

		nlohmann::json params;
		params["id"] = std::stoi(opts.args[0]);

		auto result = client.Call("vehicle.get", params);

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		std::cout << "Vehicle #" << result["id"].get<int>() << "\n";
		std::cout << "---------------\n";
		std::cout << "Name:        " << result["name"].get<std::string>() << "\n";
		std::cout << "Type:        " << result["type"].get<std::string>() << "\n";
		std::cout << "Owner:       " << result["owner"].get<int>() << "\n";
		std::cout << "State:       " << result["state"].get<std::string>() << "\n";
		std::cout << "Speed:       " << result["speed"].get<int>() << "/" << result["max_speed"].get<int>() << "\n";
		std::cout << "Age:         " << result["age_days"].get<int>() << " days\n";
		std::cout << "Reliability: " << result["reliability"].get<int>() << "%\n";
		std::cout << "Value:       " << result["value"].get<int64_t>() << "\n";
		std::cout << "Profit (Y):  " << result["profit_this_year"].get<int64_t>() << "\n";
		std::cout << "Profit (LY): " << result["profit_last_year"].get<int64_t>() << "\n";
		std::cout << "Cargo Cap:   " << result["cargo_capacity"].get<int>() << "\n";
		std::cout << "Cargo Load:  " << result["cargo_count"].get<int>() << "\n";
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}

int HandleStationList(RpcClient &client, const CliOptions &opts)
{
	try {
		auto result = client.Call("station.list", {});

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		if (result.empty()) {
			std::cout << "No stations found.\n";
			return 0;
		}

		std::vector<std::vector<std::string>> rows;
		rows.push_back({"ID", "Name", "Facilities", "Cargo"});

		for (const auto &s : result) {
			std::string facilities;
			for (const auto &f : s["facilities"]) {
				if (!facilities.empty()) facilities += ",";
				facilities += f.get<std::string>();
			}
			rows.push_back({
				std::to_string(s["id"].get<int>()),
				s["name"].get<std::string>(),
				facilities,
				std::to_string(s["cargo_waiting_total"].get<int>())
			});
		}

		PrintTable(rows);
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}

int HandleStationGet(RpcClient &client, const CliOptions &opts)
{
	try {
		if (opts.args.empty()) {
			std::cerr << "Error: station ID required\n";
			std::cerr << "Usage: ttdctl station get <id>\n";
			return 1;
		}

		nlohmann::json params;
		params["id"] = std::stoi(opts.args[0]);

		auto result = client.Call("station.get", params);

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		std::cout << "Station #" << result["id"].get<int>() << "\n";
		std::cout << "---------------\n";
		std::cout << "Name:       " << result["name"].get<std::string>() << "\n";
		std::cout << "Owner:      " << result["owner"].get<int>() << "\n";
		std::cout << "Location:   (" << result["location"]["x"].get<int>() << ", " << result["location"]["y"].get<int>() << ")\n";
		std::cout << "Facilities: ";
		for (size_t i = 0; i < result["facilities"].size(); ++i) {
			if (i > 0) std::cout << ", ";
			std::cout << result["facilities"][i].get<std::string>();
		}
		std::cout << "\n\nCargo:\n";
		for (const auto &c : result["cargo"]) {
			std::cout << "  " << c["cargo_name"].get<std::string>() << ": "
			          << c["waiting"].get<int>() << " waiting";
			if (c["rating"].get<int>() >= 0) {
				std::cout << " (rating: " << c["rating"].get<int>() << "%)";
			}
			std::cout << "\n";
		}
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}

int HandleIndustryList(RpcClient &client, const CliOptions &opts)
{
	try {
		auto result = client.Call("industry.list", {});

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		if (result.empty()) {
			std::cout << "No industries found.\n";
			return 0;
		}

		std::vector<std::vector<std::string>> rows;
		rows.push_back({"ID", "Name", "Town", "Produces", "Accepts"});

		for (const auto &ind : result) {
			std::string produces;
			for (const auto &p : ind["produces"]) {
				if (!produces.empty()) produces += ", ";
				produces += p["cargo_name"].get<std::string>();
			}
			std::string accepts;
			for (const auto &a : ind["accepts"]) {
				if (!accepts.empty()) accepts += ", ";
				accepts += a["cargo_name"].get<std::string>();
			}
			rows.push_back({
				std::to_string(ind["id"].get<int>()),
				ind["name"].get<std::string>(),
				ind.contains("town") ? ind["town"].get<std::string>() : "-",
				produces.empty() ? "-" : produces,
				accepts.empty() ? "-" : accepts
			});
		}

		PrintTable(rows);
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}

int HandleIndustryGet(RpcClient &client, const CliOptions &opts)
{
	try {
		if (opts.args.empty()) {
			std::cerr << "Error: industry ID required\n";
			std::cerr << "Usage: ttdctl industry get <id>\n";
			return 1;
		}

		nlohmann::json params;
		params["id"] = std::stoi(opts.args[0]);

		auto result = client.Call("industry.get", params);

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		std::cout << "Industry #" << result["id"].get<int>() << "\n";
		std::cout << "---------------\n";
		std::cout << "Name:       " << result["name"].get<std::string>() << "\n";
		if (result.contains("town")) {
			std::cout << "Town:       " << result["town"].get<std::string>() << "\n";
		}
		std::cout << "Location:   (" << result["location"]["x"].get<int>() << ", " << result["location"]["y"].get<int>() << ")\n";
		std::cout << "Size:       " << result["location"]["width"].get<int>() << "x" << result["location"]["height"].get<int>() << "\n";
		std::cout << "Prod Level: " << result["production_level"].get<int>() << "\n";
		std::cout << "Stations:   " << result["stations_nearby"].get<int>() << " nearby\n";

		if (!result["produces"].empty()) {
			std::cout << "\nProduces:\n";
			for (const auto &p : result["produces"]) {
				std::cout << "  " << p["cargo_name"].get<std::string>() << ": "
				          << p["waiting"].get<int>() << " waiting, rate " << p["rate"].get<int>();
				if (p.contains("last_month_production")) {
					std::cout << " (last month: " << p["last_month_production"].get<int>()
					          << " produced, " << p["last_month_transported"].get<int>() << " transported)";
				}
				std::cout << "\n";
			}
		}

		if (!result["accepts"].empty()) {
			std::cout << "\nAccepts:\n";
			for (const auto &a : result["accepts"]) {
				std::cout << "  " << a["cargo_name"].get<std::string>() << ": "
				          << a["waiting"].get<int>() << " waiting\n";
			}
		}

		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}

int HandleMapInfo(RpcClient &client, const CliOptions &opts)
{
	try {
		auto result = client.Call("map.info", {});

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		std::cout << "Map Information\n";
		std::cout << "---------------\n";
		std::cout << "Size:    " << result["size_x"].get<int>() << " x " << result["size_y"].get<int>() << "\n";
		std::cout << "Tiles:   " << result["size_total"].get<int>() << "\n";
		std::cout << "Climate: " << result["climate"].get<std::string>() << "\n";
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}

int HandleMapDistance(RpcClient &client, const CliOptions &opts)
{
	try {
		if (opts.args.size() < 4) {
			std::cerr << "Error: requires 4 coordinates: x1 y1 x2 y2\n";
			std::cerr << "Usage: ttdctl map distance <x1> <y1> <x2> <y2>\n";
			return 1;
		}

		nlohmann::json params;
		params["x1"] = std::stoi(opts.args[0]);
		params["y1"] = std::stoi(opts.args[1]);
		params["x2"] = std::stoi(opts.args[2]);
		params["y2"] = std::stoi(opts.args[3]);

		auto result = client.Call("map.distance", params);

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		std::cout << "Distance from (" << opts.args[0] << "," << opts.args[1]
		          << ") to (" << opts.args[2] << "," << opts.args[3] << "):\n";
		std::cout << "  Manhattan: " << result["manhattan"].get<int>() << " tiles\n";
		std::cout << "  Max:       " << result["max"].get<int>() << " tiles\n";
		std::cout << "  Square:    " << result["square"].get<int>() << "\n";
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}

int HandleMapScan(RpcClient &client, const CliOptions &opts)
{
	try {
		nlohmann::json params;

		/* Parse options from args */
		bool show_traffic = false;
		int zoom = 8;

		for (size_t i = 0; i < opts.args.size(); ++i) {
			if (opts.args[i] == "--traffic" || opts.args[i] == "-t") {
				show_traffic = true;
			} else if ((opts.args[i] == "--zoom" || opts.args[i] == "-z") && i + 1 < opts.args.size()) {
				zoom = std::stoi(opts.args[++i]);
			}
		}

		params["traffic"] = show_traffic;
		params["zoom"] = zoom;

		auto result = client.Call("map.scan", params);

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		/* Print header */
		int origin_x = result["origin"]["x"].get<int>();
		int origin_y = result["origin"]["y"].get<int>();
		int grid_size = result["grid_size"].get<int>();
		int actual_zoom = result["zoom"].get<int>();

		std::cout << "Map Scan";
		if (result["show_traffic"].get<bool>()) {
			std::cout << " (traffic overlay)";
		}
		std::cout << "\n";
		std::cout << "Origin: (" << origin_x << ", " << origin_y << ")  ";
		std::cout << "Zoom: " << actual_zoom << "x (" << actual_zoom << " tiles/cell)  ";
		std::cout << "Coverage: " << (grid_size * actual_zoom) << "x" << (grid_size * actual_zoom) << " tiles\n\n";

		/* Print column headers */
		std::cout << "     ";
		for (int col = 0; col < grid_size; ++col) {
			if (col % 4 == 0) {
				int x = origin_x + col * actual_zoom;
				std::cout << std::setw(4) << x;
			} else {
				std::cout << "    ";
			}
		}
		std::cout << "  X\n";

		/* Print grid */
		const auto &rows = result["rows"];
		for (size_t i = 0; i < rows.size(); ++i) {
			int y = origin_y + static_cast<int>(i) * actual_zoom;
			if (i % 4 == 0) {
				std::cout << std::setw(4) << y << " ";
			} else {
				std::cout << "     ";
			}

			std::string row = rows[i].get<std::string>();
			for (char c : row) {
				std::cout << "  " << c << " ";
			}
			std::cout << "\n";
		}
		std::cout << "Y\n\n";

		/* Print legend */
		std::cout << "Legend:\n";
		for (const auto &entry : result["legend"]) {
			std::cout << "  " << entry["symbol"].get<std::string>() << " = " << entry["label"].get<std::string>() << "\n";
		}

		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}

int HandleTileGet(RpcClient &client, const CliOptions &opts)
{
	try {
		if (opts.args.size() < 2) {
			std::cerr << "Error: requires coordinates: x y\n";
			std::cerr << "Usage: ttdctl tile get <x> <y>\n";
			return 1;
		}

		nlohmann::json params;
		params["x"] = std::stoi(opts.args[0]);
		params["y"] = std::stoi(opts.args[1]);

		auto result = client.Call("tile.get", params);

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		std::cout << "Tile at (" << result["x"].get<int>() << ", " << result["y"].get<int>() << ")\n";
		std::cout << "---------------\n";
		std::cout << "Tile ID: " << result["tile"].get<int>() << "\n";
		std::cout << "Type:    " << result["type"].get<std::string>() << "\n";
		std::cout << "Height:  " << result["height"].get<int>() << "\n";
		std::cout << "Flat:    " << (result["is_flat"].get<bool>() ? "Yes" : "No") << "\n";
		int owner = result["owner"].get<int>();
		std::cout << "Owner:   " << (owner >= 0 ? std::to_string(owner) : "None") << "\n";
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}

int HandleTownList(RpcClient &client, const CliOptions &opts)
{
	try {
		auto result = client.Call("town.list", {});

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		if (result.empty()) {
			std::cout << "No towns found.\n";
			return 0;
		}

		std::vector<std::vector<std::string>> rows;
		rows.push_back({"ID", "Name", "Population", "Houses", "City", "Location"});

		for (const auto &t : result) {
			rows.push_back({
				std::to_string(t["id"].get<int>()),
				t["name"].get<std::string>(),
				std::to_string(t["population"].get<int>()),
				std::to_string(t["houses"].get<int>()),
				t["is_city"].get<bool>() ? "Yes" : "No",
				"(" + std::to_string(t["location"]["x"].get<int>()) + "," + std::to_string(t["location"]["y"].get<int>()) + ")"
			});
		}

		PrintTable(rows);
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}

int HandleTownGet(RpcClient &client, const CliOptions &opts)
{
	try {
		if (opts.args.empty()) {
			std::cerr << "Error: town ID required\n";
			std::cerr << "Usage: ttdctl town get <id>\n";
			return 1;
		}

		nlohmann::json params;
		params["id"] = std::stoi(opts.args[0]);

		auto result = client.Call("town.get", params);

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		std::cout << "Town #" << result["id"].get<int>() << "\n";
		std::cout << "---------------\n";
		std::cout << "Name:       " << result["name"].get<std::string>() << "\n";
		std::cout << "Population: " << result["population"].get<int>() << "\n";
		std::cout << "Houses:     " << result["houses"].get<int>() << "\n";
		std::cout << "City:       " << (result["is_city"].get<bool>() ? "Yes" : "No") << "\n";
		std::cout << "Location:   (" << result["location"]["x"].get<int>() << ", " << result["location"]["y"].get<int>() << ")\n";

		int growth_rate = result["growth_rate"].get<int>();
		if (growth_rate >= 0) {
			std::cout << "Growth:     Every " << growth_rate << " days\n";
		} else {
			std::cout << "Growth:     Not growing\n";
		}

		if (!result["ratings"].empty()) {
			std::cout << "\nCompany Ratings:\n";
			for (const auto &r : result["ratings"]) {
				std::cout << "  Company " << r["company"].get<int>() << ": " << r["rating"].get<int>() << "\n";
			}
		}

		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}

int HandleOrderList(RpcClient &client, const CliOptions &opts)
{
	try {
		if (opts.args.empty()) {
			std::cerr << "Error: vehicle ID required\n";
			std::cerr << "Usage: ttdctl order list <vehicle_id>\n";
			return 1;
		}

		nlohmann::json params;
		params["vehicle_id"] = std::stoi(opts.args[0]);

		auto result = client.Call("order.list", params);

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		std::cout << "Orders for " << result["vehicle_name"].get<std::string>()
		          << " (#" << result["vehicle_id"].get<int>() << ")\n";
		std::cout << "-------------------------------------------\n";
		std::cout << "Total orders: " << result["num_orders"].get<int>();
		if (result["is_shared"].get<bool>()) {
			std::cout << " (shared with " << result["num_vehicles_sharing"].get<int>() << " vehicles)";
		}
		std::cout << "\n";
		std::cout << "Current order: #" << result["current_order_index"].get<int>() << "\n\n";

		for (const auto &order : result["orders"]) {
			int idx = order["index"].get<int>();
			std::string type = order["type"].get<std::string>();
			bool is_current = (idx == result["current_order_index"].get<int>());

			std::cout << (is_current ? ">> " : "   ");
			std::cout << "#" << idx << " " << type;

			if (order.contains("destination_name")) {
				std::cout << " -> " << order["destination_name"].get<std::string>();
			}

			if (order.contains("load_type")) {
				std::cout << " [" << order["load_type"].get<std::string>();
				std::cout << "/" << order["unload_type"].get<std::string>() << "]";
			}

			if (order.contains("non_stop") && order["non_stop"].get<bool>()) {
				std::cout << " (non-stop)";
			}
			if (order.contains("via") && order["via"].get<bool>()) {
				std::cout << " (via)";
			}

			std::cout << "\n";
		}

		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}

int HandleSubsidyList(RpcClient &client, const CliOptions &opts)
{
	try {
		auto result = client.Call("subsidy.list", {});

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		if (result.empty()) {
			std::cout << "No subsidies available.\n";
			return 0;
		}

		std::vector<std::vector<std::string>> rows;
		rows.push_back({"ID", "Cargo", "From", "To", "Months", "Awarded"});

		for (const auto &s : result) {
			std::string from = s["source"]["type"].get<std::string>() + ": ";
			if (s["source"].contains("name")) {
				from += s["source"]["name"].get<std::string>();
			}
			std::string to = s["destination"]["type"].get<std::string>() + ": ";
			if (s["destination"].contains("name")) {
				to += s["destination"]["name"].get<std::string>();
			}
			rows.push_back({
				std::to_string(s["id"].get<int>()),
				s["cargo_name"].get<std::string>(),
				from,
				to,
				std::to_string(s["remaining_months"].get<int>()),
				s["is_awarded"].get<bool>() ? ("Co." + std::to_string(s["awarded_to"].get<int>())) : "No"
			});
		}

		PrintTable(rows);
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}

int HandleCargoList(RpcClient &client, const CliOptions &opts)
{
	try {
		auto result = client.Call("cargo.list", {});

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		if (result.empty()) {
			std::cout << "No cargo types found.\n";
			return 0;
		}

		std::vector<std::vector<std::string>> rows;
		rows.push_back({"ID", "Label", "Name", "Freight", "Town Effect"});

		for (const auto &c : result) {
			rows.push_back({
				std::to_string(c["id"].get<int>()),
				c["label"].get<std::string>(),
				c["name"].get<std::string>(),
				c["is_freight"].get<bool>() ? "Yes" : "No",
				c["town_effect"].get<std::string>()
			});
		}

		PrintTable(rows);
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}

int HandleCargoGetIncome(RpcClient &client, const CliOptions &opts)
{
	try {
		if (opts.args.size() < 3) {
			std::cerr << "Error: requires cargo_type distance days_in_transit [amount]\n";
			std::cerr << "Usage: ttdctl cargo income <cargo_type> <distance> <days> [amount]\n";
			return 1;
		}

		nlohmann::json params;
		params["cargo_type"] = std::stoi(opts.args[0]);
		params["distance"] = std::stoi(opts.args[1]);
		params["days_in_transit"] = std::stoi(opts.args[2]);
		if (opts.args.size() > 3) {
			params["amount"] = std::stoi(opts.args[3]);
		}

		auto result = client.Call("cargo.getIncome", params);

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		std::cout << "Cargo Income Calculation\n";
		std::cout << "------------------------\n";
		std::cout << "Cargo Type:      " << result["cargo_type"].get<int>() << "\n";
		std::cout << "Distance:        " << result["distance"].get<int>() << " tiles\n";
		std::cout << "Days in Transit: " << result["days_in_transit"].get<int>() << "\n";
		std::cout << "Amount:          " << result["amount"].get<int>() << " units\n";
		std::cout << "Income:          " << result["income"].get<int64_t>() << "\n";
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}

int HandleIndustryGetStockpile(RpcClient &client, const CliOptions &opts)
{
	try {
		if (opts.args.empty()) {
			std::cerr << "Error: industry ID required\n";
			std::cerr << "Usage: ttdctl industry stockpile <id>\n";
			return 1;
		}

		nlohmann::json params;
		params["id"] = std::stoi(opts.args[0]);

		auto result = client.Call("industry.getStockpile", params);

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		std::cout << result["name"].get<std::string>() << " (#" << result["id"].get<int>() << ") Stockpile:\n";
		if (result["stockpile"].empty()) {
			std::cout << "  No cargo stockpiled.\n";
		} else {
			for (const auto &s : result["stockpile"]) {
				std::cout << "  " << s["cargo_name"].get<std::string>() << ": "
				          << s["stockpiled"].get<int>() << " units\n";
			}
		}
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}

int HandleIndustryGetAcceptance(RpcClient &client, const CliOptions &opts)
{
	try {
		if (opts.args.empty()) {
			std::cerr << "Error: industry ID required\n";
			std::cerr << "Usage: ttdctl industry acceptance <id>\n";
			return 1;
		}

		nlohmann::json params;
		params["id"] = std::stoi(opts.args[0]);

		auto result = client.Call("industry.getAcceptance", params);

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		std::cout << result["name"].get<std::string>() << " (#" << result["id"].get<int>() << ") Acceptance:\n";
		if (result["acceptance"].empty()) {
			std::cout << "  Does not accept any cargo.\n";
		} else {
			for (const auto &a : result["acceptance"]) {
				std::cout << "  " << a["cargo_name"].get<std::string>() << ": "
				          << a["state"].get<std::string>() << "\n";
			}
		}
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}

int HandleStationGetCargoFlow(RpcClient &client, const CliOptions &opts)
{
	try {
		if (opts.args.empty()) {
			std::cerr << "Error: station ID required\n";
			std::cerr << "Usage: ttdctl station flow <id>\n";
			return 1;
		}

		nlohmann::json params;
		params["id"] = std::stoi(opts.args[0]);

		auto result = client.Call("station.getCargoPlanned", params);

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		std::cout << result["name"].get<std::string>() << " (#" << result["id"].get<int>() << ") Cargo Flow:\n";
		if (result["cargo"].empty()) {
			std::cout << "  No cargo data.\n";
		} else {
			std::vector<std::vector<std::string>> rows;
			rows.push_back({"Cargo", "Waiting", "Rating", "Capacity", "Usage"});
			for (const auto &c : result["cargo"]) {
				rows.push_back({
					c["cargo_name"].get<std::string>(),
					std::to_string(c["waiting"].get<int>()),
					c["rating"].get<int>() >= 0 ? (std::to_string(c["rating"].get<int>()) + "%") : "-",
					std::to_string(c["link_capacity"].get<int>()),
					std::to_string(c["link_usage"].get<int>())
				});
			}
			PrintTable(rows);
		}
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}

int HandleVehicleGetCargoByType(RpcClient &client, const CliOptions &opts)
{
	try {
		if (opts.args.empty()) {
			std::cerr << "Error: vehicle ID required\n";
			std::cerr << "Usage: ttdctl vehicle cargo <id>\n";
			return 1;
		}

		nlohmann::json params;
		params["id"] = std::stoi(opts.args[0]);

		auto result = client.Call("vehicle.getCargoByType", params);

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		std::cout << result["name"].get<std::string>() << " (#" << result["id"].get<int>() << ") Cargo:\n";
		std::cout << "Type: " << result["type"].get<std::string>() << "\n\n";

		if (result["cargo"].empty()) {
			std::cout << "  No cargo capacity.\n";
		} else {
			std::vector<std::vector<std::string>> rows;
			rows.push_back({"Cargo", "Loaded", "Capacity", "Util %"});
			for (const auto &c : result["cargo"]) {
				rows.push_back({
					c["cargo_name"].get<std::string>(),
					std::to_string(c["loaded"].get<int>()),
					std::to_string(c["capacity"].get<int>()),
					std::to_string(c["utilization_pct"].get<int>()) + "%"
				});
			}
			PrintTable(rows);
		}

		std::cout << "\nTotal: " << result["total_loaded"].get<int>() << "/"
		          << result["total_capacity"].get<int>() << " ("
		          << result["total_utilization_pct"].get<int>() << "%)\n";
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}

int HandleAirportInfo(RpcClient &client, const CliOptions &opts)
{
	try {
		nlohmann::json params;
		if (!opts.args.empty()) {
			params["type"] = opts.args[0];
		}

		auto result = client.Call("airport.info", params);

		if (opts.json_output) {
			std::cout << result.dump(2) << "\n";
			return 0;
		}

		/* Single airport type */
		if (result.is_object()) {
			std::cout << "Airport: " << result["type"].get<std::string>() << "\n";
			std::cout << "-------------------\n";
			if (!result["available"].get<bool>()) {
				std::cout << "Not available.\n";
				return 0;
			}
			std::cout << "Size:         " << result["width"].get<int>() << "x" << result["height"].get<int>() << "\n";
			std::cout << "Catchment:    " << result["catchment_radius"].get<int>() << "\n";
			std::cout << "Noise:        " << result["noise_level"].get<int>() << "\n";
			std::cout << "Hangars:      " << result["num_hangars"].get<int>() << "\n";
			std::cout << "Heli-only:    " << (result["helicopter_only"].get<bool>() ? "Yes" : "No") << "\n";
			return 0;
		}

		/* List of airports */
		std::vector<std::vector<std::string>> rows;
		rows.push_back({"Type", "Size", "Catchment", "Noise", "Hangars", "Heli-only"});
		for (const auto &a : result) {
			if (!a["available"].get<bool>()) continue;
			rows.push_back({
				a["type"].get<std::string>(),
				std::to_string(a["width"].get<int>()) + "x" + std::to_string(a["height"].get<int>()),
				std::to_string(a["catchment_radius"].get<int>()),
				std::to_string(a["noise_level"].get<int>()),
				std::to_string(a["num_hangars"].get<int>()),
				a["helicopter_only"].get<bool>() ? "Yes" : "No"
			});
		}
		PrintTable(rows);
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}
