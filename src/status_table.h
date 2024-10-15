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
/// Concrete implementations should override the PrintOnCurrentLine() method
/// to work on smart terminals.
///
/// Usage is the following:
///
///  1) Create instance, passing configuration information.
///
///  2) Call `BuildStarted()` when the build starts. Similarly, call
///     `BuildEnded()` when it stops.
///
///  3) Call `CommandStarted(command, start_time_ms, description)` whenever a
///     new command is started, where |command| is a unique pointer value for
///     the command, while |start_time_ms| is the command's start time, relative
///     to the start of the build (exact epoch doesn't need to be known and
///     generally corresponds to a time prior to the call to BuildStarted()).
///
///  4) Call `CommandEnded(command)` when a given command ended. This does not
///     update the table, only internal counters.
///
///  5) Call `UpdateTable(build_time_ms)` whenever the content of the table
///     needs to be updated after some time has passed, and where
///     |build_time_ms| is a timestamp in milli-seconds relative to the
///     same epoch as the values passed to CommandStarted().
///
///  6) Call `ClearTable()` to clear the table. This is useful before
///     printing command outputs or errors, or before passing ownership
///     of the terminal to commands in the "console" pool.
///
///  7) Call `SetStatus(status_line)` whenever the status line changes.
///     It is needed to restore the cursor position after printing the table,
///     unfortunately (there is no good way to save cursor positions with
///     various terminal emulators).
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

  /// Set or update the status at the top of the table. Must be called at
  /// least once before UpdateTable().
  void SetStatus(const std::string& status);

  /// Call this when starting a new build.
  void BuildStarted();

  /// Call this when a new command is starting.
  /// \a command is a unique opaque pointer for the command.
  /// \a start_time_ms is the command's start time in milli-seconds, relative to
  /// the start of the build (exact epoch doesn't need to be known, and may
  /// differ from the time when BuildStarted() was called).
  /// \a descrirption is the command's description that will appear in the
  /// table.
  void CommandStarted(CommandPointer command, int64_t start_time_ms,
                      std::string description);

  /// Call this when a started command completes.
  /// It is a runtime error to use a |command| value that does not
  /// match a previous CommandStart() call, that was not previously
  /// finished.
  void CommandEnded(CommandPointer command);

  /// Call this when the build has completed.
  void BuildEnded();

  /// Update the table after some time has passed.
  /// This is the only method, with ClearTable(), that prints anything to the
  /// terminal. Note that since the ANSI sequences used to save/restore cursor
  /// positions do not work properly in many terminal emulators, this will also
  /// reprint the last SetStatus() line on-top of the table.
  void UpdateTable(int64_t build_time_ms);

  /// Call this to clear the table, if any. Does not reprint the status while
  /// preserving the cursor position.
  void ClearTable();

 protected:
  /// The following methods can be overridden by sub-classes.
  /// Both GetCommandDescription() and PrintOnCurrentLine() should be
  /// overridden for proper command output in smart terminals.

  /// Print |line| from the start of the current line, and place the cursor
  /// right after it, clearing anything after it. This must *not* print more
  /// than the terminal width, nor move the cursor to the next line.
  ///
  /// This method should be overridden by derived classes, as the default
  /// prints on the current line without trying to limit the width, then
  /// does an ANSI "erase from cursor to end of line" sequence.
  virtual void PrintOnCurrentLine(const std::string& line);

  /// The following methods can be overridden for tests. Their default behavior
  /// is to print standard ANSI sequences to stdout, which is difficult to
  /// collect and print/compare when unit test fails.

  /// Jump to the next line, then print |line| on it just like
  /// PrintOnCurrentLine.
  virtual void PrintOnNextLine(const std::string& line);

  /// Move down to the next line, then clear it completely. The cursor must
  /// stay on the same column.
  virtual void ClearNextLine();

  /// Move up |lines_count| lines. The cursor must stay on the same column.
  virtual void MoveUp(size_t lines_count);

  /// Flush all previous commands to final terminal.
  virtual void Flush();

 private:
  /// Support for printing pending commands below the status on smart terminals.
  /// |build_time_ms| is a timestamp relative to the start of the build, using
  /// the same epoch as CommandStarted() or UpdateTable().
  void PrintPending(int64_t build_time_ms);

  Config config_;
  size_t last_command_count_ = 0;
  size_t last_command_id_ = 0;

  // This is the timestsamp of the last table update.
  int64_t last_update_time_ms_ = -1;

  std::string last_status_;

  // Record pending commands. This maps an opaque command pointer
  // to its build start time in milliseconds, and its description.
  //
  // Ninja will frequently call StartCommand() for two distinct commands using
  // the same start time value. Since the sort being used is not stable, this
  // results in unpredictable swaps of the two commands when they are displayed
  // in the table, which is annoying. To avoid this, each command is assigned a
  // unique monotically increasing id, which is only used to differentiate them
  // here, guaranteeing stable sort order when the start times are identical.
  struct CommandValue {
    CommandValue() = default;
    CommandValue(int64_t a_start_time_ms, size_t command_id,
                 std::string a_description)
        : start_time_ms(a_start_time_ms), command_id(command_id),
          description(std::move(a_description)) {}

    int64_t start_time_ms = INT64_MAX;
    size_t command_id = 0;
    std::string description;
  };

  using CommandMap = std::unordered_map<CommandPointer, CommandValue>;

  // The table of all commands waiting to be completed.
  CommandMap pending_commands_;

  // A CommandInfo wraps a pointer to a CommandValue entry with default values
  // and methods appropriate for the MaxQueue type below. In particular
  // start_time_ms() will return INT64_MAX for default-initialized instances.

  // A comparator struct used for the MaxQueue type below, which stores
  // pointers to CommandValue instances. A nullptr value corresponds to
  // a start time of INT64_MAX milliseconds.
  struct CommandValuePtrLess {
    bool operator()(const CommandValue* a, const CommandValue* b) const {
      int64_t a_start_time = a ? a->start_time_ms : INT64_MAX;
      int64_t b_start_time = b ? b->start_time_ms : INT64_MAX;
      if (a_start_time != b_start_time)
        return a_start_time < b_start_time;

      size_t a_id = a ? a->command_id : 0;
      size_t b_id = b ? b->command_id : 0;

      return a_id < b_id;
    }
  };

  // MaxQueue is a fixed-size max-heap of CommandValue pointers, sorted by
  // their start time (larger start time / newer command at the top).
  struct MaxQueue : public std::priority_queue<const CommandValue*,
                                               std::vector<const CommandValue*>,
                                               CommandValuePtrLess> {
    void reserve(size_t capacity) { c.reserve(capacity); }
    void resize(size_t size) { c.resize(size); }
  };

  // These two values are only used within UpdateTable() but I stored
  // here to avoid performing heap allocations on each call.
  MaxQueue commands_max_queue_;
  std::vector<const CommandValue*> older_commands_;
};

#endif  // NINJA_STATUS_TABLE_H_
