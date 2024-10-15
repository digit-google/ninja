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

#ifndef NINJA_STATUS_TABLE_H_
#define NINJA_STATUS_TABLE_H_

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

#include "string_piece.h"

/// A class used to display a table of pending commands during
/// the build on smart terminals, using ANSI sequences whenever possible.
///
/// Concrete implementations should override the GetCommandDescription()
/// and PrintOnCurrentLine() method to work on smart terminals.
///
/// Usage is the following:
///
///  1) Create instance, passing configuration information and
///     a valid AsyncLoop reference.
///
///  2) Call `BuildStarted()` when the build starts. Similarly, call
///     `BuildEnded()` when it stops.
///
///  3) Call `CommandStarted(command)` whenever a new command is started, where
///     |command| is a unique pointer value for the command, whose description
///     can be returned by `GetCommandDescription(command)`.
///
///  4) Call `CommandEnded(command)` when a given command ended. This does not
///     update the table, only internal counters.
///
///  5) Call `SetStatus(status_line)` whenever the status line changes.
///     It is needed to restore the cursor position after printing the table,
///     unfortunately (there is no good way to save cursor positions with
///     various terminal emulators).
///
///  6) Call `ClearTable()` to clear the table (e.g. before printing something,
///     or at the end of the build). Call `UpdateTable()` to print the table
///     if needed (e.g. if it was cleared previously, or if enough time has
///     passed since the last call).
///
class StatusTable {
 public:
  /// An opaque type describing a given unique command.
  using CommandPointer = const void*;

  /// Configuration information for a new StatusTable instance.
  ///
  /// |max_commands| is the maximum number of commands to print in the table.
  /// A value of 0 completely disables the feature.
  ///
  /// |refresh_timeout_ms| is the periodic refresh timeout used by the
  /// internal timer. A negative value disables the feature, and the table
  /// will only be updated on ClearTable() and UpdateTable() calls.
  ///
  struct Config {
    Config() = default;
    Config(size_t a_max_commands, int64_t a_refresh_timeout_ms)
        : max_commands(a_max_commands),
          refresh_timeout_ms(a_refresh_timeout_ms) {}

    size_t max_commands = 0;
    int64_t refresh_timeout_ms = -1;
  };

  /// Constructor. |max_commands| is the maximum number of commands to print
  /// (a value of 0 disables the feature). |refresh_timeout_ms| is the minimum
  /// refresh timeout (a value of 0 disables the timer). The |async_loop|
  /// reference is used to create a timer for periodic updates.
  StatusTable(const Config& config);

  /// Destructor.
  virtual ~StatusTable();

  /// Call this when starting a new build.
  void BuildStarted();

  /// Call this when a new command is starting.
  void CommandStarted(CommandPointer command, int64_t start_time_ms);

  /// Call this when a started command completes.
  /// It is a runtime error to use a |command| value that does not
  /// match a previous CommandStart() call, that was not previously
  /// finished.
  void CommandEnded(CommandPointer command);

  /// Call this when the build has completed.
  void BuildEnded();

  /// Update the table after some time has passed.
  void UpdateTable(int64_t cur_time_ms);

  /// Call this to update the status at the top of the table, and update
  /// the commands below it if needed.
  void SetStatus(const std::string& status);

  /// Call this to clear the table, if any.
  void ClearTable();

  /// Call this to update the table if needed.
  void UpdateTable();

 protected:
  /// The following methods can be overriden by sub-classes.
  /// Both GetCommandDescription() and PrintOnCurrentLine() should be
  /// overridden for proper command output in smart terminals.

  /// Return a string describing a command.
  /// This method should be overridden by derived classes, as the default
  /// simply returns "command <number>".
  virtual std::string GetCommandDescription(CommandPointer command) const;

  /// Print |line| from the start of the current line, and place the cursor
  /// right after it, clearing anything after it. This must *not* print more
  /// than the terminal width, nor move the cursor to the next line.
  ///
  /// This method should be overridden by derived classes, as the default
  /// prints on the current line without trying to limit the width, then
  /// does an ANSI "erase from cursor to end of line" sequence.
  virtual void PrintOnCurrentLine(const std::string& line);

  /// The following methods can be overriden for tests. Their default behavior
  /// is to use standard ANSI sequences, and eventually the above two methods.

  /// Jump to the next line, then print |line| on it just like
  /// PrintOnCurrentLine.
  virtual void PrintOnNextLine(const std::string& line);

  /// Move down to the next line, then clear it completely. The cursor can
  /// stay on the same column.
  virtual void ClearNextLine();

  /// Move up |lines_count| lines. The cursor can stay on the same column.
  virtual void MoveUp(size_t lines_count);

  /// Flush all previous commands to final terminal.
  virtual void Flush();

 private:
  /// Support for printing pending commands below the status on smart terminals.
  /// |build_time_millis| is a timestamp relative to the start of the build.
  void PrintPending(int64_t build_time_millis);

  Config config_;
  size_t last_command_count_ = 0;

  // This is the timestsamp of the last table update.
  int64_t last_update_time_ms_ = -1;

  std::string last_status_;

  // Record pending commands. This maps an opaque command pointer
  // to its build start time in milliseconds, and its description.
  struct CommandValue {
    CommandValue() = default;
    CommandValue(int64_t a_start_time_ms, std::string a_description)
        : start_time_ms(a_start_time_ms),
          description(std::move(a_description)) {}

    int64_t start_time_ms = INT64_MAX;
    std::string description;
  };
  using CommandMap = std::unordered_map<CommandPointer, CommandValue>;
  CommandMap pending_commands_;

  // A CommandInfo wraps a pointer to a CommandMap entry with default values
  // and methods appropriate for the MaxQueue type below. In particular
  // start_time_ms() will return INT64_MAX for default-initialized instances.
  struct CommandInfo {
    CommandInfo() = default;
    CommandInfo(const CommandMap::value_type& v) : v_(&v) {}

    int64_t start_time_ms() const {
      return v_ ? v_->second.start_time_ms : INT64_MAX;
    }

    CommandPointer command() const { return v_ ? v_->first : nullptr; }

    StringPiece description() const {
      return v_ ? StringPiece(v_->second.description) : StringPiece();
    };

    bool operator<(const CommandInfo& other) const {
      return start_time_ms() < other.start_time_ms();
    }

   private:
    const CommandMap::value_type* v_ = nullptr;
  };

  // MaxQueue is a fixed-size max-heap of CommandInfo values, sorted by
  // their start time (larger start time / newer command at the top).
  struct MaxQueue : public std::priority_queue<CommandInfo> {
    void reserve(size_t capacity) { c.reserve(capacity); }
    void resize(size_t size) { c.resize(size); }
  };

  MaxQueue commands_max_queue_;
  std::vector<CommandInfo> older_commands_;
};

#endif  // NINJA_STATUS_TABLE_H_
