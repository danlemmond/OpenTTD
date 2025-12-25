/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file rpc_handlers.cpp JSON-RPC method handlers for AI agent integration. */

#include "../stdafx.h"
#include "rpc_server.h"
#include "../company_base.h"
#include "../company_func.h"
#include "../timer/timer_game_calendar.h"
#include "../timer/timer_game_economy.h"

#include "../safeguards.h"

static nlohmann::json HandleCompanyStatus([[maybe_unused]] const nlohmann::json &params)
{
	nlohmann::json result = nlohmann::json::array();

	for (const Company *c : Company::Iterate()) {
		nlohmann::json company_json;
		company_json["id"] = c->index.base();
		company_json["is_ai"] = c->is_ai;
		company_json["money"] = c->money.base();
		company_json["current_loan"] = c->current_loan.base();
		company_json["max_loan"] = c->GetMaxLoan().base();
		company_json["inaugurated_year"] = c->inaugurated_year.base();
		company_json["quarters_of_bankruptcy"] = c->months_of_bankruptcy / 3;

		company_json["colour"] = static_cast<int>(c->colour);

		company_json["infrastructure"] = {
			{"rail", c->infrastructure.GetRailTotal()},
			{"road", c->infrastructure.GetRoadTotal()},
			{"tram", c->infrastructure.GetTramTotal()},
			{"water", c->infrastructure.water},
			{"station", c->infrastructure.station},
			{"airport", c->infrastructure.airport},
			{"signal", c->infrastructure.signal}
		};

		company_json["current_economy"] = {
			{"income", c->cur_economy.income.base()},
			{"expenses", c->cur_economy.expenses.base()},
			{"company_value", c->cur_economy.company_value.base()},
			{"performance", c->cur_economy.performance_history}
		};

		result.push_back(company_json);
	}

	return result;
}

static nlohmann::json HandleGameStatus([[maybe_unused]] const nlohmann::json &params)
{
	nlohmann::json result;

	auto ymd = TimerGameCalendar::ConvertDateToYMD(TimerGameCalendar::date);

	result["calendar"] = {
		{"year", TimerGameCalendar::year.base()},
		{"month", TimerGameCalendar::month + 1},
		{"day", ymd.day}
	};

	result["economy"] = {
		{"year", TimerGameEconomy::year.base()}
	};

	return result;
}

static nlohmann::json HandlePing([[maybe_unused]] const nlohmann::json &params)
{
	return {{"pong", true}};
}

void RpcRegisterHandlers(RpcServer &server)
{
	server.RegisterHandler("ping", HandlePing);
	server.RegisterHandler("game.status", HandleGameStatus);
	server.RegisterHandler("company.list", HandleCompanyStatus);
}
