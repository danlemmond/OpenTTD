/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file cli_common.h Common CLI types and utilities for ttdctl. */

#ifndef CLI_COMMON_H
#define CLI_COMMON_H

#include "rpc_client.h"
#include <string>
#include <vector>

static constexpr const char *VERSION = "0.1.0";
static constexpr const char *DEFAULT_HOST = "localhost";
static constexpr uint16_t DEFAULT_PORT = 9877;

/** Command-line options parsed from arguments. */
struct CliOptions {
	std::string host = DEFAULT_HOST;
	uint16_t port = DEFAULT_PORT;
	std::string resource;
	std::string action;
	std::vector<std::string> args;
	bool help = false;
	bool json_output = false;
};

/** Function signature for command handlers. */
using CommandHandler = int (*)(RpcClient &, const CliOptions &);

/* CLI utilities - cli_common.cpp */
void PrintUsage();
CliOptions ParseArgs(int argc, char *argv[]);
void PrintTable(const std::vector<std::vector<std::string>> &rows);

/* Query commands - commands_query.cpp */
int HandlePing(RpcClient &client, const CliOptions &opts);
int HandleGameStatus(RpcClient &client, const CliOptions &opts);
int HandleCompanyList(RpcClient &client, const CliOptions &opts);
int HandleCompanyAlerts(RpcClient &client, const CliOptions &opts);
int HandleVehicleList(RpcClient &client, const CliOptions &opts);
int HandleVehicleGet(RpcClient &client, const CliOptions &opts);
int HandleStationList(RpcClient &client, const CliOptions &opts);
int HandleStationGet(RpcClient &client, const CliOptions &opts);
int HandleIndustryList(RpcClient &client, const CliOptions &opts);
int HandleIndustryGet(RpcClient &client, const CliOptions &opts);
int HandleIndustryNearest(RpcClient &client, const CliOptions &opts);
int HandleMapInfo(RpcClient &client, const CliOptions &opts);
int HandleMapDistance(RpcClient &client, const CliOptions &opts);
int HandleMapScan(RpcClient &client, const CliOptions &opts);
int HandleMapTerrain(RpcClient &client, const CliOptions &opts);
int HandleTileGet(RpcClient &client, const CliOptions &opts);
int HandleTownList(RpcClient &client, const CliOptions &opts);
int HandleTownGet(RpcClient &client, const CliOptions &opts);
int HandleTownNearest(RpcClient &client, const CliOptions &opts);
int HandleOrderList(RpcClient &client, const CliOptions &opts);
int HandleSubsidyList(RpcClient &client, const CliOptions &opts);
int HandleCargoList(RpcClient &client, const CliOptions &opts);
int HandleCargoGetIncome(RpcClient &client, const CliOptions &opts);
int HandleIndustryGetStockpile(RpcClient &client, const CliOptions &opts);
int HandleIndustryGetAcceptance(RpcClient &client, const CliOptions &opts);
int HandleStationGetCargoFlow(RpcClient &client, const CliOptions &opts);
int HandleStationCoverage(RpcClient &client, const CliOptions &opts);
int HandleVehicleGetCargoByType(RpcClient &client, const CliOptions &opts);
int HandleAirportInfo(RpcClient &client, const CliOptions &opts);
int HandleRouteCheck(RpcClient &client, const CliOptions &opts);

/* Action commands - commands_action.cpp */
int HandleGameNewGame(RpcClient &client, const CliOptions &opts);
int HandleVehicleStartStop(RpcClient &client, const CliOptions &opts);
int HandleVehicleDepot(RpcClient &client, const CliOptions &opts);
int HandleVehicleTurnaround(RpcClient &client, const CliOptions &opts);
int HandleVehicleRefit(RpcClient &client, const CliOptions &opts);
int HandleOrderAppend(RpcClient &client, const CliOptions &opts);
int HandleOrderRemove(RpcClient &client, const CliOptions &opts);
int HandleOrderInsert(RpcClient &client, const CliOptions &opts);
int HandleOrderSetFlags(RpcClient &client, const CliOptions &opts);
int HandleOrderShare(RpcClient &client, const CliOptions &opts);
int HandleCompanySetLoan(RpcClient &client, const CliOptions &opts);
int HandleTownPerformAction(RpcClient &client, const CliOptions &opts);
int HandleViewportGoto(RpcClient &client, const CliOptions &opts);
int HandleViewportFollow(RpcClient &client, const CliOptions &opts);
int HandleActivityHotspot(RpcClient &client, const CliOptions &opts);
int HandleActivityClear(RpcClient &client, const CliOptions &opts);

/* Infrastructure commands - commands_infra.cpp */
int HandleTileRoadInfo(RpcClient &client, const CliOptions &opts);
int HandleRoadBuild(RpcClient &client, const CliOptions &opts);
int HandleRoadBuildDepot(RpcClient &client, const CliOptions &opts);
int HandleRoadBuildStop(RpcClient &client, const CliOptions &opts);
int HandleRoadBuildLine(RpcClient &client, const CliOptions &opts);
int HandleRoadConnect(RpcClient &client, const CliOptions &opts);
int HandleRailBuildTrack(RpcClient &client, const CliOptions &opts);
int HandleRailBuildDepot(RpcClient &client, const CliOptions &opts);
int HandleRailBuildStation(RpcClient &client, const CliOptions &opts);
int HandleRailBuildSignal(RpcClient &client, const CliOptions &opts);
int HandleRailRemoveSignal(RpcClient &client, const CliOptions &opts);
int HandleRailBuildTrackLine(RpcClient &client, const CliOptions &opts);
int HandleRailSignalLine(RpcClient &client, const CliOptions &opts);
int HandleMarineBuildDock(RpcClient &client, const CliOptions &opts);
int HandleMarineBuildDepot(RpcClient &client, const CliOptions &opts);
int HandleAirportBuild(RpcClient &client, const CliOptions &opts);
int HandleBridgeList(RpcClient &client, const CliOptions &opts);
int HandleRailBuildBridge(RpcClient &client, const CliOptions &opts);
int HandleRoadBuildBridge(RpcClient &client, const CliOptions &opts);
int HandleRailBuildTunnel(RpcClient &client, const CliOptions &opts);
int HandleRoadBuildTunnel(RpcClient &client, const CliOptions &opts);

/* Vehicle management commands - commands_vehicle.cpp */
int HandleEngineList(RpcClient &client, const CliOptions &opts);
int HandleEngineGet(RpcClient &client, const CliOptions &opts);
int HandleVehicleBuild(RpcClient &client, const CliOptions &opts);
int HandleVehicleSell(RpcClient &client, const CliOptions &opts);
int HandleVehicleClone(RpcClient &client, const CliOptions &opts);
int HandleVehicleAttach(RpcClient &client, const CliOptions &opts);

#endif /* CLI_COMMON_H */
