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

#include "jobserver.h"

#include "test.h"

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

namespace {

#ifndef _WIN32
struct ScopedTestFd {
  explicit ScopedTestFd(int fd) : fd_(fd) {}

  ~ScopedTestFd() {
    if (IsValid())
      ::close(fd_);
  }

  bool IsValid() const { return fd_ >= 0; }

  int fd_ = -1;
};
#endif  // !_WIN32

}  // namespace

TEST(Jobserver, SlotTest) {
  // Default construction.
  Jobserver::Slot slot;
  EXPECT_FALSE(slot.IsValid());

  // Construct implicit slot
  Jobserver::Slot slot0 = Jobserver::Slot::CreateImplicit();
  EXPECT_TRUE(slot0.IsValid());
  EXPECT_TRUE(slot0.IsImplicit());
  EXPECT_FALSE(slot0.IsExplicit());

  // Construct explicit slots
  auto slot1 = Jobserver::Slot::CreateExplicit(10u);
  EXPECT_TRUE(slot1.IsValid());
  EXPECT_FALSE(slot1.IsImplicit());
  EXPECT_TRUE(slot1.IsExplicit());
  EXPECT_EQ(10u, slot1.GetExplicitValue());

  auto slot2 = Jobserver::Slot::CreateExplicit(42u);
  EXPECT_TRUE(slot2.IsValid());
  EXPECT_FALSE(slot2.IsImplicit());
  EXPECT_TRUE(slot2.IsExplicit());
  EXPECT_EQ(42u, slot2.GetExplicitValue());

  // Move operation.
  slot2 = std::move(slot1);
  EXPECT_FALSE(slot1.IsValid());
  EXPECT_TRUE(slot2.IsValid());
  EXPECT_TRUE(slot2.IsExplicit());
  ASSERT_EQ(10u, slot2.GetExplicitValue());

  slot1 = std::move(slot0);
  EXPECT_FALSE(slot0.IsValid());
  EXPECT_TRUE(slot1.IsValid());
  EXPECT_TRUE(slot1.IsImplicit());
  EXPECT_FALSE(slot1.IsExplicit());
}

TEST(Jobserver, ParseMakeFlagsValue) {
  Jobserver::Config config;
  std::string error;

  // Passing nullptr does not crash.
  config = {};
  error.clear();
  ASSERT_TRUE(Jobserver::ParseMakeFlagsValue(nullptr, &config, &error));
  EXPECT_EQ(Jobserver::Config::kModeNone, config.mode);

  // Passing an empty string does not crash.
  config = {};
  error.clear();
  ASSERT_TRUE(Jobserver::ParseMakeFlagsValue("", &config, &error));
  EXPECT_EQ(Jobserver::Config::kModeNone, config.mode);

  // Passing a string that only contains whitespace does not crash.
  config = {};
  error.clear();
  ASSERT_TRUE(Jobserver::ParseMakeFlagsValue("  \t", &config, &error));
  EXPECT_EQ(Jobserver::Config::kModeNone, config.mode);

  // Passing an `n` in the first word reports no mode.
  config = {};
  error.clear();
  ASSERT_TRUE(Jobserver::ParseMakeFlagsValue("kns --jobserver-auth=fifo:foo",
                                             &config, &error));
  EXPECT_EQ(Jobserver::Config::kModeNone, config.mode);

  // Passing "--jobserver-auth=fifo:<path>" works.
  config = {};
  error.clear();
  ASSERT_TRUE(Jobserver::ParseMakeFlagsValue("--jobserver-auth=fifo:foo",
                                             &config, &error));
  EXPECT_EQ(Jobserver::Config::kModePosixFifo, config.mode);
  EXPECT_EQ("foo", config.path);

  // Passing an `n` in the first word _after_ a dash works though, i.e.
  // It is not interpreted as GNU Make dry-run flag.
  config = {};
  error.clear();
  ASSERT_TRUE(Jobserver::ParseMakeFlagsValue(
      "-one-flag --jobserver-auth=fifo:foo", &config, &error));
  EXPECT_EQ(Jobserver::Config::kModePosixFifo, config.mode);

  config = {};
  error.clear();
  ASSERT_TRUE(Jobserver::ParseMakeFlagsValue("--jobserver-auth=semaphore_name",
                                             &config, &error));
  EXPECT_EQ(Jobserver::Config::kModeWin32Semaphore, config.mode);
  EXPECT_EQ("semaphore_name", config.path);

  config = {};
  error.clear();
  ASSERT_TRUE(Jobserver::ParseMakeFlagsValue("--jobserver-auth=10,42", &config,
                                             &error));
  EXPECT_EQ(Jobserver::Config::kModeFileDescriptors, config.mode);
  EXPECT_EQ(10, config.read_fd);
  EXPECT_EQ(42, config.write_fd);

  config = {};
  error.clear();
  ASSERT_TRUE(Jobserver::ParseMakeFlagsValue("--jobserver-auth=-1,42", &config,
                                             &error));
  EXPECT_EQ(Jobserver::Config::kModeNone, config.mode);

  config = {};
  error.clear();
  ASSERT_TRUE(Jobserver::ParseMakeFlagsValue("--jobserver-auth=10,-42", &config,
                                             &error));
  EXPECT_EQ(Jobserver::Config::kModeNone, config.mode);

  config = {};
  error.clear();
  ASSERT_TRUE(Jobserver::ParseMakeFlagsValue(
      "--jobserver-auth=10,42 --jobserver-fds=12,44 "
      "--jobserver-auth=fifo:/tmp/fifo",
      &config, &error));
  EXPECT_EQ(Jobserver::Config::kModePosixFifo, config.mode);
  EXPECT_EQ("/tmp/fifo", config.path);

  config = {};
  error.clear();
  ASSERT_FALSE(
      Jobserver::ParseMakeFlagsValue("--jobserver-fds=10,", &config, &error));
  EXPECT_EQ("Invalid file descriptor pair [10,]", error);
}

