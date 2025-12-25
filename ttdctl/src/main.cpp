/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file main.cpp Entry point for ttdctl CLI tool. */

#include "rpc_client.h"

#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <cstring>

static constexpr const char *VERSION = "0.1.0";
static constexpr const char *DEFAULT_HOST = "localhost";
static constexpr uint16_t DEFAULT_PORT = 9877;

struct CliOptions {
	std::string host = DEFAULT_HOST;
	uint16_t port = DEFAULT_PORT;
	std::string resource;
	std::string action;
	std::vector<std::string> args;
	bool help = false;
	bool json_output = false;
};

static void PrintUsage()
{
	std::cout << "ttdctl - OpenTTD CLI control tool v" << VERSION << "\n\n";
	std::cout << "Usage: ttdctl [options] <resource> <action> [args...]\n\n";
	std::cout << "Options:\n";
	std::cout << "  -h, --help          Show this help message\n";
	std::cout << "  -H, --host <host>   Server host (default: localhost)\n";
	std::cout << "  -p, --port <port>   Server port (default: 9877)\n";
	std::cout << "  -o, --output <fmt>  Output format: table, json (default: table)\n\n";
	std::cout << "Resources:\n";
	std::cout << "  ping                Test connection to game\n";
	std::cout << "  game                Game status and control\n";
	std::cout << "  company             Company information\n";
	std::cout << "  vehicle             Vehicle information\n";
	std::cout << "  station             Station information\n";
	std::cout << "  industry            Industry information\n";
	std::cout << "  map                 Map information and distance\n";
	std::cout << "  tile                Tile information\n";
	std::cout << "  town                Town information\n";
	std::cout << "  order               Vehicle order information\n";
	std::cout << "\nExamples:\n";
	std::cout << "  ttdctl ping\n";
	std::cout << "  ttdctl game status\n";
	std::cout << "  ttdctl company list\n";
	std::cout << "  ttdctl vehicle list road\n";
	std::cout << "  ttdctl vehicle get 42\n";
	std::cout << "  ttdctl station list\n";
	std::cout << "  ttdctl station get 5\n";
	std::cout << "  ttdctl industry list\n";
	std::cout << "  ttdctl industry get 3\n";
	std::cout << "  ttdctl map info\n";
	std::cout << "  ttdctl map distance 100 100 200 200\n";
	std::cout << "  ttdctl map scan [--traffic] [--zoom N]\n";
	std::cout << "  ttdctl tile get 100 100\n";
	std::cout << "  ttdctl town list\n";
	std::cout << "  ttdctl town get 0\n";
	std::cout << "  ttdctl order list 42\n";
}

static CliOptions ParseArgs(int argc, char *argv[])
{
	CliOptions opts;

	for (int i = 1; i < argc; ++i) {
		std::string arg = argv[i];

		if (arg == "-h" || arg == "--help") {
			opts.help = true;
		} else if (arg == "-H" || arg == "--host") {
			if (i + 1 < argc) opts.host = argv[++i];
		} else if (arg == "-p" || arg == "--port") {
			if (i + 1 < argc) opts.port = static_cast<uint16_t>(std::stoi(argv[++i]));
		} else if (arg == "-o" || arg == "--output") {
			if (i + 1 < argc) {
				std::string fmt = argv[++i];
				opts.json_output = (fmt == "json");
			}
		} else if (arg[0] != '-') {
			if (opts.resource.empty()) {
				opts.resource = arg;
			} else if (opts.action.empty()) {
				opts.action = arg;
			} else {
				opts.args.push_back(arg);
			}
		}
	}

	return opts;
}

static void PrintTable(const std::vector<std::vector<std::string>> &rows)
{
	if (rows.empty()) return;

	std::vector<size_t> widths(rows[0].size(), 0);
	for (const auto &row : rows) {
		for (size_t i = 0; i < row.size() && i < widths.size(); ++i) {
			widths[i] = std::max(widths[i], row[i].size());
		}
	}

	for (const auto &row : rows) {
		for (size_t i = 0; i < row.size(); ++i) {
			std::cout << row[i];
			if (i + 1 < row.size()) {
				std::cout << std::string(widths[i] - row[i].size() + 2, ' ');
			}
		}
		std::cout << "\n";
	}
}

