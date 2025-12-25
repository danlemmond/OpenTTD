/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file TerminalSession.cpp Terminal emulator session using libvterm or fallback parser. */

#include "../stdafx.h"
#include "TerminalSession.h"
#include "../core/string_consumer.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <limits>
#include <string>
#include <vector>

#ifdef WITH_VTERM
#include <vterm.h>
#endif

#include "../safeguards.h"

namespace OpenTTD::Terminal {

namespace {

/**
 * Parse an integer from a string safely.
 * @param s String to parse.
 * @return Parsed integer, or -1 if invalid.
 */
[[nodiscard]] static int SafeParseInt(const std::string &s)
{
	if (s.empty()) return -1;
	int value = 0;
	auto result = std::from_chars(s.data(), s.data() + s.size(), value, 10);
	if (result.ec != std::errc{} || result.ptr != s.data() + s.size()) return -1;
	return value;
}

constexpr TerminalColourRGB kDefaultForegroundRgb{229, 229, 229};
constexpr TerminalColourRGB kDefaultBackgroundRgb{0, 0, 0};
constexpr int kMaxScrollbackRows = 2000;

[[nodiscard]] TerminalCell MakeEmptyCell()
{
	TerminalCell cell;
	cell.codepoint = U' ';
	cell.foregroundRgb = kDefaultForegroundRgb;
	cell.backgroundRgb = kDefaultBackgroundRgb;
	return cell;
}

/* ANSI colour table (8 basic + 8 bright). */
constexpr std::array<TerminalColourRGB, 16> kAnsiColours = {{
	{0, 0, 0},         // 0 Black
	{205, 0, 0},       // 1 Red
	{0, 205, 0},       // 2 Green
	{205, 205, 0},     // 3 Yellow
	{0, 0, 238},       // 4 Blue
	{205, 0, 205},     // 5 Magenta
	{0, 205, 205},     // 6 Cyan
	{229, 229, 229},   // 7 White
	{127, 127, 127},   // 8 Bright Black (Grey)
	{255, 0, 0},       // 9 Bright Red
	{0, 255, 0},       // 10 Bright Green
	{255, 255, 0},     // 11 Bright Yellow
	{92, 92, 255},     // 12 Bright Blue
	{255, 92, 255},    // 13 Bright Magenta
	{92, 255, 255},    // 14 Bright Cyan
	{255, 255, 255},   // 15 Bright White
}};

} // namespace

#ifdef WITH_VTERM

struct TerminalSession::Impl {
	VTerm *term = nullptr;
	VTermScreen *screen = nullptr;
	VTermState *state = nullptr;
	bool dirty = true;
	bool altScreenActive = false;
	TerminalSnapshot snapshot;
	int cols = 0;
	int rows = 0;
	std::deque<std::vector<TerminalCell>> scrollback;

