#!/usr/bin/env python3
# Copyright 2024 Google Inc. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Setup a GNU Make Jobserver token pool then launch a command with it.

On Windows, this only supports the semaphore-based scheme.
On Posix, this uses a FIFO by default, except if `--pipe` is used.

NOTE: This is a basic implementation that doesn't support broken
      protocol clients that release more tokens than they acquired
      to the pool. Using these in your build will result in extra job
      slots being created, severely degrading overall performance
      over time.

See --help-usage for usage examples.
"""
import argparse
import os
import platform
import subprocess
import sys

_DEFAULT_NAME = "jobserver_tokens"
_IS_WINDOWS = platform.system() == "Windows"

if _IS_WINDOWS:
    try:
        # This requires pywin32 to be installed.
        import win32event
        import win32api
        import winerror
    except ModuleNotFoundError as e:
        print(
            "\nERROR: Could not import Win32 API, please install pywin32, e.g. `python -m pip install pywin32`.\n",
            file=sys.stderr,
        )
        raise e

    def create_sem(sem_name: str, token_count: int) -> None:
        """Create and initialize Win32 semaphore."""
        assert token_count > 0, f"Token count must be strictly positive"
        handle = win32event.CreateSemaphore(
            None, token_count, token_count - 1, sem_name  # Default security attributes,
        )
        assert handle != 0, f"Error creating Win32 semaphore {winerror.GetLastError()}"
        env = dict(os.environ)
        env["MAKEFLAGS"] = " -j --jobserver-auth=" + sem_name
        return handle, env

    def print_usage() -> int:
        print(
            """Example usage:

# Start <command> after setting the server to provide as many tokens
# as available CPUs (the default)
python \\path\\to\\jobserver_pool.py <command>

# Start <command> with a fixed number of tokens
python \\path\\to\\jobserver_pool.py --token-count=10 <command>

# Disable the feature with a non-positive count. This is equivalent
# to running <command> directly.
python \\path\\to\\jobserver_pool.py --token-count=0 <command>

# Use a specific semaphore name
python \\path\\to\\jobserver_pool.py --name my_build_jobs <command>

# Setup jobserver then start new interactive PowerShell
# session, print MAKEFLAGS value, build stuff, then exit.
python \\path\\to\\jobserver_pool.py
$env:MAKEFLAGS
... build stuff ...
exit
"""
        )
        return 0

else:

    def create_pipe(token_count: int) -> None:
        """Create and fill Posix PIPE."""
        read_fd, write_fd = os.pipe()
        os.set_inheritable(read_fd, True)
        os.set_inheritable(write_fd, True)
        assert token_count > 0, f"Token count must be strictly positive"
        os.write(write_fd, (token_count - 1) * b"x")
        env = dict(os.environ)
        env["MAKEFLAGS"] = (
            f" -j --jobserver-fds={read_fd},{write_fd} --jobserver-auth={read_fd},{write_fd}"
        )
        return read_fd, write_fd, env

    def create_fifo(path: str, token_count: int) -> None:
        """Create and fill Posix FIFO."""
        if os.path.exists(path):
            os.remove(path)

        os.mkfifo(path)

        read_fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK)
        write_fd = os.open(path, os.O_WRONLY | os.O_NONBLOCK)
        assert token_count > 0, f"Token count must be strictly positive"
        os.write(write_fd, (token_count - 1) * b"x")
        env = dict(os.environ)
        env["MAKEFLAGS"] = " -j --jobserver-auth=fifo:" + path
        return read_fd, write_fd, env

    def print_usage() -> int:
        print(
            """Example usage:

# Start <command> after setting the job pool to provide as many tokens
# as available CPUs (the default)
/path/to/jobserver_pool.py <command>

# Start <command> with a fixed number of tokens
/path/to/jobserver_pool.py --token-count=10 <command>

# Disable the feature with a non-positive count. This is equivalent
# to running <command> directly.
/path/to/jobserver_pool.py --token-count=0 <command>

# Use a specific FIFO path
/path/to/jobserver_pool.py --fifo /tmp/my_build_jobs <command>

# Setup jobserver then start new interactive Bash shell
# session, print MAKEFLAGS value, build stuff, then exit.
/path/to/jobserver_pool.py bash -i
echo "$MAKEFLAGS"
... build stuff ...
exit
"""
        )
        return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawTextHelpFormatter
    )
    if _IS_WINDOWS:
        parser.add_argument(
            "--name",
            help=f"Specify semaphore name, default is {_DEFAULT_NAME}",
            default=_DEFAULT_NAME,
        )
    else:
        mutex_group = parser.add_mutually_exclusive_group()
        mutex_group.add_argument(
            "--pipe",
            action="store_true",
            help="Implement the pool with a Unix pipe (the default)",
        )
        mutex_group.add_argument(
            "--fifo",
            help=f"Specify FIFO file path, default is $(pwd)/{_DEFAULT_NAME}",
            default=os.path.abspath(_DEFAULT_NAME),
        )

    parser.add_argument(
        "--help-usage", action="store_true", help="Print usage examples."
    )

    parser.add_argument(
        "--token-count",
        action="store",
        default=str(os.cpu_count()),
        help="Set token count, default is available CPUs count",
    )

    parser.add_argument("command", nargs=argparse.REMAINDER, help="Command to run.")
    args = parser.parse_args()

    if args.help_usage:
        return print_usage()

    if not args.command:
        parser.error("This script requires at least one command argument!")

    token_count = int(args.token_count)
    if token_count <= 0:
        # Disable the feature.
        ret = subprocess.run(args.command)
    elif _IS_WINDOWS:
        # Run with a Window semaphore.
        handle, env = create_sem(args.name, token_count)
        ret = subprocess.run(args.command, env=env)
        win32api.CloseHandle(handle)
    else:
        # Run with pipe descriptors.
        delete_fifo = ""
        if args.pipe:
            read_fd, write_fd, env = create_pipe(int(args.token_count))
            ret = subprocess.run(args.command, env=env, pass_fds=(read_fd, write_fd))
        elif args.fifo:
            read_fd, write_fd, env = create_fifo(args.fifo, int(args.token_count))
            ret = subprocess.run(args.command, env=env)
            delete_fifo = args.fifo

        os.close(read_fd)
        os.close(write_fd)

        if delete_fifo:
            os.remove(delete_fifo)

    return ret.returncode


if __name__ == "__main__":
    sys.exit(main())
