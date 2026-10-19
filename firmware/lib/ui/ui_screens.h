// The screens of PLAN-ESP32.md §3.4, rendered as text for the grids of §3.2.
//
// One pure function: the board's state and the facts in, the lines of the screen out — the status
// bar, the body rows, and the key hint worded for the fitted layout. Two text sizes give two grids,
// both 21 columns wide:
//
//   small  the 5x8 font: two status lines, five body rows, the hint — 8 lines, one per page
//   large  the 6x13B:    one status line, three body rows, the hint in the small font — 5 lines
//
// A screen's content can be longer than its body: a list keeps its cursor in view, anything else
// turns a page per UP/DOWN (Board::scroll()), and the display draws a scroll bar from scrollTotal
// and scrollFirst. display.cpp puts the lines on the panel; the host tests check every screen fits.
#pragma once

#include <stdint.h>

#include "ui_board.h"
#include "ui_post.h"

namespace ui {

enum class TextSize : uint8_t { Small, Large };  // the order of kTextSizeLabels

constexpr uint8_t kScreenCols = 21;
constexpr uint8_t kMaxScreenLines = 8;

constexpr uint8_t statusRows(TextSize size) { return size == TextSize::Small ? 2 : 1; }
constexpr uint8_t bodyRows(TextSize size) { return size == TextSize::Small ? 5 : 3; }
constexpr uint8_t screenLines(TextSize size) { return statusRows(size) + bodyRows(size) + 1; }

struct ScreenText {
  uint8_t count = 0;  // screenLines(): the status lines, the body rows, then the hint
  char lines[kMaxScreenLines][kScreenCols + 1];
  // A content longer than the body: how many lines it has, and the first one shown.
  uint8_t scrollTotal = 0;
  uint8_t scrollFirst = 0;
  // Lines that had to be cut to 21 columns: a test failure, never a device one.
  uint8_t overflow = 0;
};

void render(const Board& board, const Facts& facts, Layout layout, TextSize size, uint32_t nowMs,
            ScreenText& out);

// The POST screens of §3.3, on the same grids but without a status bar: a title on the first line,
// the checks — as they run, the fatal one, or the ones that failed — and a hint on the last.
void renderPost(const Post& post, TextSize size, ScreenText& out);

}  // namespace ui
