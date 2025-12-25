/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file TerminalSession.h Terminal emulator session using libvterm or fallback parser. */

#ifndef TERMINAL_SESSION_H
#define TERMINAL_SESSION_H

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace OpenTTD::Terminal {

/** RGB colour value for terminal cells. */
struct TerminalColourRGB {
	uint8_t r = 0;
	uint8_t g = 0;
	uint8_t b = 0;
};

/** A single cell in the terminal grid. */
struct TerminalCell {
	char32_t codepoint = U' ';           ///< Unicode codepoint to display.
	TerminalColourRGB foregroundRgb{229, 229, 229};  ///< Foreground colour (default light grey).
	TerminalColourRGB backgroundRgb{0, 0, 0};        ///< Background colour (default black).
	bool bold = false;                   ///< Bold attribute.
	bool underline = false;              ///< Underline attribute.
	bool inverse = false;                ///< Inverse (swap fg/bg) attribute.
	bool wide = false;                   ///< This cell is a wide (2-column) character.
	bool continuation = false;           ///< This cell is the trailing part of a wide character.
};

/** Snapshot of the terminal state for rendering. */
struct TerminalSnapshot {
	int rows = 0;                        ///< Number of rows.
	int cols = 0;                        ///< Number of columns.
	std::vector<TerminalCell> cells;     ///< Cell data (row-major order).

	void Clear()
	{
		this->rows = 0;
		this->cols = 0;
		this->cells.clear();
	}
};

/**
 * Terminal emulator session.
 * Wraps libvterm (if available) or a fallback ANSI parser.
 */
class TerminalSession {
public:
	TerminalSession(int cols, int rows);
	~TerminalSession();

	TerminalSession(const TerminalSession &) = delete;
	TerminalSession &operator=(const TerminalSession &) = delete;

	/** Resize the terminal. */
	void Resize(int cols, int rows);

	/** Feed output from the shell process into the terminal. */
	void FeedOutput(std::span<const uint8_t> bytes);

	/**
	 * Get a snapshot of the terminal state if it has changed.
	 * @param outSnapshot Output snapshot.
	 * @return True if the snapshot was updated, false if unchanged.
	 */
	bool ConsumeSnapshot(TerminalSnapshot &outSnapshot);

	/** Force a full refresh on the next ConsumeSnapshot. */
	void ForceFullRefresh();

	/** Get current column count. */
	[[nodiscard]] int GetCols() const;

	/** Get current row count. */
	[[nodiscard]] int GetRows() const;

	/** Get number of scrollback rows available. */
	[[nodiscard]] int GetScrollbackRowCount() const;

	/** Check if alternate screen buffer is active. */
	[[nodiscard]] bool IsAltScreenActive() const;

	/** Copy scrollback rows to output buffer. */
	void CopyScrollbackRows(int startRow, int rowCount, std::vector<TerminalCell> &out) const;

private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};

} // namespace OpenTTD::Terminal

#endif /* TERMINAL_SESSION_H */
