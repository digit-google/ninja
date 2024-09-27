// Copyright 2013 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "line_printer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x4
#include <vector>
#endif
#else
#include <unistd.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <sys/time.h>
#endif

#include "elide_middle.h"
#include "util.h"

LinePrinter::LinePrinter() {
  // If TERM is not set, or is set to "dumb", force a dumb configuration.
  // If TERM is "ninja-test-terminal", force a smart terminal configuration
  // which supports ANSI colors. This is used by regression tests to
  // avoid creating ptys.
  const char* term = getenv("TERM");
  if (!term || !strcmp(term, "dumb")) {
    smart_terminal_ = false;
  } else if (!strcmp(term, "ninja-test-terminal")) {
    smart_terminal_ = true;
    // The test terminal width is 80 by default, but can be
    // overridden with NINJA_TEST_TERMINAL_WIDTH
    test_terminal_width_ = 80;
    const char* width_env = getenv("NINJA_TEST_TERMINAL_WIDTH");
    if (width_env) {
      test_terminal_width_ = static_cast<size_t>(atoi(width_env));
    }
  } else {
#ifndef _WIN32
    smart_terminal_ = isatty(1);
#else
    console_ = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    smart_terminal_ = GetConsoleScreenBufferInfo(console_, &csbi);
#endif
  }

  // Assume that a smart terminal supports ANSI color sequences,
  supports_color_ = smart_terminal_;

  // Setting CLICOLOR_FORCE=1 forces color supports.
  // Setting CLICOLOR_FORCE=0 disables it.
  const char* clicolor_force = getenv("CLICOLOR_FORCE");
  if (clicolor_force) {
    supports_color_ = !strcmp(clicolor_force, "1");
#ifdef _WIN32
  } else if (supports_color_ && !test_terminal_width_) {
    // On Windows, try to enable virtual terminal processing. This will fail
    // prior to Windows 10 because the console does not support ANSI color
    // sequences properly.
    DWORD mode;
    if (GetConsoleMode(console_, &mode) &&
        !SetConsoleMode(console_, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
      supports_color_ = false;
    }
#endif  // !_WIN32
  }
}

void LinePrinter::Print(std::string to_print, LineType type) {
  if (console_locked_) {
    line_buffer_ = std::move(to_print);
    line_type_ = type;
    return;
  }

  if (smart_terminal_) {
    printf("\r");  // Print over previous line, if any.
    // On Windows, calling a C library function writing to stdout also handles
    // pausing the executable when the "Pause" key or Ctrl-S is pressed.
  }

  if (!smart_terminal_ || type != ELIDE) {
    printf("%s\n", to_print.c_str());
    fflush(stdout);
    have_blank_line_ = true;
    return;
  }

  bool do_print = true;

  if (test_terminal_width_ > 0) {
    // This is the terminal used during regression tests.
    // Assume this supports all ANSI sequences, and avoid
    // using the Console API on Windows.
    ElideMiddleInPlace(to_print, test_terminal_width_);
  } else {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(console_, &csbi);
    size_t console_width = static_cast<size_t>(csbi.dwSize.X);

    ElideMiddleInPlace(to_print, console_width);

    if (!supports_color_) {
      // This code path is taken on Windows 8 and earlier versions of
      // the system which do not have proper VT/ANSI sequence parsing,
      // so the input shouldn't have any ANSI color sequences here.

      // We don't want to have the cursor spamming back and forth, so instead of
      // printf use WriteConsoleOutput which updates the contents of the buffer,
      // but doesn't move the cursor position.
      COORD buf_size = { csbi.dwSize.X, 1 };
      COORD zero_zero = { 0, 0 };
      SMALL_RECT target = { csbi.dwCursorPosition.X, csbi.dwCursorPosition.Y,
                            static_cast<SHORT>(csbi.dwCursorPosition.X +
                                               csbi.dwSize.X - 1),
                            csbi.dwCursorPosition.Y };
      std::vector<CHAR_INFO> char_data(console_width);
      for (size_t i = 0; i < console_width; ++i) {
        char_data[i].Char.AsciiChar = i < to_print.size() ? to_print[i] : ' ';
        char_data[i].Attributes = csbi.wAttributes;
      }
      WriteConsoleOutput(console_, &char_data[0], buf_size, zero_zero, &target);

      do_print = false;
    }
#else   // !_WIN32
    // Limit output to width of the terminal if provided so we don't cause
    // line-wrapping.
    winsize size;
    if ((ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0) && size.ws_col) {
      ElideMiddleInPlace(to_print, size.ws_col);
    }
#endif  // !_WIN32
  }

  if (do_print) {
    printf("%s\x1B[K", to_print.c_str());  // Print + clear to end of line.
    fflush(stdout);
  }

  have_blank_line_ = false;
}

void LinePrinter::PrintOrBuffer(const char* data, size_t size) {
  if (console_locked_) {
    output_buffer_.append(data, size);
  } else {
    // Avoid printf and C strings, since the actual output might contain null
    // bytes like UTF-16 does (yuck).
    fwrite(data, 1, size, stdout);
  }
}

void LinePrinter::PrintOnNewLine(const std::string& to_print) {
  if (console_locked_ && !line_buffer_.empty()) {
    output_buffer_.append(line_buffer_);
    output_buffer_.append(1, '\n');
    line_buffer_.clear();
  }
  if (!have_blank_line_) {
    PrintOrBuffer("\n", 1);
  }
  if (!to_print.empty()) {
    PrintOrBuffer(&to_print[0], to_print.size());
  }
  have_blank_line_ = to_print.empty() || to_print.back() == '\n';
}

void LinePrinter::SetConsoleLocked(bool locked) {
  if (locked == console_locked_)
    return;

  if (locked)
    PrintOnNewLine("");

  console_locked_ = locked;

  if (!locked) {
    PrintOnNewLine(output_buffer_);
    if (!line_buffer_.empty()) {
      Print(line_buffer_, line_type_);
    }
    output_buffer_.clear();
    line_buffer_.clear();
  }
}