	explicit Impl(int initialCols, int initialRows)
	{
		this->cols = initialCols;
		this->rows = initialRows;

		this->term = vterm_new(this->rows, this->cols);
		this->state = vterm_obtain_state(this->term);
		this->screen = vterm_obtain_screen(this->term);

		vterm_set_utf8(this->term, 1);

		/* Set default terminal colors - white on black. */
		VTermColor defaultFg, defaultBg;
		vterm_color_rgb(&defaultFg, 229, 229, 229); /* Light grey/white */
		vterm_color_rgb(&defaultBg, 0, 0, 0);       /* Black */
		vterm_state_set_default_colors(this->state, &defaultFg, &defaultBg);

		vterm_state_reset(this->state, 1);
		vterm_screen_reset(this->screen, 1);
		vterm_screen_enable_altscreen(this->screen, 1);

		static VTermScreenCallbacks callbacks = {
			/* damage */
			[](VTermRect, void *user) {
				auto *impl = static_cast<Impl *>(user);
				if (impl) impl->dirty = true;
				return 1;
			},
			/* moverect */
			[](VTermRect, VTermRect, void *) { return 1; },
			/* movecursor */
			[](VTermPos, VTermPos, int, void *) { return 1; },
			/* settermprop */
			[](VTermProp prop, VTermValue *val, void *user) {
				auto *impl = static_cast<Impl *>(user);
				if (impl == nullptr || val == nullptr) return 0;
				if (prop == VTERM_PROP_ALTSCREEN) {
					impl->altScreenActive = (val->boolean != 0);
					impl->dirty = true;
					return 1;
				}
				return 0;
			},
			/* bell */
			[](void *) { return 1; },
			/* resize */
			[](int newRows, int newCols, void *user) {
				auto *impl = static_cast<Impl *>(user);
				if (impl) {
					impl->rows = newRows;
					impl->cols = newCols;
					impl->dirty = true;
				}
				return 1;
			},
			/* sb_pushline */
			[](int lineCols, const VTermScreenCell *cells, void *user) {
				auto *impl = static_cast<Impl *>(user);
				if (impl == nullptr || lineCols <= 0 || cells == nullptr) return 0;
				impl->PushScrollbackLine(cells, lineCols);
				return 1;
			},
			/* sb_popline */
			[](int, VTermScreenCell *, void *) { return 0; },
			/* sb_clear */
			[](void *) { return 1; },
		};

		vterm_screen_set_callbacks(this->screen, &callbacks, this);
		vterm_screen_set_damage_merge(this->screen, VTERM_DAMAGE_SCROLL);

		this->snapshot.rows = this->rows;
		this->snapshot.cols = this->cols;
		this->snapshot.cells.resize(static_cast<size_t>(this->rows) * this->cols, MakeEmptyCell());
	}

	~Impl()
	{
		if (this->term != nullptr) {
			vterm_free(this->term);
			this->term = nullptr;
		}
	}

	void ForceRefresh() { this->dirty = true; }

	void Feed(std::span<const uint8_t> bytes)
	{
		if (bytes.empty()) return;
		if (this->term == nullptr || this->state == nullptr || this->screen == nullptr) return;
		vterm_input_write(this->term, reinterpret_cast<const char *>(bytes.data()), bytes.size());
		this->dirty = true;
	}

	void Resize(int newCols, int newRows)
	{
		if (newCols == this->cols && newRows == this->rows) return;
		this->cols = std::max(2, newCols);
		this->rows = std::max(2, newRows);
		vterm_set_size(this->term, this->rows, this->cols);
		this->scrollback.clear();
		this->dirty = true;
	}

	[[nodiscard]] TerminalCell ConvertVTermCell(const VTermScreenCell &cell) const
	{
		TerminalCell target = MakeEmptyCell();
		target.codepoint = cell.chars[0] != 0 ? static_cast<char32_t>(cell.chars[0]) : U' ';

		VTermColor fg = cell.fg;
		VTermColor bg = cell.bg;
		vterm_screen_convert_color_to_rgb(this->screen, &fg);
		vterm_screen_convert_color_to_rgb(this->screen, &bg);

		target.foregroundRgb = {
			static_cast<uint8_t>(fg.rgb.red),
			static_cast<uint8_t>(fg.rgb.green),
			static_cast<uint8_t>(fg.rgb.blue)
		};
		target.backgroundRgb = {
			static_cast<uint8_t>(bg.rgb.red),
			static_cast<uint8_t>(bg.rgb.green),
			static_cast<uint8_t>(bg.rgb.blue)
		};

		target.bold = cell.attrs.bold;
		target.underline = cell.attrs.underline != 0;
		target.inverse = cell.attrs.reverse;
		target.wide = cell.width > 1;
		target.continuation = (cell.width == 0);

		if (target.inverse) {
			std::swap(target.foregroundRgb, target.backgroundRgb);
		}
		return target;
	}

