#!/usr/bin/env python3
import sys

# Generate a build.ninja file that contains a non-UTF8 0xa0 byte
# in the msvc_deps_prefix value.
sys.stdout.buffer.write(b"""# Auto-generated

rule fake_cc
    command = .\\ninja_fake_win32_compiler $out $in
        deps = msvc
        msvc_deps_prefix = Included[A0]

build out1: fake_cc in1.cc
""".replace(b"[A0]", b"\xa0"))
sys.stdout.flush()
