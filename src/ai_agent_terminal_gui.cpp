/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file ai_agent_terminal_gui.cpp GUI for the AI Agent terminal window. */

#include "stdafx.h"
#include "debug.h"
#include "window_gui.h"
#include "window_func.h"
#include "gfx_func.h"
#include "strings_func.h"
#include "string_func.h"
#include "video/video_driver.hpp"
#include "timer/timer.h"
#include "timer/timer_window.h"
#include "palette_func.h"
#include "fontcache.h"

#include "terminal/AIAgentLaunch.h"
#include "terminal/ShellProcess.h"
#include "terminal/TerminalSession.h"

#include "widgets/ai_agent_terminal_widget.h"

#include "table/strings.h"

#include "safeguards.h"

/** Get clipboard contents (platform-specific implementation). */
std::optional<std::string> GetClipboardContents();

using namespace OpenTTD::Terminal;

/** Default terminal size in characters. */
static constexpr int TERM_COLS = 100;
static constexpr int TERM_ROWS = 30;

/** PTY read buffer size. */
static constexpr size_t PTY_BUFFER_SIZE = 8192;

/** Character cell width (estimate for initial sizing). */
static constexpr int CELL_WIDTH = 7;

/** Terminal window padding. */
static constexpr int TERMINAL_PADDING = 4;

/**
 * Encode a Unicode codepoint as UTF-8 and append to string.
 */
static void AppendUtf8(std::string &str, char32_t cp)
{
	if (cp < 0x80) {
		str += static_cast<char>(cp);
	} else if (cp < 0x800) {
		str += static_cast<char>(0xC0 | (cp >> 6));
		str += static_cast<char>(0x80 | (cp & 0x3F));
	} else if (cp < 0x10000) {
		str += static_cast<char>(0xE0 | (cp >> 12));
		str += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
		str += static_cast<char>(0x80 | (cp & 0x3F));
	} else {
		str += static_cast<char>(0xF0 | (cp >> 18));
		str += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
		str += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
		str += static_cast<char>(0x80 | (cp & 0x3F));
	}
}

static constexpr std::initializer_list<NWidgetPart> _nested_ai_agent_terminal_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, WID_AAT_CAPTION), SetTextStyle(TC_WHITE),
		NWidget(WWT_SHADEBOX, COLOUR_GREY),
		NWidget(WWT_DEFSIZEBOX, COLOUR_GREY),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_EMPTY, INVALID_COLOUR, WID_AAT_BACKGROUND), SetResize(1, 1), SetFill(1, 1),
			SetMinimalSize(400, 200), SetScrollbar(WID_AAT_SCROLLBAR),
		NWidget(NWID_VSCROLLBAR, COLOUR_GREY, WID_AAT_SCROLLBAR),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, COLOUR_GREY), SetFill(1, 0), SetResize(1, 0), EndContainer(),
		NWidget(WWT_RESIZEBOX, COLOUR_GREY),
	EndContainer(),
};

static WindowDesc _ai_agent_terminal_desc(
	WDP_CENTER, "ai_agent_terminal", 0, 0,
	WC_AI_AGENT_TERMINAL, WC_NONE,
	{},
	_nested_ai_agent_terminal_widgets
);

struct AIAgentTerminalWindow : Window
{
	std::unique_ptr<TerminalSession> terminal_session;
	std::unique_ptr<ShellProcess> shell_process;
	TerminalSnapshot snapshot;
	std::vector<TerminalCell> scrollback_buffer;
	Scrollbar *vscroll = nullptr;
	bool has_snapshot = false;
	bool launch_attempted = false;
	bool process_exited = false;
	int exit_status = 0;
	std::string error_message;
	int scroll_offset = 0;  ///< How many rows scrolled back from current view.

	AIAgentTerminalWindow() : Window(_ai_agent_terminal_desc)
	{
		this->CreateNestedTree();
		this->vscroll = this->GetScrollbar(WID_AAT_SCROLLBAR);
		this->FinishInitNested(0);

		/* Set initial window size based on terminal dimensions and mono font height. */
		int cell_height = GetCharacterHeight(FS_MONO);
		int width = TERM_COLS * CELL_WIDTH + TERMINAL_PADDING * 2 + 12; /* +12 for scrollbar */
		int height = TERM_ROWS * cell_height + TERMINAL_PADDING * 2 + 14; /* +14 for caption */
		ResizeWindow(this, width - this->width, height - this->height);
	}