TEST(Jobserver, NullJobserver) {
  Jobserver::Config config;
  ASSERT_EQ(Jobserver::Config::kModeNone, config.mode);

  std::string error;
  std::unique_ptr<Jobserver::Client> client =
      Jobserver::Client::Create(config, &error);
  EXPECT_FALSE(client.get());
  EXPECT_EQ("Unsupported jobserver mode", error);
}

#ifndef _WIN32
TEST(Jobserver, PosixFifoClient) {
  ScopedTempDir temp_dir;
  temp_dir.CreateAndEnter("ninja_test_jobserver_fifo");

  // Create the Fifo, then write kSlotCount slots into it.
  std::string fifo_path = temp_dir.temp_dir_name_ + "fifo";
  int ret = mknod(fifo_path.c_str(), S_IFIFO | 0666, 0);
  ASSERT_EQ(0, ret) << "Could not create FIFO at: " << fifo_path;

  const size_t kSlotCount = 5;

  ScopedTestFd write_fd(::open(fifo_path.c_str(), O_RDWR));
  ASSERT_TRUE(write_fd.IsValid()) << "Cannot open FIFO at: " << strerror(errno);
  for (size_t n = 0; n < kSlotCount; ++n) {
    uint8_t slot_byte = static_cast<uint8_t>('0' + n);
    ::write(write_fd.fd_, &slot_byte, 1);
  }
  // Keep the file descriptor opened to ensure the fifo's content
  // persists in kernel memory.

  // Create new client instance.
  Jobserver::Config config;
  config.mode = Jobserver::Config::kModePosixFifo;
  config.path = fifo_path;

  std::string error;
  std::unique_ptr<Jobserver::Client> client =
      Jobserver::Client::Create(config, &error);
  EXPECT_TRUE(client.get());
  EXPECT_TRUE(error.empty()) << error;

  // Read slots from the pool, and store them
  std::vector<Jobserver::Slot> slots;

  // First slot is always implicit.
  slots.push_back(client->TryAcquire());
  ASSERT_TRUE(slots.back().IsValid());
  EXPECT_TRUE(slots.back().IsImplicit());

  // Then read kSlotCount slots from the pipe and verify their value.
  for (size_t n = 0; n < kSlotCount; ++n) {
    Jobserver::Slot slot = client->TryAcquire();
    ASSERT_TRUE(slot.IsValid()) << "Slot #" << n + 1;
    EXPECT_EQ(static_cast<uint8_t>('0' + n), slot.GetExplicitValue());
    slots.push_back(std::move(slot));
  }

  // Pool should be empty now, so next TryAcquire() will fail.
  Jobserver::Slot slot = client->TryAcquire();
  EXPECT_FALSE(slot.IsValid());
}