	bool Snapshot(TerminalSnapshot &outSnapshot)
	{
		if (!this->dirty) return false;

		this->dirty = false;
		this->snapshot.rows = this->rows;
		this->snapshot.cols = this->cols;
		this->snapshot.cells.resize(static_cast<size_t>(this->rows) * this->cols, MakeEmptyCell());

		VTermPos pos{};
		VTermScreenCell cell{};
		for (int row = 0; row < this->rows; row++) {
			pos.row = row;
			for (int col = 0; col < this->cols; col++) {
				pos.col = col;
				if (!vterm_screen_get_cell(this->screen, pos, &cell)) continue;
				auto &target = this->snapshot.cells[static_cast<size_t>(row) * this->cols + col];
				target = this->ConvertVTermCell(cell);
			}
		}

		vterm_screen_flush_damage(this->screen);
		outSnapshot = this->snapshot;
		return true;
	}

	void PushScrollbackLine(const VTermScreenCell *cells, int numCols)
	{
		if (this->screen == nullptr || numCols <= 0) return;

		std::vector<TerminalCell> row;
		row.reserve(static_cast<size_t>(numCols));
		for (int col = 0; col < numCols; col++) {
			row.push_back(this->ConvertVTermCell(cells[col]));
		}
		this->scrollback.push_back(std::move(row));
		while (this->scrollback.size() > kMaxScrollbackRows) {
			this->scrollback.pop_front();
		}
	}

	int GetScrollbackRows() const { return static_cast<int>(this->scrollback.size()); }

	void CopyScrollbackRows(int startRow, int rowCount, std::vector<TerminalCell> &out) const
	{
		if (rowCount <= 0 || startRow < 0) {
			out.clear();
			return;
		}

		const int lineWidth = this->snapshot.cols;
		out.resize(static_cast<size_t>(rowCount) * lineWidth, MakeEmptyCell());

		for (int i = 0; i < rowCount; i++) {
			int sourceIndex = startRow + i;
			if (sourceIndex < 0 || sourceIndex >= static_cast<int>(this->scrollback.size())) continue;

			const auto &row = this->scrollback[static_cast<size_t>(sourceIndex)];
			auto *dest = &out[static_cast<size_t>(i) * lineWidth];
			size_t copyCount = std::min(row.size(), static_cast<size_t>(lineWidth));
			std::copy_n(row.begin(), copyCount, dest);
			if (copyCount < static_cast<size_t>(lineWidth)) {
				std::fill(dest + copyCount, dest + lineWidth, MakeEmptyCell());
			}
		}
	}

	bool IsAltScreenActive() const { return this->altScreenActive; }
};

#else /* Fallback ANSI parser when libvterm is not available */

struct TerminalSession::Impl {
	TerminalSnapshot snapshot;
	int cols = 0;
	int rows = 0;
	int cursorRow = 0;
	int cursorCol = 0;
	int savedCursorRow = 0;
	int savedCursorCol = 0;
	bool dirty = true;
	TerminalColourRGB currentForegroundRgb = kDefaultForegroundRgb;
	TerminalColourRGB currentBackgroundRgb = kDefaultBackgroundRgb;
	bool currentBold = false;
	bool currentUnderline = false;
	bool currentInverse = false;
	std::deque<std::vector<TerminalCell>> scrollback;

	enum class EscapeState { Text, EscapeIntroducer, CSI, OSC };
	EscapeState escapeState = EscapeState::Text;
	std::string csiBuffer;
	bool csiPrivate = false;
	bool oscEscapePending = false;

	/* UTF-8 decoder state. */
	char32_t utf8Codepoint = 0;
	int utf8Remaining = 0;

	explicit Impl(int initialCols, int initialRows)
	{
		this->Resize(initialCols, initialRows);
	}

	void ForceRefresh() { this->dirty = true; }

	void ResetAttributes()
	{
		this->currentForegroundRgb = kDefaultForegroundRgb;
		this->currentBackgroundRgb = kDefaultBackgroundRgb;
		this->currentBold = false;
		this->currentUnderline = false;
		this->currentInverse = false;
	}

