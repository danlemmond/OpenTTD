/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file ShellProcess.cpp PTY abstraction for spawning shell processes. */

#include "../stdafx.h"
#include "ShellProcess.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string_view>
#include <thread>
#include <chrono>

#if defined(__APPLE__)
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>
#include <util.h>
#include <fcntl.h>
#include <sys/wait.h>
#elif defined(__linux__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#include <pty.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#endif

#include "../safeguards.h"

namespace OpenTTD::Terminal {

namespace {

#if defined(__APPLE__) || defined(__linux__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)

/**
 * POSIX implementation of ShellProcess using forkpty.
 */
class PosixShellProcess final : public ShellProcess {
public:
	PosixShellProcess(int masterFd, pid_t pid, std::string description)
		: master_fd(masterFd), pid(pid), description(std::move(description))
	{
		int flags = fcntl(this->master_fd, F_GETFL, 0);
		if (flags != -1) {
			fcntl(this->master_fd, F_SETFL, flags | O_NONBLOCK);
		}
	}

	~PosixShellProcess() override
	{
		if (this->pid > 0) {
			this->KillChild();
		}
		if (this->master_fd >= 0) {
			close(this->master_fd);
		}
	}

	[[nodiscard]] bool IsRunning() const override
	{
		return !this->exited;
	}

	ssize_t Read(uint8_t *buffer, size_t length) override
	{
		if (this->master_fd < 0) return -1;

		ssize_t result = ::read(this->master_fd, buffer, length);
		if (result == 0) {
			this->CheckChild();
		} else if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
			return 0;
		} else if (result < 0) {
			this->CheckChild();
		}
		return result;
	}

	bool Write(std::span<const uint8_t> data) override
	{
		if (this->master_fd < 0 || data.empty()) return false;

		const uint8_t *ptr = data.data();
		size_t remaining = data.size();
		while (remaining > 0) {
			ssize_t written = ::write(this->master_fd, ptr, remaining);
			if (written < 0) {
				if (errno == EAGAIN || errno == EWOULDBLOCK) {
					continue;
				}
				return false;
			}
			ptr += written;
			remaining -= static_cast<size_t>(written);
		}
		return true;
	}

	void Resize(int cols, int rows) override
	{
		if (this->master_fd < 0) return;
		struct winsize ws{};
		ws.ws_col = static_cast<unsigned short>(std::clamp(cols, 2, 500));
		ws.ws_row = static_cast<unsigned short>(std::clamp(rows, 2, 500));
		ioctl(this->master_fd, TIOCSWINSZ, &ws);
	}

	[[nodiscard]] int ExitStatus() const override
	{
		return this->exit_status;
	}

	[[nodiscard]] std::string_view CommandDescription() const override
	{
		return this->description;
	}

private:
	int master_fd;
	pid_t pid;
	int exit_status = 0;
	bool exited = false;
	std::string description;

	void KillChild()
	{
		if (this->pid <= 0) return;

		if (!this->exited) {
			/* Kill entire process group (negative PID) to terminate all descendants.
			 * This handles sub-agents, background processes, and any other children
			 * spawned by Claude Code. */
			kill(-this->pid, SIGHUP);
			kill(-this->pid, SIGTERM);

			/* Wait up to 500ms for graceful termination. */
			for (int i = 0; i < 10; ++i) {
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
				int status = 0;
				pid_t result = waitpid(this->pid, &status, WNOHANG);
				if (result == this->pid) {
					this->exited = true;
					this->exit_status = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
					this->pid = -1;
					return;
				}
			}

			/* Force kill if still running after grace period. */
			kill(-this->pid, SIGKILL);
		}

		/* Final cleanup - reap zombie or detach waiter thread. */
		int status = 0;
		pid_t result = waitpid(this->pid, &status, WNOHANG);
		if (result == this->pid) {
			this->exited = true;
			this->exit_status = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
		} else if (result == 0) {
			/* Process still running (shouldn't happen after SIGKILL), detach waiter. */
			pid_t pidToWait = this->pid;
			std::thread([pidToWait]() {
				int waitStatus = 0;
				waitpid(pidToWait, &waitStatus, 0);
			}).detach();
		}

		this->pid = -1;
	}

	void CheckChild()
	{
		if (this->exited || this->pid <= 0) return;

		int status = 0;
		pid_t result = waitpid(this->pid, &status, WNOHANG);
		if (result == this->pid) {
			this->exited = true;
			this->exit_status = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
			this->pid = -1;
		}
	}
};

static std::string JoinCommand(const std::vector<std::string> &command)
{
	std::ostringstream ss;
	for (size_t i = 0; i < command.size(); i++) {
		if (i != 0) ss << ' ';
		ss << command[i];
	}
	return ss.str();
}

static std::unique_ptr<ShellProcess> LaunchPosixProcess(const ShellLaunchOptions &options, std::string &errorOut)
{
	if (options.command.empty()) {
		errorOut = "No command specified for terminal session.";
		return nullptr;
	}

	int masterFd = -1;
	struct winsize ws{};
	ws.ws_col = static_cast<unsigned short>(std::clamp(options.cols, 2, 500));
	ws.ws_row = static_cast<unsigned short>(std::clamp(options.rows, 2, 500));

	pid_t pid = forkpty(&masterFd, nullptr, nullptr, &ws);
	if (pid < 0) {
		errorOut = std::string("forkpty failed: ") + strerror(errno);
		return nullptr;
	}

	if (pid == 0) {
		/* Child process. */

		/* Create new session and process group so we can kill entire tree on cleanup. */
		setsid();

		if (!options.workingDirectory.empty()) {
			if (chdir(options.workingDirectory.c_str()) != 0) {
				_exit(126);
			}
		}

		/* Set environment variables using setenv instead of putenv.
		 * setenv makes its own copy, avoiding the strdup issue. */
		for (const auto &env : options.environment) {
			size_t eq_pos = env.find('=');
			if (eq_pos != std::string::npos) {
				std::string key = env.substr(0, eq_pos);
				std::string value = env.substr(eq_pos + 1);
				setenv(key.c_str(), value.c_str(), 1);
			}
		}

		std::vector<char *> argv;
		argv.reserve(options.command.size() + 1);
		for (const auto &arg : options.command) {
			argv.push_back(const_cast<char *>(arg.c_str()));
		}
		argv.push_back(nullptr);

		execvp(argv[0], argv.data());
		_exit(127);
	}

	/* Prevent master FD from being inherited by any future child processes. */
	int fdflags = fcntl(masterFd, F_GETFD, 0);
	if (fdflags != -1) {
		fcntl(masterFd, F_SETFD, fdflags | FD_CLOEXEC);
	}

	return std::make_unique<PosixShellProcess>(masterFd, pid, JoinCommand(options.command));
}

#endif /* POSIX */

} // namespace

bool ShellProcess::Write(std::string_view text)
{
	return this->Write(std::span<const uint8_t>(reinterpret_cast<const uint8_t *>(text.data()), text.size()));
}

std::unique_ptr<ShellProcess> LaunchShellProcess(const ShellLaunchOptions &options, std::string &errorOut)
{
#if defined(__APPLE__) || defined(__linux__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
	return LaunchPosixProcess(options, errorOut);
#else
	errorOut = "Agent terminal is only supported on POSIX builds right now.";
	return nullptr;
#endif
}

} // namespace OpenTTD::Terminal