	void OnInit() override
	{
		if (!this->launch_attempted) {
			this->launch_attempted = true;
			this->LaunchAgent();
		}
	}

	void LaunchAgent()
	{
		AIAgentLaunchPlan plan = BuildAIAgentLaunchPlan(TERM_COLS, TERM_ROWS);

		if (!plan.available) {
			this->error_message = plan.error;
			return;
		}

		std::string error;
		this->shell_process = LaunchShellProcess(plan.options, error);
		if (!this->shell_process) {
			this->error_message = "Failed to launch: " + error;
			return;
		}

		this->terminal_session = std::make_unique<TerminalSession>(TERM_COLS, TERM_ROWS);
	}

	void Close([[maybe_unused]] int data = 0) override
	{
		this->shell_process.reset();
		this->terminal_session.reset();
		VideoDriver::GetInstance()->EditBoxLostFocus();
		this->Window::Close();
	}

	std::string GetWidgetString(WidgetID widget, StringID stringid) const override
	{
		if (widget == WID_AAT_CAPTION) {
			if (!this->error_message.empty()) {
				return "AI Agent - " + this->error_message;
			} else if (this->process_exited) {
				return fmt::format("AI Agent - Exited ({})", this->exit_status);
			}
			return "AI Agent Terminal";
		}
		return this->Window::GetWidgetString(widget, stringid);
	}

	void OnPaint() override
	{
		this->DrawWidgets();
	}

	/**
	 * Check if a codepoint is safe to render.
	 * Filters out problematic Unicode that might cause font rendering issues.
	 */
	static bool IsSafeCodepoint(char32_t cp)
	{
		/* Basic printable ASCII - always safe */
		if (cp >= 0x20 && cp <= 0x7E) return true;

		/* Latin-1 Supplement (common accented chars) */
		if (cp >= 0xA0 && cp <= 0xFF) return true;

		/* Latin Extended-A and B */
		if (cp >= 0x100 && cp <= 0x24F) return true;

		/* Box drawing characters (used by terminal UIs) */
		if (cp >= 0x2500 && cp <= 0x257F) return true;

		/* Block elements */
		if (cp >= 0x2580 && cp <= 0x259F) return true;

		/* Common punctuation and symbols */
		if (cp >= 0x2000 && cp <= 0x206F) return true;

		/* Arrows */
		if (cp >= 0x2190 && cp <= 0x21FF) return true;

		/* Mathematical operators */
		if (cp >= 0x2200 && cp <= 0x22FF) return true;

		/* Misc symbols */
		if (cp >= 0x2600 && cp <= 0x26FF) return true;

		/* Dingbats */
		if (cp >= 0x2700 && cp <= 0x27BF) return true;

		/* Braille patterns (sometimes used for graphics) */
		if (cp >= 0x2800 && cp <= 0x28FF) return true;

		/* Powerline/Nerd Font private use area - skip these as they may not render */
		if (cp >= 0xE000 && cp <= 0xF8FF) return true;

		/* Skip other characters that might cause issues */
		return false;
	}

	/** Helper to draw a single terminal row. */
	void DrawTerminalRow(const TerminalCell *cells, int cols, int x, int y, int right) const
	{
		std::string line;
		for (int col = 0; col < cols; col++) {
			const TerminalCell &cell = cells[col];
			if (cell.continuation) continue;

			char32_t cp = cell.codepoint;
			if (cp >= U' ' && IsSafeCodepoint(cp)) {
				AppendUtf8(line, cp);
			} else if (cp >= U' ') {
				/* Unknown character - use replacement */
				line += '?';
			} else {
				line += ' ';
			}
		}

		/* Trim trailing spaces. */
		while (!line.empty() && line.back() == ' ') line.pop_back();

		if (!line.empty()) {
			DrawString(x, right, y, line, TC_WHITE, SA_LEFT | SA_FORCE, false, FS_MONO);
		}
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget != WID_AAT_BACKGROUND) return;

