/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file rpc_handlers.h Declarations for JSON-RPC method handlers. */

#ifndef RPC_HANDLERS_H
#define RPC_HANDLERS_H

#include "rpc_server.h"

/* Query handlers - rpc_handlers_query.cpp */
void RpcRegisterQueryHandlers(RpcServer &server);

/* Action handlers - rpc_handlers_action.cpp */
void RpcRegisterActionHandlers(RpcServer &server);

/* Infrastructure handlers - rpc_handlers_infra.cpp */
void RpcRegisterInfraHandlers(RpcServer &server);

/* Meta handlers - rpc_handlers_meta.cpp */
void RpcRegisterMetaHandlers(RpcServer &server);

#endif /* RPC_HANDLERS_H */
