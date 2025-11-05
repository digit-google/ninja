// Copyright 2012 Google Inc. All Rights Reserved.
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

#ifndef NINJA_SUBPROCESS_H_
#define NINJA_SUBPROCESS_H_

#include <string>
#include <vector>
#include <queue>

#ifdef _WIN32
#include <windows.h>
#else
#include <signal.h>
#endif

// ppoll() exists on FreeBSD, but only on newer versions.
#ifdef __FreeBSD__
#  include <sys/param.h>
#  if defined USE_PPOLL && __FreeBSD_version < 1002000
#    undef USE_PPOLL
#  endif
#endif

#include "exit_status.h"

/// Subprocess wraps a single async subprocess.  It is entirely
/// passive: it expects the caller to notify it when its fds are ready
/// for reading, as well as call Finish() to reap the child once done()
/// is true.
struct Subprocess {
  ~Subprocess();

  /// Returns ExitSuccess on successful process exit, ExitInterrupted if
  /// the process was interrupted, ExitFailure if it otherwise failed.
  ExitStatus Finish();

  void OnPipeReady();
  bool Done() const;

  /// Retrieve the command's combined stdout and stderr
  const std::string& GetOutput() const;

  /// Retrieve the command's stdout only.
  const std::string& GetStdout() const;

  /// Retrieve the command's stderr only.
  const std::string& GetStderr() const;

 private:
  Subprocess(bool use_console);
  bool Start(struct SubprocessSet* set, const std::string& command);

  /// The combined stdout + stderr output, note that this mixes
  /// both streams in unpredictable ways. This is maintained here
  /// in case some developer workflows depend on it.
  std::string combined_output_;

#ifdef _WIN32
  /// Models a single stdout or stderr buffer receiving data from
  /// the child process. Usage is:
  ///
  ///  1) Create instance.
  ///
  ///  2) Call Setup() to create the corresponding pipe and return
  ///     its write handle to be sent to the client in CreateProcess().
  ///
  ///     The pipe's read handle is stored in the instance, and a new
  ///     asynchronous connection i/o operation is started on it.
  ///
  ///  3) When the I/O Completion port receives a completion for the
  ///     subprocess, find which OutputPipe.overlapped_ address matches
  ///     it, then call OnPipeReady() on the corresponding instance.
  struct OutputPipe {
    /// Close instance on destruction.
    ~OutputPipe() { Close(); }

    /// Set up pipe as the parent-side pipe of the subprocess; return the
    /// other end of the pipe, usable in the child process. @arg ioport
    /// is the handle of an I/O Completion port that will receive events
    /// when data arrives on the read end of the pipe. @arg subproc is
    /// the Subprocess instance this OutputPipe belongs to.
    HANDLE Setup(HANDLE ioport, Subprocess* subproc);

    /// Close pipe read-end.
    void Close();

    /// Return true if this pipe is closed.
    bool IsClosed() const { return pipe_ == INVALID_HANDLE_VALUE; }

    /// Called when an i/o request completed for this pipe.
    /// The first one corresponds to the child process connecting to
    /// the pipe, while all other ones corresponds to output data
    /// arriving on the read handle or an error corresponding to the
    /// child process closing the pipe. Automatically restarts an
    /// async Read() i/o operation when needed.
    void OnPipeReady();

    Subprocess* subproc_ = nullptr;
    HANDLE pipe_ = INVALID_HANDLE_VALUE;
    bool is_reading_ = false;
    std::string buf_;
    OVERLAPPED overlapped_ = {};
    char overlapped_buf_[4 << 10];
  };

  /// Child process handle.
  HANDLE child_ = NULL;

  /// OutputPipe instance for the child's stdout and stderr streams.
  /// Only used when use_console_ is false.
  OutputPipe stdout_pipe_;
  OutputPipe stderr_pipe_;
#else   // !_WIN32

  /// A version of OutputPipe for Posix, usage is similar to its Win32 version.
  struct OutputPipe {
    ~OutputPipe() { Close(); }
    int Setup(Subprocess* subproc);
    void OnPipeReady();
    void Close();
    bool IsClosed() const { return fd_ == -1; }

    int fd_ = -1;
    std::string buf_;
    Subprocess* subproc_ = nullptr;
  };

  /// Output buffer for non-console subprocesses. Ignored if use_console_ is
  /// false.
  OutputPipe stdout_pipe_;
  OutputPipe stderr_pipe_;

  /// PID of the subprocess. Set to -1 when the subprocess is reaped.
  pid_t pid_ = -1;

  /// In POSIX platforms it is necessary to use waitpid(WNOHANG) to know whether
  /// a certain subprocess has finished. This is done for terminal subprocesses.
  /// However, this also causes the subprocess to be reaped before Finish() is
  /// called, so we need to store the ExitStatus so that a later Finish()
  /// invocation can return it.
  ExitStatus exit_status_ = ExitSuccess;

  /// Call waitpid() on the subprocess with the provided options and update the
  /// pid_ and exit_status_ fields.
  /// Return a boolean indicating whether the subprocess has indeed terminated.
  bool TryFinish(int waitpid_options);
#endif  // !_WIN32

  /// True if this subprocess should send its output directly to the current
  /// console / terminal. Used for launching commands from Ninja edges that
  /// belong to the "console" pool.
  bool use_console_ = false;

  friend struct SubprocessSet;
};

/// SubprocessSet runs a ppoll/pselect() loop around a set of Subprocesses.
/// DoWork() waits for any state change in subprocesses; finished_
/// is a queue of subprocesses as they finish.
struct SubprocessSet {
  SubprocessSet();
  ~SubprocessSet();

  Subprocess* Add(const std::string& command, bool use_console = false);
  bool DoWork();
  Subprocess* NextFinished();
  void Clear();

  std::vector<Subprocess*> running_;
  std::queue<Subprocess*> finished_;

#ifdef _WIN32
  static BOOL WINAPI NotifyInterrupted(DWORD dwCtrlType);
  static HANDLE ioport_;
#else
  static void SetInterruptedFlag(int signum);
  static void SigChldHandler(int signo, siginfo_t* info, void* context);

  /// Store the signal number that causes the interruption.
  /// 0 if not interruption.
  static volatile sig_atomic_t interrupted_;
  /// Whether ninja should quit. Set on SIGINT, SIGTERM or SIGHUP reception.
  static bool IsInterrupted() { return interrupted_ != 0; }
  static void HandlePendingInterruption();

  /// Initialized to 0 before ppoll/pselect().
  /// Filled to 1 by SIGCHLD handler when a child process terminates.
  static volatile sig_atomic_t s_sigchld_received;
  void CheckConsoleProcessTerminated();

  struct sigaction old_int_act_;
  struct sigaction old_term_act_;
  struct sigaction old_hup_act_;
  struct sigaction old_chld_act_;
  sigset_t old_mask_;
#endif
};

#endif // NINJA_SUBPROCESS_H_
