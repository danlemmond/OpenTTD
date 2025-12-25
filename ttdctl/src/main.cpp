/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file main.cpp Entry point and command routing for ttdctl CLI tool. */

#include "cli_common.h"
#include <iostream>

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
		} else if (opts.action == "newgame") {
			return HandleGameNewGame(client, opts);
		}
	} else if (opts.resource == "company") {
		if (opts.action == "list" || opts.action.empty()) {
			return HandleCompanyList(client, opts);
		} else if (opts.action == "setloan") {
			return HandleCompanySetLoan(client, opts);
		} else if (opts.action == "alerts") {
			return HandleCompanyAlerts(client, opts);
		}
	} else if (opts.resource == "subsidy") {
		if (opts.action == "list" || opts.action.empty()) {
			return HandleSubsidyList(client, opts);
		}
	} else if (opts.resource == "cargo") {
		if (opts.action == "list" || opts.action.empty()) {
			return HandleCargoList(client, opts);
		} else if (opts.action == "income") {
			return HandleCargoGetIncome(client, opts);
		}
	} else if (opts.resource == "vehicle") {
		if (opts.action == "list" || opts.action.empty()) {
			return HandleVehicleList(client, opts);
		} else if (opts.action == "get") {
			return HandleVehicleGet(client, opts);
		} else if (opts.action == "cargo") {
			return HandleVehicleGetCargoByType(client, opts);
		} else if (opts.action == "build") {
			return HandleVehicleBuild(client, opts);
		} else if (opts.action == "sell") {
			return HandleVehicleSell(client, opts);
		} else if (opts.action == "clone") {
			return HandleVehicleClone(client, opts);
		} else if (opts.action == "startstop") {
			return HandleVehicleStartStop(client, opts);
		} else if (opts.action == "depot") {
			return HandleVehicleDepot(client, opts);
		} else if (opts.action == "turnaround") {
			return HandleVehicleTurnaround(client, opts);
		} else if (opts.action == "refit") {
			return HandleVehicleRefit(client, opts);
		} else if (opts.action == "attach") {
			return HandleVehicleAttach(client, opts);
		}
	} else if (opts.resource == "engine") {
		if (opts.action == "list" || opts.action.empty()) {
			return HandleEngineList(client, opts);
		} else if (opts.action == "get") {
			return HandleEngineGet(client, opts);
		}
	} else if (opts.resource == "station") {
		if (opts.action == "list" || opts.action.empty()) {
			return HandleStationList(client, opts);
		} else if (opts.action == "get") {
			return HandleStationGet(client, opts);
		} else if (opts.action == "flow") {
			return HandleStationGetCargoFlow(client, opts);
		} else if (opts.action == "coverage") {
			return HandleStationCoverage(client, opts);
		}
	} else if (opts.resource == "industry") {
		if (opts.action == "list" || opts.action.empty()) {
			return HandleIndustryList(client, opts);
		} else if (opts.action == "get") {
			return HandleIndustryGet(client, opts);
		} else if (opts.action == "stockpile") {
			return HandleIndustryGetStockpile(client, opts);
		} else if (opts.action == "acceptance") {
			return HandleIndustryGetAcceptance(client, opts);
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
		} else if (opts.action == "roadinfo") {
			return HandleTileRoadInfo(client, opts);
		}
	} else if (opts.resource == "road") {
		if (opts.action == "build") {
			return HandleRoadBuild(client, opts);
		} else if (opts.action == "depot") {
			return HandleRoadBuildDepot(client, opts);
		} else if (opts.action == "stop") {
			return HandleRoadBuildStop(client, opts);
		} else if (opts.action == "bridge") {
			return HandleRoadBuildBridge(client, opts);
		} else if (opts.action == "tunnel") {
			return HandleRoadBuildTunnel(client, opts);
		}
	} else if (opts.resource == "rail") {
		if (opts.action == "track") {
			return HandleRailBuildTrack(client, opts);
		} else if (opts.action == "depot") {
			return HandleRailBuildDepot(client, opts);
		} else if (opts.action == "station") {
			return HandleRailBuildStation(client, opts);
		} else if (opts.action == "signal") {
			return HandleRailBuildSignal(client, opts);
		} else if (opts.action == "remove-signal") {
			return HandleRailRemoveSignal(client, opts);
		} else if (opts.action == "track-line") {
			return HandleRailBuildTrackLine(client, opts);
		} else if (opts.action == "signal-line") {
			return HandleRailSignalLine(client, opts);
		} else if (opts.action == "bridge") {
			return HandleRailBuildBridge(client, opts);
		} else if (opts.action == "tunnel") {
			return HandleRailBuildTunnel(client, opts);
		}
	} else if (opts.resource == "bridge") {
		if (opts.action == "list" || opts.action.empty()) {
			return HandleBridgeList(client, opts);
		}
	} else if (opts.resource == "marine") {
		if (opts.action == "dock") {
			return HandleMarineBuildDock(client, opts);
		} else if (opts.action == "depot") {
			return HandleMarineBuildDepot(client, opts);
		}
	} else if (opts.resource == "airport") {
		if (opts.action == "build") {
			return HandleAirportBuild(client, opts);
		} else if (opts.action == "info" || opts.action.empty()) {
			return HandleAirportInfo(client, opts);
		}
	} else if (opts.resource == "route") {
		if (opts.action == "check") {
			return HandleRouteCheck(client, opts);
		}
	} else if (opts.resource == "town") {
		if (opts.action == "list" || opts.action.empty()) {
			return HandleTownList(client, opts);
		} else if (opts.action == "get") {
			return HandleTownGet(client, opts);
		} else if (opts.action == "action") {
			return HandleTownPerformAction(client, opts);
		}
	} else if (opts.resource == "order") {
		if (opts.action == "list" || opts.action.empty()) {
			return HandleOrderList(client, opts);
		} else if (opts.action == "append") {
			return HandleOrderAppend(client, opts);
		} else if (opts.action == "remove") {
			return HandleOrderRemove(client, opts);
		} else if (opts.action == "insert") {
			return HandleOrderInsert(client, opts);
		} else if (opts.action == "setflags") {
			return HandleOrderSetFlags(client, opts);
		} else if (opts.action == "share") {
			return HandleOrderShare(client, opts);
		}
	} else if (opts.resource == "viewport") {
		if (opts.action == "goto") {
			return HandleViewportGoto(client, opts);
		} else if (opts.action == "follow") {
			return HandleViewportFollow(client, opts);
		}
	} else if (opts.resource == "activity") {
		if (opts.action == "hotspot" || opts.action.empty()) {
			return HandleActivityHotspot(client, opts);
		} else if (opts.action == "clear") {
			return HandleActivityClear(client, opts);
		}
	}

	std::cerr << "Unknown command: " << opts.resource;
	if (!opts.action.empty()) std::cerr << " " << opts.action;
	std::cerr << "\n";
	std::cerr << "Try 'ttdctl --help' for usage.\n";
	return 1;
}
