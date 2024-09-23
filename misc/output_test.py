#!/usr/bin/env python3

"""Runs ./ninja and checks if the output is correct.

In order to simulate a smart terminal it uses the 'script' command.
"""

import os
import platform
import subprocess
import sys
import tempfile
import unittest
from textwrap import dedent
import typing as T

_IS_WINDOWS = platform.system() == "Windows"

default_env = dict(os.environ)
default_env.pop("NINJA_STATUS", None)
default_env.pop("CLICOLOR_FORCE", None)
default_env["TERM"] = ""
NINJA_PATH = os.path.abspath("./ninja")
if _IS_WINDOWS:
    NINJA_PATH += ".exe"

assert os.path.exists(NINJA_PATH), f"Cannot find Ninja: {NINJA_PATH}"


class BuildDir:
    def __init__(self, build_ninja: str):
        self.build_ninja = dedent(build_ninja)
        self.d = None

    def __enter__(self):
        self.d = tempfile.TemporaryDirectory()
        with open(os.path.join(self.d.name, "build.ninja"), "w") as f:
            f.write(self.build_ninja)
            f.flush()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.d.cleanup()

    def run_cmd(
        self,
        cmd_args: T.Sequence[str] = [],
        env: T.Dict[str, str] = default_env,
        cwd: T.Union[None, str] = "",
    ) -> str:
        """Run Ninja command and retrieve its output.

        This runs the command directly, without trying to run it in
        a pseudo smart terminal (pty on Posix, console on Windows).

        Args:
            cmd_args: A list of command-line arguments to pass to Ninja.
            env: An optional alternative environment to run the command in.
            cwd: An optional current directory to run the command in.
                By default, it is run in the build directory's path.
                Use "" (an empty string) to run in the current directory instead.
        Returns:
            the combined stdout and stderr as a string, with \r removed when
            running on Windows.
        """
        ninja_cmd = [NINJA_PATH] + cmd_args
        if cwd == "":
            cwd = self.d.name
        ret = subprocess.run(
            ninja_cmd,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            cwd=cwd,
            env=env,
        )
        if ret.returncode != 0:
            if ret.stdout is not None:
                sys.stdout.write(ret.stdout)
            if ret.stderr is not None:
                sys.stdout.write(ret.stderr)
            ret.check_returncode()
        final_output = ""
        for line in ret.stdout.splitlines(True):
            if len(line) > 0 and line[-1] == "\r":
                continue
            final_output += line.replace("\r", "")
        return final_output

    def run(
        self,
        flags: str = "",
        pipe: bool = False,
        env: T.Dict[str, str] = default_env,
    ) -> str:
        ninja_cmd = "{} {}".format(NINJA_PATH, flags)
        try:
            if pipe:
                output = subprocess.check_output(
                    [ninja_cmd], shell=True, cwd=self.d.name, env=env
                )
            elif platform.system() == "Darwin":
                output = subprocess.check_output(
                    ["script", "-q", "/dev/null", "bash", "-c", ninja_cmd],
                    cwd=self.d.name,
                    env=env,
                )
            else:
                output = subprocess.check_output(
                    ["script", "-qfec", ninja_cmd, "/dev/null"],
                    cwd=self.d.name,
                    env=env,
                )
        except subprocess.CalledProcessError as err:
            sys.stdout.buffer.write(err.output)
            raise err
        final_output = ""
        for line in output.decode("utf-8").splitlines(True):
            if len(line) > 0 and line[-1] == "\r":
                continue
            final_output += line.replace("\r", "")
        return final_output


def run_plan(
    build_ninja: str,
    args: T.Sequence[str] = [],
    env: T.Dict[str, str] = default_env,
) -> str:
    """Convenience function to run Ninja with a given build plan.

    Everything is written to and runs in a temporary directory.

    Args:
        build_ninja: Content of the build.ninja file.
        args: List of Ninja command arguments.
        env: Optional environment dictionary to run the command in.

    Returns:
        the combined stdout + stderr for the command as a string,
        with \r removed when running on Windows.
    """
    with BuildDir(build_ninja) as b:
        return b.run_cmd(args, env)


def run_with_color(
    build_ninja: str,
    flags: str = "",
    env: T.Dict[str, str] = default_env,
) -> str:
    """Same as run_plan(), but uses a pseudo smart terminal to run the command.

    Useful to test smart terminal detection and status outputs.
    Currently only works on Posix.

    Args:
        build_ninja: Content of the build.ninja file.
        flags: Extra Ninja command-line arguments as a single string.
        env: Optional environment to run the command in.

    Returns:
        Combined stdout + stderr output as a single string.
    """
    with BuildDir(build_ninja) as b:
        return b.run(flags, False, env)


