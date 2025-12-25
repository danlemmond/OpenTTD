/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file main.cpp Entry point for ttdctl CLI tool. */

#include "rpc_client.h"

#include <iostream>
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
	std::cout << "\nExamples:\n";
	std::cout << "  ttdctl ping\n";
	std::cout << "  ttdctl game status\n";
	std::cout << "  ttdctl company list\n";
	std::cout << "  ttdctl company list -o json\n";
	std::cout << "  ttdctl vehicle list\n";
	std::cout << "  ttdctl vehicle list road\n";
	std::cout << "  ttdctl vehicle get 42\n";
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
	}

	std::cerr << "Unknown command: " << opts.resource;
	if (!opts.action.empty()) std::cerr << " " << opts.action;
	std::cerr << "\n";
	std::cerr << "Try 'ttdctl --help' for usage.\n";
	return 1;
}
