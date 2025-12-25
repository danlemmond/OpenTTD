/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file rpc_client.cpp JSON-RPC client implementation for ttdctl. */

#include "rpc_client.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

#include <stdexcept>
#include <cstring>

RpcClient::RpcClient(const std::string &host, uint16_t port)
	: host(host), port(port)
{
}

RpcClient::~RpcClient() = default;

std::string RpcClient::SendRequest(const std::string &request)
{
	int sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		throw std::runtime_error("Failed to create socket");
	}

	struct hostent *server = gethostbyname(this->host.c_str());
	if (server == nullptr) {
		close(sock);
		throw std::runtime_error("Failed to resolve host: " + this->host);
	}

	struct sockaddr_in addr{};
	addr.sin_family = AF_INET;
	std::memcpy(&addr.sin_addr.s_addr, server->h_addr, server->h_length);
	addr.sin_port = htons(this->port);

	if (connect(sock, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) {
		close(sock);
		throw std::runtime_error("Failed to connect to " + this->host + ":" + std::to_string(this->port));
	}

	int flag = 1;
	setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

	std::string msg = request + "\n";
	if (send(sock, msg.c_str(), msg.size(), 0) < 0) {
		close(sock);
		throw std::runtime_error("Failed to send request");
	}

	std::string response;
	char buffer[4096];
	ssize_t received;

	while ((received = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
		buffer[received] = '\0';
		response += buffer;
		if (response.find('\n') != std::string::npos) break;
	}

	close(sock);

	if (response.empty()) {
		throw std::runtime_error("Empty response from server");
	}

	return response;
}

nlohmann::json RpcClient::Call(const std::string &method, const nlohmann::json &params)
{
	nlohmann::json request = {
		{"jsonrpc", "2.0"},
		{"id", this->next_id++},
		{"method", method},
		{"params", params}
	};

	std::string response_str = this->SendRequest(request.dump());

	nlohmann::json response;
	try {
		response = nlohmann::json::parse(response_str);
	} catch (const nlohmann::json::parse_error &e) {
		throw std::runtime_error("Invalid JSON response: " + std::string(e.what()));
	}

	if (response.contains("error")) {
		auto &error = response["error"];
		std::string msg = "RPC error";
		if (error.contains("message")) {
			msg = error["message"].get<std::string>();
		}
		if (error.contains("code")) {
			msg += " (code: " + std::to_string(error["code"].get<int>()) + ")";
		}
		throw std::runtime_error(msg);
	}

	if (!response.contains("result")) {
		throw std::runtime_error("Response missing 'result' field");
	}

	return response["result"];
}
