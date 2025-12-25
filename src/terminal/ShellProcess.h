/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file ShellProcess.h PTY abstraction for spawning shell processes. */

#ifndef SHELL_PROCESS_H
#define SHELL_PROCESS_H

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace OpenTTD::Terminal {

/** Options for launching a shell process. */
struct ShellLaunchOptions {
	std::vector<std::string> command;        ///< Command and arguments to execute.
	std::vector<std::string> environment;    ///< Environment variables (KEY=VALUE format).
	std::string workingDirectory;            ///< Working directory for the process.
	int cols = 80;                           ///< Terminal width in columns.
	int rows = 24;                           ///< Terminal height in rows.
};

/**
 * Abstract interface for a shell process connected via PTY.
 */
class ShellProcess {
public:
	virtual ~ShellProcess() = default;

	/** Check if the process is still running. */
	[[nodiscard]] virtual bool IsRunning() const = 0;

	/** Read data from the process. Returns bytes read, 0 if no data, -1 on error. */
	virtual ssize_t Read(uint8_t *buffer, size_t length) = 0;

	/** Write binary data to the process. */
	virtual bool Write(std::span<const uint8_t> data) = 0;

	/** Write text to the process. */
	bool Write(std::string_view text);

	/** Resize the terminal. */
	virtual void Resize(int cols, int rows) = 0;

	/** Get the exit status of the process. */
	[[nodiscard]] virtual int ExitStatus() const = 0;

	/** Get a description of the command being run. */
	[[nodiscard]] virtual std::string_view CommandDescription() const = 0;
};

/**
 * Launch a shell process with the given options.
 * @param options Launch configuration.
 * @param errorOut Set to error message on failure.
 * @return The shell process, or nullptr on failure.
 */
std::unique_ptr<ShellProcess> LaunchShellProcess(const ShellLaunchOptions &options, std::string &errorOut);

} // namespace OpenTTD::Terminal

#endif /* SHELL_PROCESS_H */