	void Feed(std::span<const uint8_t> bytes)
	{
		for (uint8_t byte : bytes) {
			char32_t ch = 0;

			/* UTF-8 decoding. */
			if (this->utf8Remaining > 0) {
				/* Continuation byte expected. */
				if ((byte & 0xC0) == 0x80) {
					this->utf8Codepoint = (this->utf8Codepoint << 6) | (byte & 0x3F);
					this->utf8Remaining--;
					if (this->utf8Remaining > 0) continue;
					ch = this->utf8Codepoint;
				} else {
					/* Invalid continuation - reset and process as new byte. */
					this->utf8Remaining = 0;
					this->utf8Codepoint = 0;
				}
			}

			if (ch == 0) {
				if ((byte & 0x80) == 0) {
					/* ASCII. */
					ch = byte;
				} else if ((byte & 0xE0) == 0xC0) {
					/* 2-byte sequence. */
					this->utf8Codepoint = byte & 0x1F;
					this->utf8Remaining = 1;
					continue;
				} else if ((byte & 0xF0) == 0xE0) {
					/* 3-byte sequence. */
					this->utf8Codepoint = byte & 0x0F;
					this->utf8Remaining = 2;
					continue;
				} else if ((byte & 0xF8) == 0xF0) {
					/* 4-byte sequence. */
					this->utf8Codepoint = byte & 0x07;
					this->utf8Remaining = 3;
					continue;
				} else {
					/* Invalid UTF-8 lead byte - treat as Latin-1. */
					ch = byte;
				}
			}

			switch (this->escapeState) {
				case EscapeState::Text:
					this->HandleTextChar(ch);
					break;
				case EscapeState::EscapeIntroducer:
					this->HandleEscapeIntroducer(ch);
					break;
				case EscapeState::CSI:
					this->HandleCsiChar(ch);
					break;
				case EscapeState::OSC:
					this->HandleOscChar(ch);
					break;
			}
		}
		this->dirty = true;
	}

	void HandleTextChar(char32_t ch)
	{
		switch (ch) {
			case '\r':
				this->cursorCol = 0;
				break;
			case '\n':
				this->cursorCol = 0;
				this->cursorRow++;
				if (this->cursorRow >= this->rows) {
					this->Scroll();
					this->cursorRow = this->rows - 1;
				}
				break;
			case '\b':
				this->cursorCol = std::max(0, this->cursorCol - 1);
				this->WriteChar(U' ');
				this->cursorCol = std::max(0, this->cursorCol - 1);
				break;
			case '\t': {
				constexpr int tab = 4;
				int spaces = tab - (this->cursorCol % tab);
				for (int i = 0; i < spaces; i++) this->WriteChar(U' ');
				break;
			}
			case 0x1B:
				this->escapeState = EscapeState::EscapeIntroducer;
				break;
			default:
				if (ch >= 0x20) this->WriteChar(ch);
				break;
		}
	}

	void HandleEscapeIntroducer(char32_t ch)
	{
		if (ch == '[') {
			this->escapeState = EscapeState::CSI;
			this->csiBuffer.clear();
			this->csiPrivate = false;
		} else if (ch == ']') {
			this->escapeState = EscapeState::OSC;
			this->oscEscapePending = false;
		} else {
			this->escapeState = EscapeState::Text;
		}
	}

	void HandleCsiChar(char32_t ch)
	{
		if (ch == '?') {
			this->csiPrivate = true;
			return;
		}
		if (ch >= 0x40 && ch <= 0x7E) {
			this->HandleCsiCommand(static_cast<char>(ch));
			this->escapeState = EscapeState::Text;
			this->csiBuffer.clear();
			this->csiPrivate = false;
		} else {
			this->csiBuffer.push_back(static_cast<char>(ch));
		}
	}

	void HandleOscChar(char32_t ch)
	{
		if (ch == 0x07) {
			this->escapeState = EscapeState::Text;
			return;
		}
		if (this->oscEscapePending) {
			if (ch == '\\') {
				this->escapeState = EscapeState::Text;
			} else if (ch == '[') {
				this->escapeState = EscapeState::CSI;
				this->csiBuffer.clear();
				this->csiPrivate = false;
			} else if (ch == ']') {
				this->escapeState = EscapeState::OSC;
			} else {
				this->escapeState = EscapeState::Text;
			}
			this->oscEscapePending = false;
			return;
		}
		if (ch == 0x1B) this->oscEscapePending = true;
	}

