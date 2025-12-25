/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file AIAgentLaunch.cpp Logic for launching Claude Code AI agent. */

#include "../stdafx.h"
#include "AIAgentLaunch.h"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string_view>
#include <system_error>
#include <vector>

#if defined(__APPLE__) || defined(__linux__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#include <unistd.h>
#endif

#include "../safeguards.h"

namespace OpenTTD::Terminal {

namespace {

constexpr const char *kAgentWorkspaceDir = ".openttd-agent";
constexpr const char *kWorkspaceReadme = "CLAUDE.md";
constexpr const char *kRepoReadmeFilename = "IN_GAME_AGENT.md";
constexpr int kMaxRepoSearchDepth = 8;

#if defined(__APPLE__) || defined(__linux__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)

/**
 * Find an executable in PATH.
 * @param name Executable name.
 * @return Full path if found, nullopt otherwise.
 */
std::optional<std::string> FindExecutable(const std::string &name)
{
	if (name.empty()) return std::nullopt;

	/* If name contains a path separator, check directly. */
	if (name.find('/') != std::string::npos) {
		if (access(name.c_str(), X_OK) == 0) return name;
		return std::nullopt;
	}

	const char *pathEnv = std::getenv("PATH");
	if (pathEnv == nullptr) return std::nullopt;

	std::string_view remaining(pathEnv);
	size_t start = 0;
	while (start <= remaining.size()) {
		const size_t end = remaining.find(':', start);
		auto segment = remaining.substr(start, end == std::string_view::npos ? remaining.size() - start : end - start);
		if (!segment.empty()) {
			std::filesystem::path candidate = std::filesystem::path(segment) / name;
			if (access(candidate.c_str(), X_OK) == 0) {
				return candidate.string();
			}
		}
		if (end == std::string_view::npos) break;
		start = end + 1;
	}

	return std::nullopt;
}

/**
 * Check if the workspace readme is a stub that needs updating.
 */
bool IsStubWorkspaceReadme(const std::filesystem::path &readmePath)
{
	std::ifstream in(readmePath);
	if (!in) return false;
	std::string firstLine;
	if (!std::getline(in, firstLine)) return false;
	return firstLine.find("AI Agent x OpenTTD") != std::string::npos
		|| firstLine.find("Claude Code x OpenTTD") != std::string::npos;
}

struct SeedResult {
	bool success = false;
	std::string error;
};

/**
 * Copy IN_GAME_AGENT.md to the workspace.
 */
SeedResult SeedWorkspaceReadme(const std::filesystem::path &workspace, const std::filesystem::path &repoRoot)
{
	/* Claude Code CLI looks for CLAUDE.md in .claude/ subdirectory. */
	auto agentConfigDir = workspace / ".claude";
	auto readmePath = agentConfigDir / kWorkspaceReadme;

	auto repoReadme = repoRoot / "ai-agent-workspace" / kRepoReadmeFilename;
	if (!std::filesystem::exists(repoReadme)) {
		return {false, "IN_GAME_AGENT.md not found at: " + repoReadme.string()};
	}

	std::error_code dirEc;
	std::filesystem::create_directories(agentConfigDir, dirEc);
	if (dirEc) {
		return {false, "Failed to create agent config directory: " + dirEc.message()};
	}

	/* Copy if missing, stub, or source is newer than deployed. */
	bool readmeExists = std::filesystem::exists(readmePath);
	bool shouldCopy = !readmeExists || IsStubWorkspaceReadme(readmePath);

	if (!shouldCopy && readmeExists) {
		std::error_code ec;
		auto sourceTime = std::filesystem::last_write_time(repoReadme, ec);
		if (!ec) {
			auto deployedTime = std::filesystem::last_write_time(readmePath, ec);
			if (!ec && sourceTime > deployedTime) {
				shouldCopy = true;
			}
		}
	}

	if (shouldCopy) {
		std::error_code copyEc;
		std::filesystem::copy_file(repoReadme, readmePath,
			std::filesystem::copy_options::overwrite_existing, copyEc);
		if (copyEc) {
			return {false, "Failed to copy IN_GAME_AGENT.md: " + copyEc.message()};
		}
	}

	return {true, ""};
}

struct WorkspaceResult {
	std::filesystem::path path;
	bool success = false;
	std::string error;
};

/**
 * Create the agent workspace directory.
 */
WorkspaceResult EnsureWorkspace()
{
	const char *home = std::getenv("HOME");
	if (!home || !*home) {
		return {{}, false, "HOME environment variable not set"};
	}
	auto workspace = std::filesystem::path(home) / kAgentWorkspaceDir;
	std::error_code ec;
	std::filesystem::create_directories(workspace, ec);
	if (ec) {
		return {{}, false, "Failed to create workspace directory: " + ec.message()};
	}
	return {workspace, true, ""};
}

/**
 * Check if a directory looks like the OpenTTD repo root.
 */
bool LooksLikeRepoRoot(const std::filesystem::path &candidate)
{
	return std::filesystem::exists(candidate / "ttdctl" / "CMakeLists.txt")
		&& std::filesystem::exists(candidate / "src" / "rpc");
}

/**
 * Detect the OpenTTD repository root.
 */
std::optional<std::filesystem::path> DetectRepoRoot()
{
	std::vector<std::filesystem::path> seeds;

	try {
		seeds.push_back(std::filesystem::current_path());
	} catch (...) {
		/* Ignore failures; other probes may still succeed. */
	}

	for (const auto &seed : seeds) {
		auto current = seed;
		for (int depth = 0; depth < kMaxRepoSearchDepth && !current.empty(); ++depth) {
			if (LooksLikeRepoRoot(current)) {
				return current;
			}
			auto parent = current.parent_path();
			if (parent == current) break;
			current = std::move(parent);
		}
	}

	return std::nullopt;
}

/**
 * Find the ttdctl binary.
 */
std::optional<std::filesystem::path> FindTtdctlBinary(const std::optional<std::filesystem::path> &repoRoot)
{
	if (!repoRoot) return std::nullopt;

	std::vector<std::filesystem::path> candidates = {
		*repoRoot / "build" / "ttdctl" / "ttdctl",
		*repoRoot / "build" / "bin" / "ttdctl",
	};

	for (const auto &candidate : candidates) {
		if (std::filesystem::is_regular_file(candidate)) {
			return candidate;
		}
	}

	return std::nullopt;
}

/**
 * Create a symlink to the tool in the workspace bin directory.
 */
void PublishWorkspaceTool(const std::filesystem::path &workspace, const std::filesystem::path &toolPath, std::string_view alias)
{
	if (!std::filesystem::is_regular_file(toolPath)) return;

	auto binDir = workspace / "bin";
	std::error_code ec;
	std::filesystem::create_directories(binDir, ec);

	auto linkPath = binDir / alias;
	if (std::filesystem::exists(linkPath)) {
		std::error_code removeEc;
		std::filesystem::remove(linkPath, removeEc);
	}

	std::error_code linkEc;
	std::filesystem::create_symlink(toolPath, linkPath, linkEc);
	if (linkEc) {
		/* Fall back to copying if symlink fails. */
		std::error_code copyEc;
		std::filesystem::copy_file(toolPath, linkPath,
			std::filesystem::copy_options::overwrite_existing, copyEc);
	}
}

/**
 * Add default environment variables for the shell.
 */
void AddDefaultEnvironment(ShellLaunchOptions &options, const std::filesystem::path &workspace,
	const std::optional<std::filesystem::path> &repoRoot)
{
	options.environment.emplace_back("TERM=xterm-256color");
	options.environment.emplace_back("LC_ALL=en_US.UTF-8");
	options.environment.emplace_back("LANG=en_US.UTF-8");
	options.environment.emplace_back("TTD_AGENT_MODE=ai-agent");

	/* Use a clean temp directory. */
	auto cleanTmpDir = workspace / ".tmp";
	std::error_code tmpEc;
	std::filesystem::create_directories(cleanTmpDir, tmpEc);
	if (!tmpEc) {
		options.environment.emplace_back("TMPDIR=" + cleanTmpDir.string());
	}

	std::vector<std::filesystem::path> candidatePaths;
	candidatePaths.emplace_back(workspace / "bin");
	candidatePaths.emplace_back(workspace);

	if (repoRoot) {
		options.environment.emplace_back("OPENTTD_REPO_ROOT=" + repoRoot->string());
		candidatePaths.emplace_back(*repoRoot / "build" / "ttdctl");
		candidatePaths.emplace_back(*repoRoot / "build" / "bin");
		candidatePaths.emplace_back(*repoRoot / "build");
	}

	std::string existingPath;
	if (const char *envPath = std::getenv("PATH")) {
		existingPath = envPath;
	}

	std::vector<std::string> segments;
	for (const auto &path : candidatePaths) {
		if (std::filesystem::exists(path)) {
			segments.push_back(path.string());
		}
	}
	if (!existingPath.empty()) {
		segments.push_back(existingPath);
	}

	if (!segments.empty()) {
		std::ostringstream buffer;
		bool first = true;
		for (const auto &seg : segments) {
			if (!seg.empty()) {
				if (!first) buffer << ":";
				first = false;
				buffer << seg;
			}
		}
		if (!first) {
			options.environment.emplace_back("PATH=" + buffer.str());
		}
	}
}

#endif /* POSIX */

} // namespace

AIAgentLaunchPlan BuildAIAgentLaunchPlan(int cols, int rows)
{
	AIAgentLaunchPlan plan;
	plan.options.cols = cols;
	plan.options.rows = rows;

#if defined(__APPLE__) || defined(__linux__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
	/* Step 1: Detect repo root - REQUIRED for proper agent setup. */
	auto repoRoot = DetectRepoRoot();
	if (!repoRoot) {
		plan.error = "Could not locate OpenTTD repository. The AI Agent terminal requires running from a development build within the repo directory.";
		plan.available = false;
		return plan;
	}

	/* Step 2: Create workspace directory. */
	auto workspaceResult = EnsureWorkspace();
	if (!workspaceResult.success) {
		plan.error = workspaceResult.error;
		plan.available = false;
		return plan;
	}
	auto workspace = workspaceResult.path;

	/* Step 3: Seed IN_GAME_AGENT.md - REQUIRED for proper agent instructions. */
	auto seedResult = SeedWorkspaceReadme(workspace, *repoRoot);
	if (!seedResult.success) {
		plan.error = seedResult.error;
		plan.available = false;
		return plan;
	}

	/* Step 4: Find ttdctl binary - REQUIRED for agent to interact with game. */
	auto ttdctlPath = FindTtdctlBinary(repoRoot);
	if (!ttdctlPath) {
		plan.error = "ttdctl binary not found. Build the project first with: ninja -C build";
		plan.available = false;
		return plan;
	}

	/* Setup workspace environment. */
	plan.options.workingDirectory = workspace.string();
	std::error_code workspaceEc;
	std::filesystem::create_directories(workspace / "bin", workspaceEc);
	AddDefaultEnvironment(plan.options, workspace, repoRoot);
	plan.options.environment.emplace_back("AGENT_WORKSPACE=" + plan.options.workingDirectory);

	PublishWorkspaceTool(workspace, *ttdctlPath, "ttdctl");

	/* Check for custom command override. */
	if (const char *customCommand = std::getenv("AGENT_TERMINAL_COMMAND")) {
		plan.options.command = {"/bin/sh", "-lc", customCommand};
		plan.description = customCommand;
		plan.usesAgent = true;
		plan.available = true;
		return plan;
	}

	/* Check for Claude Code CLI. */
	if (auto agentBin = FindExecutable("claude")) {
		constexpr const char *kClaudeSettings = R"({"spinnerTipsEnabled":false})";
		plan.options.command = {
			agentBin.value(), "--dangerously-skip-permissions", "--settings", kClaudeSettings
		};
		plan.description = agentBin.value();
		plan.usesAgent = true;
		plan.available = true;
		return plan;
	}

	plan.error = "Claude Code CLI not found. Install it with: npm install -g @anthropic-ai/claude-code";
	plan.available = false;
	return plan;
#else
	plan.error = "Agent terminal is only supported on macOS and Linux right now.";
	return plan;
#endif
}

} // namespace OpenTTD::Terminal