		/* Draw black background. */
		GfxFillRect(r.left, r.top, r.right, r.bottom, PC_BLACK);

		if (!this->has_snapshot) {
			/* Draw status message if no terminal content yet. */
			int y = r.top + TERMINAL_PADDING;
			if (this->shell_process && this->shell_process->IsRunning()) {
				DrawString(r.left + TERMINAL_PADDING, r.right - TERMINAL_PADDING, y, "Waiting for terminal...", TC_GREY, SA_LEFT, false, FS_MONO);
			} else if (!this->shell_process) {
				DrawString(r.left + TERMINAL_PADDING, r.right - TERMINAL_PADDING, y, this->error_message.empty() ? "No shell process." : this->error_message, TC_RED, SA_LEFT, false, FS_MONO);
			}
			return;
		}

		const int offset_x = r.left + TERMINAL_PADDING;
		const int offset_y = r.top + TERMINAL_PADDING;
		const int cell_height = GetCharacterHeight(FS_MONO);
		const int visible_rows = this->snapshot.rows;
		const int cols = this->snapshot.cols;

		/* Get scroll position. */
		int scroll_pos = this->vscroll ? this->vscroll->GetPosition() : 0;
		int scrollback_rows = this->terminal_session ? this->terminal_session->GetScrollbackRowCount() : 0;