	[[nodiscard]] std::vector<int> ParseCsiParams() const
	{
		std::vector<int> params;
		std::string current;
		for (char c : this->csiBuffer) {
			if (c == ';') {
				params.push_back(current.empty() ? -1 : SafeParseInt(current));
				current.clear();
			} else if (c >= '0' && c <= '9') {
				current.push_back(c);
			}
		}
		if (!current.empty() || this->csiBuffer.find(';') != std::string::npos) {
			params.push_back(current.empty() ? -1 : SafeParseInt(current));
		}
		return params;
	}

	void HandleCsiCommand(char finalByte)
	{
		auto params = this->ParseCsiParams();
		auto getParam = [&](size_t idx, int def) {
			return (idx >= params.size() || params[idx] < 0) ? def : params[idx];
		};

		/* Handle private mode sequences (ESC[?...h/l). */
		if (this->csiPrivate) {
			if (finalByte == 'h' || finalByte == 'l') {
				bool enable = (finalByte == 'h');
				for (int mode : params) {
					switch (mode) {
						case 1049: /* Alternate screen buffer. */
						case 47:   /* Alternate screen (older). */
						case 1047: /* Alternate screen. */
							if (enable) {
								/* Switch to alternate screen - clear it. */
								this->savedCursorRow = this->cursorRow;
								this->savedCursorCol = this->cursorCol;
								this->EraseInDisplay(2);
							} else {
								/* Switch back - restore cursor. */
								this->cursorRow = std::clamp(this->savedCursorRow, 0, this->rows - 1);
								this->cursorCol = std::clamp(this->savedCursorCol, 0, this->cols - 1);
							}
							break;
						case 25: /* Show/hide cursor - ignore for now. */
						case 7:  /* Auto-wrap - ignore. */
						case 12: /* Blinking cursor - ignore. */
						default:
							break;
					}
				}
			}
			return;
		}

		switch (finalByte) {
			case 'A': this->cursorRow = std::max(0, this->cursorRow - getParam(0, 1)); break;
			case 'B': this->cursorRow = std::min(this->rows - 1, this->cursorRow + getParam(0, 1)); break;
			case 'C': this->cursorCol = std::min(this->cols - 1, this->cursorCol + getParam(0, 1)); break;
			case 'D': this->cursorCol = std::max(0, this->cursorCol - getParam(0, 1)); break;
			case 'H':
			case 'f':
				this->MoveCursorTo(getParam(0, 1), getParam(1, 1));
				break;
			case 'J': this->EraseInDisplay(getParam(0, 0)); break;
			case 'K': this->EraseInLine(getParam(0, 0)); break;
			case 'm': this->ApplySgr(params); break;
			case 's':
				this->savedCursorRow = this->cursorRow;
				this->savedCursorCol = this->cursorCol;
				break;
			case 'u':
				this->cursorRow = std::clamp(this->savedCursorRow, 0, this->rows - 1);
				this->cursorCol = std::clamp(this->savedCursorCol, 0, this->cols - 1);
				break;
			default: break;
		}
	}

	void ApplySgr(std::vector<int> params)
	{
		if (params.empty()) {
			this->ResetAttributes();
			return;
		}

		for (int rawCode : params) {
			int code = rawCode < 0 ? 0 : rawCode;
			if (code == 0) {
				this->ResetAttributes();
			} else if (code == 1) {
				this->currentBold = true;
			} else if (code == 4) {
				this->currentUnderline = true;
			} else if (code == 7) {
				this->currentInverse = true;
			} else if (code == 22) {
				this->currentBold = false;
			} else if (code == 24) {
				this->currentUnderline = false;
			} else if (code == 27) {
				this->currentInverse = false;
			} else if (code == 39) {
				this->currentForegroundRgb = kDefaultForegroundRgb;
			} else if (code == 49) {
				this->currentBackgroundRgb = kDefaultBackgroundRgb;
			} else if (code >= 30 && code <= 37) {
				this->currentForegroundRgb = kAnsiColours[code - 30];
			} else if (code >= 40 && code <= 47) {
				this->currentBackgroundRgb = kAnsiColours[code - 40];
			} else if (code >= 90 && code <= 97) {
				this->currentForegroundRgb = kAnsiColours[8 + code - 90];
			} else if (code >= 100 && code <= 107) {
				this->currentBackgroundRgb = kAnsiColours[8 + code - 100];
			}
		}
	}

