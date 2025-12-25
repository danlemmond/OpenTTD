/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file rpc_server.h JSON-RPC server for AI agent integration. */

#ifndef RPC_SERVER_H
#define RPC_SERVER_H

#include "../network/core/os_abstraction.h"
#include "../3rdparty/nlohmann/json.hpp"

#include <functional>
#include <map>
#include <string>
#include <vector>

static constexpr uint16_t RPC_DEFAULT_PORT = 9877;

enum class RpcErrorCode : int {
	ParseError = -32700,
	InvalidRequest = -32600,
	MethodNotFound = -32601,
	InvalidParams = -32602,
	InternalError = -32603,
};

using RpcHandler = std::function<nlohmann::json(const nlohmann::json &params)>;

class RpcServer {
public:
	RpcServer();
	~RpcServer();

	RpcServer(const RpcServer &) = delete;
	RpcServer &operator=(const RpcServer &) = delete;

	bool Start(uint16_t port = RPC_DEFAULT_PORT);
	void Stop();
	void Poll();

	void RegisterHandler(const std::string &method, RpcHandler handler);

	bool IsRunning() const { return this->listen_socket != INVALID_SOCKET; }

private:
	struct ClientConnection {
		SOCKET socket = INVALID_SOCKET;
		std::string recv_buffer;
	};

	SOCKET listen_socket = INVALID_SOCKET;
	std::vector<ClientConnection> clients;
	std::map<std::string, RpcHandler> handlers;

	void AcceptNewClients();
	void ProcessClients();
	void ProcessClientData(ClientConnection &client);
	nlohmann::json HandleRequest(const nlohmann::json &request);
	nlohmann::json MakeErrorResponse(const nlohmann::json &id, RpcErrorCode code, const std::string &message);
	nlohmann::json MakeSuccessResponse(const nlohmann::json &id, const nlohmann::json &result);
	void SendResponse(ClientConnection &client, const nlohmann::json &response);
};

void RpcServerStart();
void RpcServerStop();
void RpcServerPoll();
void RpcRegisterHandlers(RpcServer &server);

#endif /* RPC_SERVER_H */
