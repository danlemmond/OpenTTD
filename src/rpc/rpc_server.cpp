/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file rpc_server.cpp Implementation of JSON-RPC server for AI agent integration. */

#include "../stdafx.h"
#include "rpc_server.h"
#include "../debug.h"
#include "../network/core/os_abstraction.h"

#include "../safeguards.h"

static std::unique_ptr<RpcServer> _rpc_server;

RpcServer::RpcServer() = default;

RpcServer::~RpcServer()
{
	this->Stop();
}

bool RpcServer::Start(uint16_t port)
{
	if (this->IsRunning()) return true;

	this->listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (this->listen_socket == INVALID_SOCKET) {
		Debug(net, 0, "[rpc] Failed to create socket: {}", NetworkError::GetLast().AsString());
		return false;
	}

	int reuse = 1;
	if (setsockopt(this->listen_socket, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse)) < 0) {
		Debug(net, 1, "[rpc] Failed to set SO_REUSEADDR");
	}

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = htons(port);

	if (bind(this->listen_socket, (sockaddr *)&addr, sizeof(addr)) < 0) {
		Debug(net, 0, "[rpc] Failed to bind to port {}: {}", port, NetworkError::GetLast().AsString());
		closesocket(this->listen_socket);
		this->listen_socket = INVALID_SOCKET;
		return false;
	}

	if (listen(this->listen_socket, 5) < 0) {
		Debug(net, 0, "[rpc] Failed to listen: {}", NetworkError::GetLast().AsString());
		closesocket(this->listen_socket);
		this->listen_socket = INVALID_SOCKET;
		return false;
	}

	SetNonBlocking(this->listen_socket);

	Debug(net, 1, "[rpc] JSON-RPC server started on localhost:{}", port);
	return true;
}

void RpcServer::Stop()
{
	for (auto &client : this->clients) {
		if (client.socket != INVALID_SOCKET) {
			closesocket(client.socket);
		}
	}
	this->clients.clear();

	if (this->listen_socket != INVALID_SOCKET) {
		closesocket(this->listen_socket);
		this->listen_socket = INVALID_SOCKET;
		Debug(net, 1, "[rpc] JSON-RPC server stopped");
	}
}

void RpcServer::Poll()
{
	if (!this->IsRunning()) return;

	this->AcceptNewClients();
	this->ProcessClients();
}

void RpcServer::RegisterHandler(const std::string &method, RpcHandler handler)
{
	this->handlers[method] = std::move(handler);
}

void RpcServer::AcceptNewClients()
{
	for (;;) {
		sockaddr_in client_addr{};
		socklen_t addr_len = sizeof(client_addr);
		SOCKET client_socket = accept(this->listen_socket, (sockaddr *)&client_addr, &addr_len);

		if (client_socket == INVALID_SOCKET) break;

		SetNonBlocking(client_socket);
		SetNoDelay(client_socket);

		this->clients.emplace_back(ClientConnection{client_socket, {}});
		Debug(net, 2, "[rpc] Client connected");
	}
}

void RpcServer::ProcessClients()
{
	for (auto it = this->clients.begin(); it != this->clients.end();) {
		char buffer[4096];
		ssize_t received = recv(it->socket, buffer, sizeof(buffer) - 1, 0);

		if (received > 0) {
			buffer[received] = '\0';
			it->recv_buffer.append(buffer, received);
			this->ProcessClientData(*it);
			++it;
		} else if (received == 0) {
			Debug(net, 2, "[rpc] Client disconnected");
			closesocket(it->socket);
			it = this->clients.erase(it);
		} else {
			auto err = NetworkError::GetLast();
			if (err.WouldBlock()) {
				++it;
			} else {
				Debug(net, 2, "[rpc] Client error: {}", err.AsString());
				closesocket(it->socket);
				it = this->clients.erase(it);
			}
		}
	}
}

void RpcServer::ProcessClientData(ClientConnection &client)
{
	size_t pos;
	while ((pos = client.recv_buffer.find('\n')) != std::string::npos) {
		std::string line = client.recv_buffer.substr(0, pos);
		client.recv_buffer.erase(0, pos + 1);

		if (line.empty() || (line.size() == 1 && line[0] == '\r')) continue;
		if (!line.empty() && line.back() == '\r') line.pop_back();

		try {
			nlohmann::json request = nlohmann::json::parse(line);
			nlohmann::json response = this->HandleRequest(request);
			this->SendResponse(client, response);
		} catch (const nlohmann::json::parse_error &e) {
			nlohmann::json response = this->MakeErrorResponse(nullptr, RpcErrorCode::ParseError, e.what());
			this->SendResponse(client, response);
		}
	}
}

nlohmann::json RpcServer::HandleRequest(const nlohmann::json &request)
{
	nlohmann::json id = nullptr;
	if (request.contains("id")) {
		id = request["id"];
	}

	if (!request.contains("jsonrpc") || request["jsonrpc"] != "2.0") {
		return this->MakeErrorResponse(id, RpcErrorCode::InvalidRequest, "Invalid JSON-RPC version");
	}

	if (!request.contains("method") || !request["method"].is_string()) {
		return this->MakeErrorResponse(id, RpcErrorCode::InvalidRequest, "Missing or invalid method");
	}

	std::string method = request["method"];
	nlohmann::json params = request.value("params", nlohmann::json::object());

	auto handler_it = this->handlers.find(method);
	if (handler_it == this->handlers.end()) {
		return this->MakeErrorResponse(id, RpcErrorCode::MethodNotFound, "Method not found: " + method);
	}

	try {
		nlohmann::json result = handler_it->second(params);
		return this->MakeSuccessResponse(id, result);
	} catch (const std::exception &e) {
		return this->MakeErrorResponse(id, RpcErrorCode::InternalError, e.what());
	}
}

nlohmann::json RpcServer::MakeErrorResponse(const nlohmann::json &id, RpcErrorCode code, const std::string &message)
{
	return {
		{"jsonrpc", "2.0"},
		{"id", id},
		{"error", {
			{"code", static_cast<int>(code)},
			{"message", message}
		}}
	};
}

nlohmann::json RpcServer::MakeSuccessResponse(const nlohmann::json &id, const nlohmann::json &result)
{
	return {
		{"jsonrpc", "2.0"},
		{"id", id},
		{"result", result}
	};
}

void RpcServer::SendResponse(ClientConnection &client, const nlohmann::json &response)
{
	std::string data = response.dump() + "\n";
	send(client.socket, data.c_str(), data.size(), 0);
}

void RpcServerStart()
{
	if (_rpc_server) return;

	_rpc_server = std::make_unique<RpcServer>();
	RpcRegisterHandlers(*_rpc_server);
	_rpc_server->Start();
}

void RpcServerStop()
{
	_rpc_server.reset();
}

void RpcServerPoll()
{
	if (_rpc_server) {
		_rpc_server->Poll();
	}
}