TEST(Jobserver, PosixFifoClientWithWrongPath) {
  ScopedTempDir temp_dir;
  temp_dir.CreateAndEnter("ninja_test_jobserver_fifo");

  // Create a regular file.
  std::string file_path = temp_dir.temp_dir_name_ + "not_a_fifo";
  int fd = ::open(file_path.c_str(), O_CREAT | O_RDWR, 0660);
  ASSERT_GE(fd, 0) << "Could not create file: " << strerror(errno);
  ::close(fd);

  // Create new client instance, passing the file path for the fifo.
  Jobserver::Config config;
  config.mode = Jobserver::Config::kModePosixFifo;
  config.path = file_path;

  std::string error;
  std::unique_ptr<Jobserver::Client> client =
      Jobserver::Client::Create(config, &error);
  EXPECT_FALSE(client.get());
  EXPECT_FALSE(error.empty());
  EXPECT_EQ("Not a fifo path: " + file_path, error);

  // Do the same with an empty file path.
  error.clear();
  config.path.clear();
  client = Jobserver::Client::Create(config, &error);
  EXPECT_FALSE(client.get());
  EXPECT_FALSE(error.empty());
  EXPECT_EQ("Empty fifo path", error);
}

TEST(Jobserver, PosixPipePool) {
  const size_t kSlotCount = 10;
  std::string error;
  auto pool = Jobserver::Pool::Create(
      kSlotCount, Jobserver::Config::kModeFileDescriptors, &error);
  ASSERT_TRUE(pool.get()) << error;
  EXPECT_TRUE(error.empty());

  std::string makeflags = pool->GetEnvMakeFlagsValue();

  std::string auth_prefix = " -j --jobserver-fds=";
  ASSERT_EQ(auth_prefix, makeflags.substr(0, auth_prefix.size()));

  // Parse the MAKEFLAGS value to create a JobServer::Config
  Jobserver::Config config;
  ASSERT_TRUE(
      Jobserver::ParseMakeFlagsValue(makeflags.c_str(), &config, &error));
  EXPECT_EQ(config.mode, Jobserver::Config::kModeFileDescriptors);

  // Create a client from the Config, and try to read all slots.
  std::unique_ptr<Jobserver::Client> client =
      Jobserver::Client::Create(config, &error);
  EXPECT_TRUE(client.get());
  EXPECT_TRUE(error.empty()) << error;

  // First slot is always implicit.
  Jobserver::Slot slot = client->TryAcquire();
  EXPECT_TRUE(slot.IsValid());
  EXPECT_TRUE(slot.IsImplicit());

  // Then read kSlotCount - 1 slots from the pipe.
  for (size_t n = 1; n < kSlotCount; ++n) {
    Jobserver::Slot slot = client->TryAcquire();
    EXPECT_TRUE(slot.IsValid()) << "Slot #" << n + 1;
    EXPECT_TRUE(slot.IsExplicit()) << "Slot #" << n + 1;
  }

  // Pool should be empty now, so next TryAcquire() will fail.
  slot = client->TryAcquire();
  EXPECT_FALSE(slot.IsValid());
}

TEST(Jobserver, PosixFifoPool) {
  const size_t kSlotCount = 10;
  std::string error;
  auto pool = Jobserver::Pool::Create(
      kSlotCount, Jobserver::Config::kModePosixFifo, &error);
  ASSERT_TRUE(pool.get()) << error;
  EXPECT_TRUE(error.empty());

  std::string makeflags = pool->GetEnvMakeFlagsValue();

  std::string auth_prefix = " -j --jobserver-auth=fifo:";
  ASSERT_EQ(auth_prefix, makeflags.substr(0, auth_prefix.size()));

  // Parse the MAKEFLAGS value to create a JobServer::Config
  Jobserver::Config config;
  ASSERT_TRUE(
      Jobserver::ParseMakeFlagsValue(makeflags.c_str(), &config, &error));
  EXPECT_EQ(config.mode, Jobserver::Config::kModePosixFifo);

  // Create a client from the Config, and try to read all slots.
  std::unique_ptr<Jobserver::Client> client =
      Jobserver::Client::Create(config, &error);
  EXPECT_TRUE(client.get());
  EXPECT_TRUE(error.empty()) << error;

  // First slot is always implicit.
  Jobserver::Slot slot = client->TryAcquire();
  EXPECT_TRUE(slot.IsValid());
  EXPECT_TRUE(slot.IsImplicit());

  // Then read kSlotCount - 1 slots from the pipe.
  for (size_t n = 1; n < kSlotCount; ++n) {
    Jobserver::Slot slot = client->TryAcquire();
    EXPECT_TRUE(slot.IsValid()) << "Slot #" << n + 1;
    EXPECT_TRUE(slot.IsExplicit()) << "Slot #" << n + 1;
  }

  // Pool should be empty now, so next TryAcquire() will fail.
  slot = client->TryAcquire();
  EXPECT_FALSE(slot.IsValid());
}

#endif  // !_WIN32
