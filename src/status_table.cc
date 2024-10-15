// Copyright 2024 Google Inc. All Rights Reserved.
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

#include "status_table.h"

#include <stdint.h>
#include <string.h>

#include "assert.h"

#ifdef _WIN32
#undef min  // make std::min() work on Windows.
#endif

StatusTable::StatusTable(const Config& config) : config_(config) {
  commands_max_queue_.reserve(config_.max_commands);
  older_commands_.reserve(config_.max_commands);
}

StatusTable::~StatusTable() = default;

void StatusTable::BuildStarted() {
  last_update_time_ms_ = -1;
  last_command_id_ = 0;
}

void StatusTable::BuildEnded() {
  last_update_time_ms_ = -1;
  ClearTable();
}

void StatusTable::CommandStarted(CommandPointer command, int64_t start_time_ms,
                                 std::string description) {
  last_command_id_++;
  pending_commands_.emplace(
      std::piecewise_construct, std::forward_as_tuple(command),
      std::forward_as_tuple(start_time_ms, last_command_id_,
                            std::move(description)));
}

void StatusTable::CommandEnded(CommandPointer command) {
  auto it = pending_commands_.find(command);
  assert(it != pending_commands_.end());
  pending_commands_.erase(it);
}

void StatusTable::SetStatus(const std::string& status) {
  last_status_ = status;
}

void StatusTable::UpdateTable(int64_t build_time_ms) {
  if (last_update_time_ms_ >= 0) {
    int64_t since_last_ms = build_time_ms - last_update_time_ms_;
    if (since_last_ms < config_.refresh_timeout_ms) {
      // No need to update more than necessary when tasks complete
      // really really fast.
      return;
    }
  }
  last_update_time_ms_ = build_time_ms;

  // Compute the current time relative to the hidden build baseline.
  PrintPending(build_time_ms);
}

void StatusTable::PrintPending(int64_t build_time_ms) {
  size_t max_commands = config_.max_commands;
  if (!max_commands)
    return;

  // Find the |max_commands| older running edges, by using a fixed-size
  // max-priority-queue. Then sort the result from oldest to newest
  // commands.

  // Fill queue with empty items to simplify loop below.
  assert(commands_max_queue_.empty());
  commands_max_queue_.resize(max_commands);
  for (const auto& pair : pending_commands_) {
    int64_t start_time_ms = pair.second.start_time_ms;
    // If this command is newer than the current top, ignore it
    // otherwise replace the top with it in the queue.
    const auto& top = commands_max_queue_.top();
    if (!top || start_time_ms < top->start_time_ms) {
      commands_max_queue_.pop();
      commands_max_queue_.emplace(&pair.second);
    }
  }

  // Compute the older commands in _decreasing_ starting time,
  // then parse it in reverse order for printing (to avoid an
  // std::reverse() call).
  older_commands_.clear();
  while (!commands_max_queue_.empty()) {
    const CommandValue* command = commands_max_queue_.top();
    // Ignore empty entries, which happen when there are less
    // than |config_.max_commands| items in |pending_commands_|.
    if (command)
      older_commands_.push_back(command);
    commands_max_queue_.pop();
  }

  std::string pending_line;

  for (auto command_reverse_it = older_commands_.end();
       command_reverse_it != older_commands_.begin();) {
    const CommandValue* command = *(--command_reverse_it);
    // Format the elapsed time in a human friendly format.
    char elapsed_buffer[16];
    int64_t elapsed_ms = build_time_ms - command->start_time_ms;
    if (elapsed_ms < 0) {
      snprintf(elapsed_buffer, sizeof(elapsed_buffer), "??????");
    } else {
      if (elapsed_ms < 60000) {
        snprintf(elapsed_buffer, sizeof(elapsed_buffer), "%d.%ds",
                 static_cast<int>((elapsed_ms / 1000)),
                 static_cast<int>((elapsed_ms % 1000) / 100));
      } else {
        snprintf(elapsed_buffer, sizeof(elapsed_buffer), "%dm%ds",
                 static_cast<int>((elapsed_ms / 60000)),
                 static_cast<int>((elapsed_ms % 60000) / 1000));
      }
    }

    // Get edge description or command.
    StringPiece description = command->description;

    // Format '<elapsed> | <description>' where <elapsed> is
    // right-justified.
    size_t justification_width = 6;
    size_t elapsed_width = strlen(elapsed_buffer);
    size_t justified_elapsed_width =
        std::min(justification_width, elapsed_width);
    size_t needed_capacity = justified_elapsed_width + 3 + description.size();
    if (needed_capacity > pending_line.capacity())
      pending_line.reserve(needed_capacity);
    if (elapsed_width < justification_width) {
      pending_line.assign(justification_width - elapsed_width, ' ');
    } else {
      pending_line.clear();
    }
    pending_line.append(elapsed_buffer, elapsed_width);
    pending_line.append(" | ", 3);
    pending_line.append(description.begin(), description.size());

    PrintOnNextLine(pending_line);
  }

  // Clear previous lines that are not needed anymore.
  size_t count = older_commands_.size();
  size_t next_height = count;
  for (; count < last_command_count_; ++count) {
    ClearNextLine();
  }

  if (count > 0) {
    // Move up to the top status line. Then print the status
    // again to reposition the cursor to the right position.
    // Note that using ASCII sequences to save/restore the
    // cursor position does not work reliably in all terminals
    // (and terminal emulators like mosh or asciinema).
    MoveUp(count);
    PrintOnCurrentLine(last_status_);
  }
  Flush();

  last_command_count_ = next_height;
}

void StatusTable::PrintOnCurrentLine(const std::string& line) {
  printf("%s\x1B[0K", line.c_str());
}

void StatusTable::PrintOnNextLine(const std::string& line) {
  printf("\n");
  PrintOnCurrentLine(line);
}

void StatusTable::ClearNextLine() {
  printf("\x1B[1B\x1B[2K");
}

void StatusTable::MoveUp(size_t lines_count) {
  printf("\x1B[%dA", static_cast<int>(lines_count));
}

void StatusTable::Flush() {
  fflush(stdout);
}

void StatusTable::ClearTable() {
  if (last_command_count_ == 0)
    return;

  // repeat "go down 1 line; erase whole line" |last_command_count_| times.
  for (size_t n = 0; n < last_command_count_; ++n)
    ClearNextLine();

  // move up |last_height_| lines.
  MoveUp(last_command_count_);
  Flush();

  last_command_count_ = 0;
}