class Output(unittest.TestCase):
    ECHO_COMMAND = "echo" if _IS_WINDOWS else "printf"
    BUILD_SIMPLE_ECHO = """\
rule echo
  command = {print} "do thing"
  description = echo $out

build a:echo
""".format(
        print=ECHO_COMMAND
    )

    def test_issue_1418(self) -> None:
        if _IS_WINDOWS:
            build_plan = r"""rule echo
  command = cmd /c "sleep $delay && echo $out"
  description = echo $out

build a: echo
  delay = 3
build b: echo
  delay = 2
build c: echo
  delay = 1
"""
        else:
            build_plan = r"""rule echo
  command = sleep $delay && echo $out
  description = echo $out

build a: echo
  delay = 3
build b: echo
  delay = 2
build c: echo
  delay = 1
"""
        expected_output = """[1/3] echo c
c
[2/3] echo b
b
[3/3] echo a
a
"""
        self.assertEqual(run_plan(build_plan, ["-j3"]), expected_output)

    @unittest.skipIf(
        platform.system() == "Windows", "These test methods do not work on Windows"
    )
    def test_issue_1214(self) -> None:
        print_red = """rule echo
  command = printf '\x1b[31mred\x1b[0m'
  description = echo $out

build a: echo
"""
        # Only strip color when ninja's output is piped.
        self.assertEqual(
            run_with_color(print_red),
            """[1/1] echo a\x1b[K
\x1b[31mred\x1b[0m
""",
        )
        self.assertEqual(
            run_plan(print_red),
            """[1/1] echo a
red
""",
        )
        # Even in verbose mode, colors should still only be stripped when piped.
        self.assertEqual(
            run_with_color(print_red, flags="-v"),
            """[1/1] printf '\x1b[31mred\x1b[0m'
\x1b[31mred\x1b[0m
""",
        )
        self.assertEqual(
            run_plan(print_red, ["-v"]),
            """[1/1] printf '\x1b[31mred\x1b[0m'
red
""",
        )

        # CLICOLOR_FORCE=1 can be used to disable escape code stripping.
        env = default_env.copy()
        env["CLICOLOR_FORCE"] = "1"
        self.assertEqual(
            run_plan(print_red, [], env=env),
            """[1/1] echo a
\x1b[31mred\x1b[0m
""",
        )

    def test_issue_1966(self) -> None:
        if _IS_WINDOWS:
            # The copy command prints the path of each input file on new lines
            # so redirect to NUL to get rid of this.
            command = 'cmd /c "copy $rspfile + $rspfile $out > NUL"'
            command_expanded = 'cmd /c "copy cat.rsp + cat.rsp a > NUL"'
        else:
            command = "cat $rspfile $rspfile > $out"
            command_expanded = "cat cat.rsp cat.rsp > a"

        build_plan = f"""rule cat
  command = {command}
  rspfile = cat.rsp
  rspfile_content = a b c

build a: cat
"""
        expected = f"[1/1] {command_expanded}\n"

        with BuildDir(build_plan) as b:
            self.assertEqual(b.run_cmd(["-j3"]), expected)

    def test_pr_1685(self) -> None:
        # Running those tools without .ninja_deps and .ninja_log shouldn't fail.
        with BuildDir("") as b:
            self.assertEqual(b.run_cmd(["-t", "recompact"]), "")
            self.assertEqual(b.run_cmd(["-t", "restat"]), "")

    def test_issue_2048(self) -> None:
        with tempfile.TemporaryDirectory() as d:
            with open(os.path.join(d, "build.ninja"), "w"):
                pass

            with open(os.path.join(d, ".ninja_log"), "w") as f:
                f.write("# ninja log v4\n")

            try:
                output = subprocess.check_output(
                    [NINJA_PATH, "-t", "recompact"],
                    cwd=d,
                    env=default_env,
                    stderr=subprocess.STDOUT,
                    text=True,
                )

                self.assertEqual(
                    output.strip(),
                    "ninja: warning: build log version is too old; starting over",
                )
            except subprocess.CalledProcessError as err:
                self.fail("non-zero exit code with: " + err.output)

    def test_depfile_directory_creation(self) -> None:
        b = BuildDir(
            """\
            rule touch
              command = touch $out && echo "$out: extra" > $depfile

            build somewhere/out: touch
              depfile = somewhere_else/out.d
            """
        )
        with b:
            self.assertEqual(
                b.run_cmd([]),
                dedent(
                    """\
                [1/1] touch somewhere/out && echo "somewhere/out: extra" > somewhere_else/out.d
                """
                ),
            )
            self.assertTrue(os.path.isfile(os.path.join(b.d.name, "somewhere", "out")))
            self.assertTrue(
                os.path.isfile(os.path.join(b.d.name, "somewhere_else", "out.d"))
            )

    def test_status(self) -> None:
        with BuildDir("") as b:
            self.assertEqual(b.run_cmd(), "ninja: no work to do.\n")
            self.assertEqual(b.run_cmd(["--quiet"]), "")

    def test_ninja_status_default(self) -> None:
        "Do we show the default status by default?"
        with BuildDir(Output.BUILD_SIMPLE_ECHO) as b:
            self.assertEqual(b.run_cmd([]), "[1/1] echo a\ndo thing\n")

    @unittest.skipIf(
        platform.system() == "Windows", "These test methods do not work on Windows"
    )
    def test_ninja_status_default_with_color(self) -> None:
        "Do we show the default status by default?"
        self.assertEqual(
            run_with_color(Output.BUILD_SIMPLE_ECHO), "[1/1] echo a\x1b[K\ndo thing\n"
        )

    def test_ninja_status_quiet(self) -> None:
        "Do we suppress the status information when --quiet is specified?"
        with BuildDir(Output.BUILD_SIMPLE_ECHO) as b:
            output = b.run_cmd(["--quiet"])
            self.assertEqual(output, "do thing\n")

    def test_entering_directory_on_stdout(self) -> None:
        with BuildDir(Output.BUILD_SIMPLE_ECHO) as b:
            output = b.run_cmd(["-C", b.d.name], cwd=None)
            self.assertEqual(output.splitlines()[0][:25], "ninja: Entering directory")

    def test_tool_inputs(self) -> None:
        plan = """
rule cat
  command = cat $in $out
build out1 : cat in1
build out2 : cat in2 out1
build out3 : cat out2 out1 | implicit || order_only
"""
        with BuildDir(plan) as b:
            self.assertEqual(
                b.run_cmd(["-t", "inputs", "out3"]),
                """implicit
in1
in2
order_only
out1
out2
""",
            )

            self.assertEqual(
                b.run_cmd(["-t", "inputs", "--dependency-order", "out3"]),
                """in2
in1
out1
out2
implicit
order_only
""",
            )

        # Verify that results are shell-escaped by default, unless --no-shell-escape
        # is used. Also verify that phony outputs are never part of the results.
        quote = '"' if platform.system() == "Windows" else "'"

        plan = """
rule cat
  command = cat $in $out
build out1 : cat in1
build out$ 2 : cat out1
build out$ 3 : phony out$ 2
build all: phony out$ 3
"""

        with BuildDir(plan) as b:
            # Quoting changes the order of results when sorting alphabetically.
            self.assertEqual(
                b.run_cmd(["-t", "inputs", "all"]),
                f"""{quote}out 2{quote}
in1
out1
""",
            )

            self.assertEqual(
                b.run_cmd(["-t", "inputs", "--no-shell-escape", "all"]),
                """in1
out 2
out1
""",
            )

            # But not when doing dependency order.
            self.assertEqual(
                b.run_cmd(["-t", "inputs", "--dependency-order", "all"]),
                f"""in1
out1
{quote}out 2{quote}
""",
            )

            self.assertEqual(
                b.run_cmd(
                    ["-t", "inputs", "--dependency-order", "--no-shell-escape", "all"]
                ),
                f"""in1
out1
out 2
""",
            )

            self.assertEqual(
                b.run_cmd(
                    [
                        "-t",
                        "inputs",
                        "--dependency-order",
                        "--no-shell-escape",
                        "--print0",
                        "all",
                    ]
                ),
                f"""in1\0out1\0out 2\0""",
            )

    def test_explain_output(self):
        if _IS_WINDOWS:
            command_source = 'cmd /c "if not exist $out (echo x > $out)"'
            command_expanded = 'cmd /c "if not exist input (echo x > input)"'
        else:
            command_source = "[ -e $out ] || touch $out"
            command_expanded = "[ -e input ] || touch input"

        build_plan = f"""\
            build .FORCE: phony
            rule create_if_non_exist
              command = {command_source}
              restat = true
            rule write
              command = cp $in $out
            build input : create_if_non_exist .FORCE
            build mid : write input
            build output : write mid
            default output
            """
        with BuildDir(build_plan) as b:
            # The explain output is shown just before the relevant build:
            self.assertEqual(
                b.run_cmd(["-v", "-d", "explain"]),
                dedent(
                    f"""\
                ninja explain: .FORCE is dirty
                [1/3] {command_expanded}
                ninja explain: input is dirty
                [2/3] cp input mid
                ninja explain: mid is dirty
                [3/3] cp mid output
                """
                ),
            )
            # Don't print "ninja explain: XXX is dirty" for inputs that are
            # pruned from the graph by an earlier restat.
            self.assertEqual(
                b.run_cmd(["-v", "-d", "explain"]),
                dedent(
                    f"""\
                ninja explain: .FORCE is dirty
                [1/3] {command_expanded}
                """
                ),
            )


if __name__ == "__main__":
    unittest.main()