	void MoveCursorTo(int rowParam, int colParam)
	{
		int targetRow = (rowParam <= 0 ? 1 : rowParam) - 1;
		int targetCol = (colParam <= 0 ? 1 : colParam) - 1;
		this->cursorRow = std::clamp(targetRow, 0, this->rows - 1);
		this->cursorCol = std::clamp(targetCol, 0, this->cols - 1);
	}

	void EraseInDisplay(int mode)
	{
		switch (mode) {
			case 0: this->ClearRange(this->cursorRow, this->cursorCol, this->rows - 1, this->cols - 1); break;
			case 1: this->ClearRange(0, 0, this->cursorRow, this->cursorCol); break;
			case 2:
				this->ClearRange(0, 0, this->rows - 1, this->cols - 1);
				this->cursorRow = 0;
				this->cursorCol = 0;
				break;
			default: break;
		}
	}

	void EraseInLine(int mode)
	{
		int startCol = 0, endCol = this->cols - 1;
		switch (mode) {
			case 0: startCol = this->cursorCol; break;
			case 1: endCol = this->cursorCol; break;
			default: break;
		}
		for (int col = startCol; col <= endCol && this->cursorRow < this->rows; col++) {
			this->snapshot.cells[static_cast<size_t>(this->cursorRow) * this->cols + col] = MakeEmptyCell();
		}
	}

	void ClearRange(int startRow, int startCol, int endRow, int endCol)
	{
		startRow = std::clamp(startRow, 0, this->rows - 1);
		endRow = std::clamp(endRow, 0, this->rows - 1);
		startCol = std::clamp(startCol, 0, this->cols - 1);
		endCol = std::clamp(endCol, 0, this->cols - 1);

		for (int row = startRow; row <= endRow; row++) {
			int beginCol = (row == startRow) ? startCol : 0;
			int finishCol = (row == endRow) ? endCol : this->cols - 1;
			for (int col = beginCol; col <= finishCol; col++) {
				this->snapshot.cells[static_cast<size_t>(row) * this->cols + col] = MakeEmptyCell();
			}
		}
	}

	void Scroll()
	{
		if (this->rows <= 1) return;
		this->PushScrollbackRow(0);
		auto begin = this->snapshot.cells.begin();
		auto end = begin + static_cast<int64_t>(this->cols);
		std::rotate(begin, end, this->snapshot.cells.end());
		std::fill(this->snapshot.cells.end() - this->cols, this->snapshot.cells.end(), MakeEmptyCell());
	}

	void WriteChar(char32_t ch)
	{
		if (this->cursorCol >= this->cols) {
			this->cursorCol = 0;
			this->cursorRow++;
		}
		if (this->cursorRow >= this->rows) {
			this->Scroll();
			this->cursorRow = this->rows - 1;
		}

		auto &cell = this->snapshot.cells[static_cast<size_t>(this->cursorRow) * this->cols + this->cursorCol];
		cell = MakeEmptyCell();
		cell.codepoint = ch;
		cell.foregroundRgb = this->currentForegroundRgb;
		cell.backgroundRgb = this->currentBackgroundRgb;
		cell.bold = this->currentBold;
		cell.underline = this->currentUnderline;
		cell.inverse = this->currentInverse;
		if (cell.inverse) {
			std::swap(cell.foregroundRgb, cell.backgroundRgb);
		}
		this->cursorCol++;
	}