		/* Draw each visible row. */
		for (int display_row = 0; display_row < visible_rows; display_row++) {
			int y = offset_y + display_row * cell_height;
			if (y >= r.bottom) break;

			int data_row = scroll_pos + display_row;

			if (data_row < scrollback_rows) {
				/* This row is in scrollback. */
				std::vector<TerminalCell> scrollback_row;
				const_cast<TerminalSession*>(this->terminal_session.get())->CopyScrollbackRows(data_row, 1, scrollback_row);
				if (!scrollback_row.empty()) {
					this->DrawTerminalRow(scrollback_row.data(), std::min(cols, static_cast<int>(scrollback_row.size())), offset_x, y, r.right - TERMINAL_PADDING);
				}
			} else {
				/* This row is in the current terminal view. */
				int term_row = data_row - scrollback_rows;
				if (term_row >= 0 && term_row < visible_rows) {
					const TerminalCell *cells = &this->snapshot.cells[term_row * cols];
					this->DrawTerminalRow(cells, cols, offset_x, y, r.right - TERMINAL_PADDING);
				}
			}
		}
	}

	/** Update the scrollbar based on terminal state. */
	void UpdateScrollbar()
	{
		if (!this->terminal_session || !this->vscroll) return;

		int scrollback_rows = this->terminal_session->GetScrollbackRowCount();
		int visible_rows = this->snapshot.rows;
		int total_rows = scrollback_rows + visible_rows;

		this->vscroll->SetCount(total_rows);
		this->vscroll->SetCapacity(visible_rows);

		/* If user hasn't scrolled, stay at bottom. */
		if (this->scroll_offset == 0) {
			this->vscroll->SetPosition(scrollback_rows);
		}
	}

	/** Poll PTY for data periodically. */
	IntervalTimer<TimerWindow> pty_poll = {std::chrono::milliseconds(16), [this](auto) {
		if (!this->shell_process) return;

		if (!this->shell_process->IsRunning() && !this->process_exited) {
			/* Process exited. */
			this->process_exited = true;
			this->exit_status = this->shell_process->ExitStatus();
			this->SetDirty();
			return;
		}

		/* Read from PTY. */
		std::array<uint8_t, PTY_BUFFER_SIZE> buffer;
		ssize_t bytes = this->shell_process->Read(buffer.data(), buffer.size());
		if (bytes > 0 && this->terminal_session) {
			this->terminal_session->FeedOutput(std::span<const uint8_t>(buffer.data(), static_cast<size_t>(bytes)));

			/* Update snapshot. */
			if (this->terminal_session->ConsumeSnapshot(this->snapshot)) {
				this->has_snapshot = true;
				this->UpdateScrollbar();
				this->SetDirty();
			}
		}
	}};

	EventState OnKeyPress([[maybe_unused]] char32_t key, uint16_t keycode) override
	{
		if (_focused_window != this || !this->shell_process || !this->shell_process->IsRunning()) {
			return ES_NOT_HANDLED;
		}

		std::string seq;

		/* Handle special keys. */
		switch (keycode) {
			case WKC_RETURN:
			case WKC_NUM_ENTER:
				seq = "\r";
				break;
			case WKC_BACKSPACE:
				seq = "\x7F";
				break;
			case WKC_TAB:
				seq = "\t";
				break;
			case WKC_ESC:
				seq = "\x1B";
				break;
			case WKC_UP:
				seq = "\x1B[A";
				break;
			case WKC_DOWN:
				seq = "\x1B[B";
				break;
			case WKC_RIGHT:
				seq = "\x1B[C";
				break;
			case WKC_LEFT:
				seq = "\x1B[D";
				break;
			case WKC_HOME:
				seq = "\x1B[H";
				break;
			case WKC_END:
				seq = "\x1B[F";
				break;
			case WKC_DELETE:
				seq = "\x1B[3~";
				break;
			case WKC_PAGEUP:
				seq = "\x1B[5~";
				break;
			case WKC_PAGEDOWN:
				seq = "\x1B[6~";
				break;
			case WKC_CTRL | 'C':
				seq = "\x03";
				break;
			case WKC_CTRL | 'D':
				seq = "\x04";
				break;
			case WKC_CTRL | 'Z':
				seq = "\x1A";
				break;
			case WKC_CTRL | 'L':
				seq = "\x0C";
				break;
			case WKC_CTRL | 'V':
			case WKC_META | 'V': {
				/* Paste from clipboard. */
				auto clipboard = GetClipboardContents();
				if (clipboard.has_value() && !clipboard->empty()) {
					this->shell_process->Write(*clipboard);
				}
				return ES_HANDLED;
			}
			default:
				/* Handle printable characters. */
				if (key >= 0x20 && key < 0x7F) {
					seq = static_cast<char>(key);
				} else if (key >= 0x80) {
					/* UTF-8 encode. */
					if (key < 0x800) {
						seq.push_back(static_cast<char>(0xC0 | (key >> 6)));
						seq.push_back(static_cast<char>(0x80 | (key & 0x3F)));
					} else if (key < 0x10000) {
						seq.push_back(static_cast<char>(0xE0 | (key >> 12)));
						seq.push_back(static_cast<char>(0x80 | ((key >> 6) & 0x3F)));
						seq.push_back(static_cast<char>(0x80 | (key & 0x3F)));
					}
				}
				break;
		}

		if (!seq.empty()) {
			this->shell_process->Write(seq);
			return ES_HANDLED;
		}

		return ES_NOT_HANDLED;
	}

	void OnResize() override
	{
		this->UpdateScrollbar();
		this->SetDirty();
	}

	void OnFocus() override
	{
		VideoDriver::GetInstance()->EditBoxGainedFocus();
	}

	void OnFocusLost(bool) override
	{
		VideoDriver::GetInstance()->EditBoxLostFocus();
	}

	void OnMouseWheel(int wheel, WidgetID widget) override
	{
		if (widget != WID_AAT_BACKGROUND || !this->vscroll) return;

		/* Scroll the terminal view. */
		int scrollback_rows = this->terminal_session ? this->terminal_session->GetScrollbackRowCount() : 0;
		int new_pos = this->vscroll->GetPosition() - wheel * 3;  /* 3 lines per wheel notch */
		new_pos = std::clamp(new_pos, 0, std::max(0, scrollback_rows));
		this->vscroll->SetPosition(new_pos);

		/* Track if user is at bottom (auto-scroll) or has scrolled up. */
		this->scroll_offset = scrollback_rows - new_pos;

		this->SetDirty();
	}
};

/** Open the AI Agent terminal window. */
void ShowAIAgentTerminalWindow()
{
	/* Only allow one terminal window. */
	CloseWindowById(WC_AI_AGENT_TERMINAL, 0);
	new AIAgentTerminalWindow();
}
