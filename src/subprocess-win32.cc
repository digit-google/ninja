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

#include "exit_status.h"
#include "subprocess.h"

#include <assert.h>
#include <stdio.h>

#include <algorithm>

#include "util.h"

Subprocess::Subprocess(bool use_console) : use_console_(use_console) {}

Subprocess::~Subprocess() {
  stdout_pipe_.Close();
  stderr_pipe_.Close();

  // Reap child if forgotten.
  if (child_)
    Finish();
}

void Subprocess::OutputPipe::Close() {
  if (pipe_ != INVALID_HANDLE_VALUE) {
    CloseHandle(pipe_);
    pipe_ = INVALID_HANDLE_VALUE;
  }
}

HANDLE Subprocess::OutputPipe::Setup(HANDLE ioport, Subprocess* subproc) {
  subproc_ = subproc;

  char pipe_name[100];
  snprintf(pipe_name, sizeof(pipe_name),
           "\\\\.\\pipe\\ninja_pid%lu_sp%p", GetCurrentProcessId(), this);

  pipe_ = ::CreateNamedPipeA(pipe_name,
                             PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
                             PIPE_TYPE_BYTE,
                             PIPE_UNLIMITED_INSTANCES,
                             0, 0, INFINITE, NULL);
  if (pipe_ == INVALID_HANDLE_VALUE)
    Win32Fatal("CreateNamedPipe");

  if (!CreateIoCompletionPort(pipe_, ioport,
                              reinterpret_cast<ULONG_PTR>(subproc), 0))
    Win32Fatal("CreateIoCompletionPort");

  overlapped_ = {};
  if (!ConnectNamedPipe(pipe_, &overlapped_) &&
      GetLastError() != ERROR_IO_PENDING) {
    Win32Fatal("ConnectNamedPipe");
  }

  is_reading_ = false;

  // Get the write end of the pipe as a handle inheritable across processes.
  HANDLE output_write_handle =
      CreateFileA(pipe_name, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
  HANDLE output_write_child;
  if (!DuplicateHandle(GetCurrentProcess(), output_write_handle,
                       GetCurrentProcess(), &output_write_child,
                       0, TRUE, DUPLICATE_SAME_ACCESS)) {
    Win32Fatal("DuplicateHandle");
  }
  CloseHandle(output_write_handle);

  return output_write_child;
}

bool Subprocess::Start(SubprocessSet* set, const std::string& command) {
  HANDLE stdout_child_pipe = INVALID_HANDLE_VALUE;
  HANDLE stderr_child_pipe = INVALID_HANDLE_VALUE;
  HANDLE nul = INVALID_HANDLE_VALUE;

  STARTUPINFOA startup_info = {};
  startup_info.cb = sizeof(STARTUPINFO);
  if (!use_console_) {
    stdout_child_pipe = stdout_pipe_.Setup(set->ioport_, this);
    stderr_child_pipe = stderr_pipe_.Setup(set->ioport_, this);

    SECURITY_ATTRIBUTES security_attributes = {};
    security_attributes.nLength = sizeof(SECURITY_ATTRIBUTES);
    security_attributes.bInheritHandle = TRUE;
    // Must be inheritable so subprocesses can dup to children.
    nul = CreateFileA("NUL", GENERIC_READ,
                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                      &security_attributes, OPEN_EXISTING, 0, NULL);
    if (nul == INVALID_HANDLE_VALUE)
      Fatal("couldn't open nul");

    startup_info.dwFlags = STARTF_USESTDHANDLES;
    startup_info.hStdInput = nul;
    startup_info.hStdOutput = stdout_child_pipe;
    startup_info.hStdError = stderr_child_pipe;
  }
  // In the console case, child_pipe is still inherited by the child and closed
  // when the subprocess finishes, which then notifies ninja.

  PROCESS_INFORMATION process_info;
  memset(&process_info, 0, sizeof(process_info));

  // Ninja handles ctrl-c, except for subprocesses in console pools.
  DWORD process_flags = use_console_ ? 0 : CREATE_NEW_PROCESS_GROUP;

  // Do not prepend 'cmd /c' on Windows, this breaks command
  // lines greater than 8,191 chars.
  DWORD error = 0;
  if (!CreateProcessA(NULL, const_cast<char*>(command.c_str()), NULL, NULL,
                      /* inherit handles */ TRUE, process_flags, NULL, NULL,
                      &startup_info, &process_info)) {
    error = GetLastError();
  }

  if (!use_console_) {
    // Close child channels as they are never used by the parent.
    CloseHandle(nul);
    CloseHandle(stdout_child_pipe);
    CloseHandle(stderr_child_pipe);
  }

  if (error == ERROR_FILE_NOT_FOUND) {
    // File (program) not found error is treated as a normal build
    // action failure.
    stdout_pipe_.Close();
    stderr_pipe_.Close();

    // child_ is already NULL;
    const char msg[] =
        "CreateProcess failed: The system cannot find the file "
        "specified.\n";
    stderr_pipe_.buf_ = msg;

    return true;
  } else if (error != 0) {
    fprintf(stderr, "\nCreateProcess failed. Command attempted:\n\"%s\"\n",
            command.c_str());
    const char* hint = NULL;
    // ERROR_INVALID_PARAMETER means the command line was formatted
    // incorrectly. This can be caused by a command line being too long or
    // leading whitespace in the command. Give extra context for this case.
    if (error == ERROR_INVALID_PARAMETER) {
      if (command.length() > 0 && (command[0] == ' ' || command[0] == '\t'))
        hint = "command contains leading whitespace";
      else
        hint = "is the command line too long?";
    }
    Win32Fatal("CreateProcess", hint);
  }

  CloseHandle(process_info.hThread);
  child_ = process_info.hProcess;

  return true;
}

const std::string& Subprocess::GetStdout() const {
  return stdout_pipe_.buf_;
}

const std::string& Subprocess::GetStderr() const {
  return stderr_pipe_.buf_;
}

void Subprocess::OutputPipe::OnPipeReady() {
  DWORD bytes;
  if (!GetOverlappedResult(pipe_, &overlapped_, &bytes, TRUE)) {
    if (GetLastError() == ERROR_BROKEN_PIPE) {
      Close();
      return;
    }
    Win32Fatal("GetOverlappedResult");
  }

  if (is_reading_ && bytes) {
    buf_.append(overlapped_buf_, bytes);
  }

  overlapped_ = {};
  is_reading_ = true;
  if (!::ReadFile(pipe_, overlapped_buf_, sizeof(overlapped_buf_),
                  &bytes, &overlapped_)) {
    if (GetLastError() == ERROR_BROKEN_PIPE) {
      Close();
      return;
    }
    if (GetLastError() != ERROR_IO_PENDING)
      Win32Fatal("ReadFile");
  }

  // Even if we read any bytes in the readfile call, we'll enter this
  // function again later and get them at that point.
}

ExitStatus Subprocess::Finish() {
  if (!child_)
    return ExitFailure;

  // TODO: add error handling for all of these.
  WaitForSingleObject(child_, INFINITE);

  DWORD exit_code = 0;
  GetExitCodeProcess(child_, &exit_code);

  CloseHandle(child_);
  child_ = NULL;

  return exit_code == CONTROL_C_EXIT ? ExitInterrupted :
                                       static_cast<ExitStatus>(exit_code);
}

bool Subprocess::Done() const {
  return stdout_pipe_.IsClosed() && stderr_pipe_.IsClosed();
}

HANDLE SubprocessSet::ioport_;

SubprocessSet::SubprocessSet() {
  ioport_ = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
  if (!ioport_)
    Win32Fatal("CreateIoCompletionPort");
  if (!SetConsoleCtrlHandler(NotifyInterrupted, TRUE))
    Win32Fatal("SetConsoleCtrlHandler");
}

SubprocessSet::~SubprocessSet() {
  Clear();

  SetConsoleCtrlHandler(NotifyInterrupted, FALSE);
  CloseHandle(ioport_);
}

BOOL WINAPI SubprocessSet::NotifyInterrupted(DWORD dwCtrlType) {
  if (dwCtrlType == CTRL_C_EVENT || dwCtrlType == CTRL_BREAK_EVENT) {
    if (!PostQueuedCompletionStatus(ioport_, 0, 0, NULL))
      Win32Fatal("PostQueuedCompletionStatus");
    return TRUE;
  }

  return FALSE;
}

Subprocess* SubprocessSet::Add(const std::string& command, bool use_console) {
  Subprocess *subprocess = new Subprocess(use_console);
  if (!subprocess->Start(this, command)) {
    delete subprocess;
    return 0;
  }
  if (subprocess->child_)
    running_.push_back(subprocess);
  else
    finished_.push(subprocess);
  return subprocess;
}

bool SubprocessSet::DoWork() {
  DWORD bytes_read;
  Subprocess* subproc;
  OVERLAPPED* overlapped;

  if (!GetQueuedCompletionStatus(ioport_, &bytes_read,
                                 reinterpret_cast<PULONG_PTR>(&subproc),
                                 &overlapped, INFINITE)) {
    if (GetLastError() != ERROR_BROKEN_PIPE)
      Win32Fatal("GetQueuedCompletionStatus");
  }

  if (!subproc) // A NULL subproc indicates that we were interrupted and is
                // delivered by NotifyInterrupted above.
    return true;

  if (overlapped == &subproc->stdout_pipe_.overlapped_)
    subproc->stdout_pipe_.OnPipeReady();
  else if (overlapped == &subproc->stderr_pipe_.overlapped_)
    subproc->stderr_pipe_.OnPipeReady();
  else
    Fatal("Unknown overlapped pointer %p for subprocess %p", overlapped,
          subproc);

  if (subproc->Done()) {
    std::vector<Subprocess*>::iterator end =
        std::remove(running_.begin(), running_.end(), subproc);
    if (running_.end() != end) {
      finished_.push(subproc);
      running_.resize(end - running_.begin());
    }
  }

  return false;
}

Subprocess* SubprocessSet::NextFinished() {
  if (finished_.empty())
    return NULL;
  Subprocess* subproc = finished_.front();
  finished_.pop();
  return subproc;
}

void SubprocessSet::Clear() {
  for (Subprocess* subproc : running_) {
    // Since the foreground process is in our process group, it will receive a
    // CTRL_C_EVENT or CTRL_BREAK_EVENT at the same time as us.
    if (subproc->child_ && !subproc->use_console_) {
      if (!GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT,
                                    GetProcessId(subproc->child_))) {
        Win32Fatal("GenerateConsoleCtrlEvent");
      }
    }
  }
  for (Subprocess* subproc : running_)
    delete subproc;
  running_.clear();
}
