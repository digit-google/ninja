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

#include <stdarg.h>

#include "test.h"
#include "util.h"

namespace {

void StringAppendFormatArgs(std::string* str, const char* fmt, va_list ap) {
  const int max_retries = 10;
  size_t org_size = str->size();
  for (int retries = 0; retries < max_retries; ++retries) {
    size_t available = str->capacity() - org_size;
    if (available == 0) {
      available = str->capacity();
    }

    // Make [0..org_size + available] bytes accessible.
    // This includes room for the \0 terminator.
    str->resize(org_size + available);

    va_list ap2;
    va_copy(ap2, ap);
    int ret = vsnprintf(const_cast<char*>(str->data() + org_size),
                        available + 1, fmt, ap2);
    va_end(ap2);

    if (ret < 0) {
      // Some versions of the MS C runtime return -1 in case of truncation!
      // For these, simply double the buffer size and retry.
      str->reserve(str->capacity() * 2);
      continue;
    }

    // Resize to correct size. Return only if result is smaller than
    // available bytes. Otherwise try again with the correct capacity.
    str->resize(org_size + static_cast<size_t>(ret));
    if (ret <= static_cast<int>(available))
      return;
  }
  Fatal("Could not format string!");
}

std::string StringFormat(const char* fmt, ...) {
  std::string result;
  va_list ap;
  va_start(ap, fmt);
  StringAppendFormatArgs(&result, fmt, ap);
  va_end(ap);
  return result;
}

void StringAppendFormat(std::string* str, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  StringAppendFormatArgs(str, fmt, ap);
  va_end(ap);
}

std::string GetDescription(StatusTable::CommandPointer command) {
  return StringFormat("command_%zu", reinterpret_cast<size_t>(command));
}

}  // namespace

/// A StatusTable sub-class that just appends method calls to a log string.
struct TestStatusTable : public StatusTable {
  TestStatusTable(const StatusTable::Config& config) : StatusTable(config) {}

  void PrintOnCurrentLine(const std::string& line) override {
    StringAppendFormat(&log_, "PrintOnCurrentLine(%s)\n", line.c_str());
  }

  void PrintOnNextLine(const std::string& line) override {
    StringAppendFormat(&log_, "PrintOnNextLine(%s)\n", line.c_str());
  }

  void ClearNextLine() override { log_ += "ClearNextLine()\n"; }

  void MoveUp(size_t line_count) override {
    StringAppendFormat(&log_, "MoveUp(%zd)\n", line_count);
  }

  void Flush() override { log_ += "Flush()\n"; }

  static StatusTable::CommandPointer MakeCommand(size_t n) {
    return reinterpret_cast<StatusTable::CommandPointer>(n);
  }

  // Return log (and clear it as well in the instance)
  std::string log() { return std::move(log_); }

  mutable std::string log_;
};

TEST(StatusTable, NoCommands) {
  StatusTable::Config table_config(0, 100);
  TestStatusTable table(table_config);
  ASSERT_EQ(table.log(), "");

  table.SetStatus("some_status");
  table.BuildStarted();

  auto cmd1 = table.MakeCommand(1);
  auto cmd2 = table.MakeCommand(2);
  auto cmd3 = table.MakeCommand(3);

  table.CommandStarted(cmd1, 0, GetDescription(cmd1));
  table.CommandStarted(cmd2, 0, GetDescription(cmd2));
  table.CommandStarted(cmd3, 0, GetDescription(cmd3));
  table.UpdateTable(0);

  ASSERT_EQ(table.log(), "");

  table.CommandEnded(cmd2);
  table.CommandEnded(cmd3);
  table.UpdateTable(500);

  ASSERT_EQ(table.log(), "");

  table.BuildEnded();
  ASSERT_EQ(table.log(), "");
}

TEST(StatusTable, TwoCommandsNoPeriodicUpdates) {
  StatusTable::Config table_config(2, 100);
  TestStatusTable table(table_config);
  ASSERT_EQ(table.log(), "");

  table.SetStatus("some_status");
  table.BuildStarted();

  auto cmd1 = table.MakeCommand(1);
  auto cmd2 = table.MakeCommand(2);
  auto cmd3 = table.MakeCommand(3);

  table.CommandStarted(cmd1, 0, GetDescription(cmd1));
  table.CommandStarted(cmd2, 250, GetDescription(cmd2));
  table.CommandStarted(cmd3, 570, GetDescription(cmd3));
  table.UpdateTable(570);

  ASSERT_EQ(table.log(),
            "PrintOnNextLine(  0.5s | command_1)\n"
            "PrintOnNextLine(  0.3s | command_2)\n"
            "MoveUp(2)\n"
            "PrintOnCurrentLine(some_status)\n"
            "Flush()\n");

  table.CommandEnded(cmd1);
  table.UpdateTable(670);
  ASSERT_EQ(table.log(),
            "PrintOnNextLine(  0.4s | command_2)\n"
            "PrintOnNextLine(  0.1s | command_3)\n"
            "MoveUp(2)\n"
            "PrintOnCurrentLine(some_status)\n"
            "Flush()\n");

  table.CommandEnded(cmd2);
  table.UpdateTable(1070);

  ASSERT_EQ(table.log(),
            "PrintOnNextLine(  0.5s | command_3)\n"
            "ClearNextLine()\n"
            "MoveUp(2)\n"
            "PrintOnCurrentLine(some_status)\n"
            "Flush()\n");

  table.CommandEnded(cmd3);
  table.UpdateTable(1270);
  ASSERT_EQ(table.log(),
            "ClearNextLine()\n"
            "MoveUp(1)\n"
            "PrintOnCurrentLine(some_status)\n"
            "Flush()\n");

  table.BuildEnded();
  table.UpdateTable(1270);
  ASSERT_EQ(table.log_, "Flush()\n");
}

