/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file rpc_client.h JSON-RPC client for ttdctl. */

#ifndef RPC_CLIENT_H
#define RPC_CLIENT_H

#include <nlohmann/json.hpp>
#include <string>
#include <cstdint>

class RpcClient {
public:
	RpcClient(const std::string &host, uint16_t port);
	~RpcClient();

	RpcClient(const RpcClient &) = delete;
	RpcClient &operator=(const RpcClient &) = delete;

	nlohmann::json Call(const std::string &method, const nlohmann::json &params);

private:
	std::string host;
	uint16_t port;
	int next_id = 1;

	std::string SendRequest(const std::string &request);
};

#endif /* RPC_CLIENT_H */