static int HandlePing(RpcClient &client, [[maybe_unused]] const CliOptions &opts)
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

static int HandleGameStatus(RpcClient &client, const CliOptions &opts)
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

static int HandleCompanyList(RpcClient &client, const CliOptions &opts)
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

static int HandleVehicleList(RpcClient &client, const CliOptions &opts)
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

static int HandleVehicleGet(RpcClient &client, const CliOptions &opts)
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

static int HandleStationList(RpcClient &client, const CliOptions &opts)
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

static int HandleStationGet(RpcClient &client, const CliOptions &opts)
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

static int HandleIndustryList(RpcClient &client, const CliOptions &opts)
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

static int HandleIndustryGet(RpcClient &client, const CliOptions &opts)
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

static int HandleMapInfo(RpcClient &client, const CliOptions &opts)
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

static int HandleMapDistance(RpcClient &client, const CliOptions &opts)
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

static int HandleTileGet(RpcClient &client, const CliOptions &opts)
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

static int HandleTownList(RpcClient &client, const CliOptions &opts)
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

static int HandleTownGet(RpcClient &client, const CliOptions &opts)
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

static int HandleOrderList(RpcClient &client, const CliOptions &opts)
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

static int HandleMapScan(RpcClient &client, const CliOptions &opts)
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

int main(int argc, char *argv[])
{
	auto opts = ParseArgs(argc, argv);

	if (opts.help || opts.resource.empty()) {
		PrintUsage();
		return opts.help ? 0 : 1;
	}

	RpcClient client(opts.host, opts.port);

	if (opts.resource == "ping") {
		return HandlePing(client, opts);
	} else if (opts.resource == "game") {
		if (opts.action == "status" || opts.action.empty()) {
			return HandleGameStatus(client, opts);
		}
	} else if (opts.resource == "company") {
		if (opts.action == "list" || opts.action.empty()) {
			return HandleCompanyList(client, opts);
		}
	} else if (opts.resource == "vehicle") {
		if (opts.action == "list" || opts.action.empty()) {
			return HandleVehicleList(client, opts);
		} else if (opts.action == "get") {
			return HandleVehicleGet(client, opts);
		}
	} else if (opts.resource == "station") {
		if (opts.action == "list" || opts.action.empty()) {
			return HandleStationList(client, opts);
		} else if (opts.action == "get") {
			return HandleStationGet(client, opts);
		}
	} else if (opts.resource == "industry") {
		if (opts.action == "list" || opts.action.empty()) {
			return HandleIndustryList(client, opts);
		} else if (opts.action == "get") {
			return HandleIndustryGet(client, opts);
		}
	} else if (opts.resource == "map") {
		if (opts.action == "info" || opts.action.empty()) {
			return HandleMapInfo(client, opts);
		} else if (opts.action == "distance") {
			return HandleMapDistance(client, opts);
		} else if (opts.action == "scan") {
			return HandleMapScan(client, opts);
		}
	} else if (opts.resource == "tile") {
		if (opts.action == "get" || opts.action.empty()) {
			return HandleTileGet(client, opts);
		}
	} else if (opts.resource == "town") {
		if (opts.action == "list" || opts.action.empty()) {
			return HandleTownList(client, opts);
		} else if (opts.action == "get") {
			return HandleTownGet(client, opts);
		}
	} else if (opts.resource == "order") {
		if (opts.action == "list" || opts.action.empty()) {
			return HandleOrderList(client, opts);
		}
	}

	std::cerr << "Unknown command: " << opts.resource;
	if (!opts.action.empty()) std::cerr << " " << opts.action;
	std::cerr << "\n";
	std::cerr << "Try 'ttdctl --help' for usage.\n";
	return 1;
}