TEST(StatusTable, TwoCommandsWithPeriodicUpdates) {
  StatusTable::Config table_config(2, 100);
  TestStatusTable table(table_config);
  ASSERT_EQ(table.log(), "");

  table.SetStatus("some_status");
  table.BuildStarted();

  auto cmd1 = table.MakeCommand(1);
  auto cmd2 = table.MakeCommand(2);
  auto cmd3 = table.MakeCommand(3);

  table.CommandStarted(cmd1, 0, GetDescription(cmd1));
  table.CommandStarted(cmd2, 250, GetDescription(cmd2));
  table.CommandStarted(cmd3, 570, GetDescription(cmd3));
  table.UpdateTable(570);

  ASSERT_EQ(table.log(),
            "PrintOnNextLine(  0.5s | command_1)\n"
            "PrintOnNextLine(  0.3s | command_2)\n"
            "MoveUp(2)\n"
            "PrintOnCurrentLine(some_status)\n"
            "Flush()\n");

  // Check that if not enough time passes by, no update is performed.
  table.UpdateTable(620);
  ASSERT_EQ(table.log(), "");

  table.UpdateTable(670);
  ASSERT_EQ(table.log(),
            "PrintOnNextLine(  0.6s | command_1)\n"
            "PrintOnNextLine(  0.4s | command_2)\n"
            "MoveUp(2)\n"
            "PrintOnCurrentLine(some_status)\n"
            "Flush()\n");

  table.log_.clear();
  table.UpdateTable(770);
  ASSERT_EQ(table.log(),
            "PrintOnNextLine(  0.7s | command_1)\n"
            "PrintOnNextLine(  0.5s | command_2)\n"
            "MoveUp(2)\n"
            "PrintOnCurrentLine(some_status)\n"
            "Flush()\n");

  table.CommandEnded(cmd1);
  table.UpdateTable(870);
  ASSERT_EQ(table.log(),
            "PrintOnNextLine(  0.6s | command_2)\n"
            "PrintOnNextLine(  0.3s | command_3)\n"
            "MoveUp(2)\n"
            "PrintOnCurrentLine(some_status)\n"
            "Flush()\n");

  table.CommandEnded(cmd2);
  table.UpdateTable(1270);

  ASSERT_EQ(table.log(),
            "PrintOnNextLine(  0.7s | command_3)\n"
            "ClearNextLine()\n"
            "MoveUp(2)\n"
            "PrintOnCurrentLine(some_status)\n"
            "Flush()\n");

  table.CommandEnded(cmd3);
  table.UpdateTable(1370);
  ASSERT_EQ(table.log(),
            "ClearNextLine()\n"
            "MoveUp(1)\n"
            "PrintOnCurrentLine(some_status)\n"
            "Flush()\n");

  table.BuildEnded();
  table.UpdateTable(1370);
  ASSERT_EQ(table.log_, "Flush()\n");
}

TEST(StatusTable, ProperCommandDurations) {
  // Perform a first build.
  StatusTable::Config table_config(2, 100);
  TestStatusTable table(table_config);
  ASSERT_EQ(table.log(), "");

  table.SetStatus("some_status");
  table.BuildStarted();

  auto cmd1 = table.MakeCommand(1);
  auto cmd2 = table.MakeCommand(2);
  auto cmd3 = table.MakeCommand(3);

  table.CommandStarted(cmd1, 0, GetDescription(cmd1));
  table.CommandStarted(cmd2, 250, GetDescription(cmd2));
  table.CommandStarted(cmd3, 570, GetDescription(cmd3));
  table.UpdateTable(570);

  ASSERT_EQ(table.log(),
            "PrintOnNextLine(  0.5s | command_1)\n"
            "PrintOnNextLine(  0.3s | command_2)\n"
            "MoveUp(2)\n"
            "PrintOnCurrentLine(some_status)\n"
            "Flush()\n");

  // Terminate build and clear log.
  table.CommandEnded(cmd3);
  table.CommandEnded(cmd2);
  table.CommandEnded(cmd1);
  table.BuildEnded();
  (void)table.log();

  // Perform second build. Verify that the durations are correct.
  table.BuildStarted();
  table.CommandStarted(cmd1, 10000, GetDescription(cmd1));
  table.CommandStarted(cmd2, 10250, GetDescription(cmd2));
  table.CommandStarted(cmd3, 10570, GetDescription(cmd3));
  table.UpdateTable(10570);

  ASSERT_EQ(table.log(),
            "PrintOnNextLine(  0.5s | command_1)\n"
            "PrintOnNextLine(  0.3s | command_2)\n"
            "MoveUp(2)\n"
            "PrintOnCurrentLine(some_status)\n"
            "Flush()\n");
}