	void PushScrollbackRow(int rowIndex)
	{
		if (rowIndex < 0 || rowIndex >= this->rows || this->cols <= 0) return;

		std::vector<TerminalCell> row;
		row.reserve(static_cast<size_t>(this->cols));
		const auto *src = &this->snapshot.cells[static_cast<size_t>(rowIndex) * this->cols];
		row.insert(row.end(), src, src + this->cols);
		this->scrollback.push_back(std::move(row));
		while (this->scrollback.size() > kMaxScrollbackRows) {
			this->scrollback.pop_front();
		}
	}

	void Resize(int newCols, int newRows)
	{
		this->cols = std::max(2, newCols);
		this->rows = std::max(2, newRows);
		this->snapshot.rows = this->rows;
		this->snapshot.cols = this->cols;
		this->snapshot.cells.assign(static_cast<size_t>(this->rows) * this->cols, MakeEmptyCell());
		this->cursorRow = std::min(this->cursorRow, this->rows - 1);
		this->cursorCol = std::min(this->cursorCol, this->cols - 1);
		this->savedCursorRow = std::clamp(this->savedCursorRow, 0, this->rows - 1);
		this->savedCursorCol = std::clamp(this->savedCursorCol, 0, this->cols - 1);
		this->scrollback.clear();
		this->ResetAttributes();
		this->dirty = true;
	}

	int GetScrollbackRows() const { return static_cast<int>(this->scrollback.size()); }

	void CopyScrollbackRows(int startRow, int rowCount, std::vector<TerminalCell> &out) const
	{
		if (rowCount <= 0 || startRow < 0) {
			out.clear();
			return;
		}

		const int lineWidth = this->snapshot.cols;
		if (lineWidth <= 0) {
			out.clear();
			return;
		}

		out.resize(static_cast<size_t>(rowCount) * lineWidth, MakeEmptyCell());
		for (int i = 0; i < rowCount; i++) {
			int sourceIndex = startRow + i;
			if (sourceIndex < 0 || sourceIndex >= static_cast<int>(this->scrollback.size())) continue;

			const auto &row = this->scrollback[static_cast<size_t>(sourceIndex)];
			auto *dest = &out[static_cast<size_t>(i) * lineWidth];
			size_t copyCount = std::min(row.size(), static_cast<size_t>(lineWidth));
			std::copy_n(row.begin(), copyCount, dest);
			if (copyCount < static_cast<size_t>(lineWidth)) {
				std::fill(dest + copyCount, dest + lineWidth, MakeEmptyCell());
			}
		}
	}

	bool Snapshot(TerminalSnapshot &outSnapshot)
	{
		if (!this->dirty) return false;
		this->dirty = false;
		outSnapshot = this->snapshot;
		return true;
	}

	bool IsAltScreenActive() const { return false; }
};

#endif /* WITH_VTERM */

TerminalSession::TerminalSession(int cols, int rows)
	: impl(std::make_unique<Impl>(cols, rows))
{
}

TerminalSession::~TerminalSession() = default;

void TerminalSession::Resize(int cols, int rows)
{
	this->impl->Resize(cols, rows);
}

void TerminalSession::FeedOutput(std::span<const uint8_t> bytes)
{
	if (bytes.empty()) return;
	this->impl->Feed(bytes);
}

bool TerminalSession::ConsumeSnapshot(TerminalSnapshot &outSnapshot)
{
	return this->impl->Snapshot(outSnapshot);
}

void TerminalSession::ForceFullRefresh()
{
	this->impl->ForceRefresh();
}

int TerminalSession::GetCols() const
{
	return this->impl->snapshot.cols;
}

int TerminalSession::GetRows() const
{
	return this->impl->snapshot.rows;
}

int TerminalSession::GetScrollbackRowCount() const
{
	return this->impl->GetScrollbackRows();
}

bool TerminalSession::IsAltScreenActive() const
{
	return this->impl->IsAltScreenActive();
}

void TerminalSession::CopyScrollbackRows(int startRow, int rowCount, std::vector<TerminalCell> &out) const
{
	this->impl->CopyScrollbackRows(startRow, rowCount, out);
}

} // namespace OpenTTD::Terminal
