/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file cli_common.cpp Common CLI utilities for ttdctl. */

#include "cli_common.h"
#include <iostream>
#include <algorithm>

void PrintUsage()
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
	std::cout << "  vehicle             Vehicle information and control\n";
	std::cout << "  engine              Engine/vehicle type information\n";
	std::cout << "  station             Station information\n";
	std::cout << "  industry            Industry information\n";
	std::cout << "  map                 Map information and distance\n";
	std::cout << "  tile                Tile information\n";
	std::cout << "  town                Town information\n";
	std::cout << "  order               Vehicle order information\n";
	std::cout << "  subsidy             Subsidy opportunities\n";
	std::cout << "  cargo               Cargo types and income calculation\n";
	std::cout << "  road                Road infrastructure building\n";
	std::cout << "  rail                Rail infrastructure building\n";
	std::cout << "  marine              Marine infrastructure (docks, depots)\n";
	std::cout << "  airport             Airport information and building\n";
	std::cout << "  viewport            Camera/viewport control\n";
	std::cout << "  activity            Activity tracking for auto-camera\n";
	std::cout << "\nVehicle Management:\n";
	std::cout << "  vehicle build       Build a new vehicle at a depot\n";
	std::cout << "  vehicle sell        Sell a vehicle (must be in depot)\n";
	std::cout << "  vehicle clone       Clone an existing vehicle\n";
	std::cout << "  vehicle startstop   Toggle vehicle start/stop\n";
	std::cout << "  vehicle depot       Send vehicle to depot\n";
	std::cout << "  vehicle turnaround  Cancel depot order (turn around)\n";
	std::cout << "  vehicle refit       Refit vehicle to different cargo\n";
	std::cout << "\nEngine Queries:\n";
	std::cout << "  engine list         List available engines\n";
	std::cout << "  engine get          Get detailed engine info\n";
	std::cout << "\nCompany Management:\n";
	std::cout << "  company setloan     Set company loan amount\n";
	std::cout << "\nOrder Management:\n";
	std::cout << "  order append        Add order to vehicle (at end)\n";
	std::cout << "  order insert        Insert order at position\n";
	std::cout << "  order remove        Remove order from vehicle\n";
	std::cout << "  order setflags      Modify order load/unload flags\n";
	std::cout << "  order share         Share/copy orders between vehicles\n";
	std::cout << "\nTown Actions:\n";
	std::cout << "  town action         Perform town action (advertise, bribe, etc.)\n";
	std::cout << "\nInfrastructure Actions:\n";
	std::cout << "  tile roadinfo       Get road/rail orientation info for depot placement\n";
	std::cout << "  road build          Build road pieces on a tile\n";
	std::cout << "  road depot          Build a road vehicle depot\n";
	std::cout << "  road stop           Build a bus/truck stop\n";
	std::cout << "  rail track          Build railway track\n";
	std::cout << "  rail depot          Build a train depot\n";
	std::cout << "  rail station        Build a train station\n";
	std::cout << "  rail signal         Build rail signal (block, entry, exit, combo, pbs, pbs_oneway)\n";
	std::cout << "  rail remove-signal  Remove rail signal\n";
	std::cout << "  marine dock         Build a ship dock\n";
	std::cout << "  marine depot        Build a ship depot\n";
	std::cout << "  airport build       Build an airport\n";
	std::cout << "\nEconomic/Analytics Commands:\n";
	std::cout << "  subsidy list        List available subsidies\n";
	std::cout << "  cargo list          List all cargo types\n";
	std::cout << "  cargo income        Calculate income for a route\n";
	std::cout << "  industry stockpile  View cargo stockpiled at industry\n";
	std::cout << "  industry acceptance Check cargo acceptance at industry\n";
	std::cout << "  station flow        View cargo flow at station\n";
	std::cout << "  vehicle cargo       View cargo breakdown for vehicle\n";
	std::cout << "  airport info        List airport types and specs\n";
	std::cout << "\nMeta Commands:\n";
	std::cout << "  game newgame        Start a new game with default settings\n";
	std::cout << "\nCamera/Viewport Control:\n";
	std::cout << "  viewport goto       Scroll viewport to tile coordinates\n";
	std::cout << "  viewport follow     Follow a vehicle with the camera\n";
	std::cout << "  activity hotspot    Get the most active area (for auto-camera)\n";
	std::cout << "  activity clear      Clear activity history\n";
	std::cout << "\nExamples:\n";
	std::cout << "  ttdctl ping\n";
	std::cout << "  ttdctl game status\n";
	std::cout << "  ttdctl game newgame                 # Generate new world\n";
	std::cout << "  ttdctl game newgame --seed 12345    # With specific seed\n";
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
	std::cout << "\n  # Engine/Vehicle Management:\n";
	std::cout << "  ttdctl engine list road              # List road vehicle engines\n";
	std::cout << "  ttdctl engine get 5                  # Get engine details\n";
	std::cout << "  ttdctl vehicle build --engine 5 --depot 12345\n";
	std::cout << "  ttdctl vehicle build --engine 5 --depot_x 100 --depot_y 50\n";
	std::cout << "  ttdctl vehicle sell 42               # Sell vehicle (must be in depot)\n";
	std::cout << "  ttdctl vehicle clone 42 --depot 12345 --share-orders\n";
	std::cout << "\n  # Vehicle Actions:\n";
	std::cout << "  ttdctl vehicle startstop 42\n";
	std::cout << "  ttdctl vehicle depot 42\n";
	std::cout << "  ttdctl vehicle turnaround 42\n";
	std::cout << "  ttdctl vehicle refit 42 --cargo 5\n";
	std::cout << "\n  # Order Management:\n";
	std::cout << "  ttdctl order append 42 --station 5 --load full --unload transfer\n";
	std::cout << "  ttdctl order insert 42 --index 0 --station 5\n";
	std::cout << "  ttdctl order remove 42 --index 1\n";
	std::cout << "  ttdctl order setflags 42 --index 0 --load full\n";
	std::cout << "  ttdctl order share 42 43 --mode share\n";
	std::cout << "\n  # Company & Town:\n";
	std::cout << "  ttdctl company setloan 500000\n";
	std::cout << "  ttdctl town action 0 --action advertise_small\n";
	std::cout << "  ttdctl town action 0 --action bribe\n";
	std::cout << "\n  # Infrastructure:\n";
	std::cout << "  ttdctl tile roadinfo 100 100            # Get road orientation info\n";
	std::cout << "  ttdctl road build 100 100 --pieces x    # Build horizontal road\n";
	std::cout << "  ttdctl road depot 101 100 --direction ne  # Build depot facing NE\n";
	std::cout << "  ttdctl road stop 100 100 --direction se --type bus\n";
	std::cout << "  ttdctl rail track 50 50 --track x       # Build X-axis track\n";
	std::cout << "  ttdctl rail depot 51 50 --direction sw  # Build depot facing SW\n";
	std::cout << "  ttdctl rail station 52 50 --axis x --platforms 2 --length 5\n";
	std::cout << "  ttdctl rail signal 50 50 --track x       # Build block signal (default)\n";
	std::cout << "  ttdctl rail signal 50 50 --track x --type pbs --two-way\n";
	std::cout << "  ttdctl rail signal 50 50 --track y --type entry --variant semaphore\n";
	std::cout << "  ttdctl rail remove-signal 50 50 --track x  # Remove signal\n";
	std::cout << "  ttdctl marine dock 50 50                 # Build dock at sloped coastal tile\n";
	std::cout << "  ttdctl marine depot 60 60 --axis x       # Build ship depot on water\n";
	std::cout << "  ttdctl airport build 100 100 --type small  # Build small airport\n";
	std::cout << "  ttdctl airport build 100 100 --type international  # Build international airport\n";
	std::cout << "\n  # Camera/Viewport Control:\n";
	std::cout << "  ttdctl viewport goto 100 100            # Scroll to coordinates\n";
	std::cout << "  ttdctl viewport goto 100 100 --instant  # Jump instantly\n";
	std::cout << "  ttdctl viewport follow 42               # Follow vehicle 42\n";
	std::cout << "  ttdctl viewport follow --stop           # Stop following\n";
	std::cout << "  ttdctl activity hotspot                 # Find most active area\n";
	std::cout << "  ttdctl activity hotspot --seconds 60    # Look back 60 seconds\n";
	std::cout << "  ttdctl activity clear                   # Clear activity log\n";
}

CliOptions ParseArgs(int argc, char *argv[])
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
		} else if (!opts.action.empty()) {
			/* Pass through unknown options to subcommand after action is set */
			opts.args.push_back(arg);
		}
	}

	return opts;
}

void PrintTable(const std::vector<std::vector<std::string>> &rows)
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
