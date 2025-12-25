/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file AIAgentLaunch.h Logic for launching Claude Code AI agent. */

#ifndef AI_AGENT_LAUNCH_H
#define AI_AGENT_LAUNCH_H

#include "ShellProcess.h"
#include <string>

namespace OpenTTD::Terminal {

/** Result of planning an AI agent launch. */
struct AIAgentLaunchPlan {
	ShellLaunchOptions options;          ///< Launch options for the shell process.
	std::string description;             ///< Human-readable description of what will be launched.
	std::string error;                   ///< Error message if not available.
	bool usesAgent = false;              ///< True if launching an AI agent (vs fallback shell).
	bool available = false;              ///< True if launch is possible.
};

/**
 * Build a launch plan for the AI agent.
 * Checks for Claude Code CLI, ttdctl binary, and workspace setup.
 * @param cols Terminal width.
 * @param rows Terminal height.
 * @return The launch plan.
 */
AIAgentLaunchPlan BuildAIAgentLaunchPlan(int cols, int rows);

} // namespace OpenTTD::Terminal

#endif /* AI_AGENT_LAUNCH_H */
