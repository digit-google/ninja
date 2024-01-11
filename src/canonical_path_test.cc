// Copyright 2023 Google Inc. All Rights Reserved.
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

#include "canonical_path.h"

#include "test.h"

TEST(CanonicalPath, Empty) {
  CanonicalPath empty;
  EXPECT_TRUE(empty.value().empty());
  EXPECT_EQ(empty.value(), "");
  EXPECT_EQ(empty.slash_bits(), 0u);
}

TEST(CanonicalPath, Simple) {
  CanonicalPath path("foo/bar");
  EXPECT_EQ(path.value(), "foo/bar");
  EXPECT_EQ(path.slash_bits(), 0u);
}

TEST(CanonicalPath, CopyAndMove) {
  CanonicalPath path1("foo/bar");
  CanonicalPath path2 = path1;
  EXPECT_EQ(path1.value(), "foo/bar");
  EXPECT_EQ(path1.value(), path2.value());

  CanonicalPath path3 = std::move(path1);
  EXPECT_EQ("", path1.value());
  EXPECT_EQ("foo/bar", path3.value());
}

TEST(CanonicalPath, PathSamples) {
  CanonicalPath path;
  EXPECT_EQ("", path.value());
  EXPECT_EQ(0u, path.slash_bits());

  path = CanonicalPath("foo.h");
  EXPECT_EQ("foo.h", path.value());
  EXPECT_EQ(0u, path.slash_bits());

  path = CanonicalPath("./foo.h");
  EXPECT_EQ("foo.h", path.value());
  EXPECT_EQ(0u, path.slash_bits());

  path = CanonicalPath("./foo/./bar.h");
  EXPECT_EQ("foo/bar.h", path.value());
  EXPECT_EQ(0u, path.slash_bits());

  path = CanonicalPath("./x/foo/../bar.h");
  EXPECT_EQ("x/bar.h", path.value());

  path = CanonicalPath("./x/foo/../../bar.h");
  EXPECT_EQ("bar.h", path.value());

  path = CanonicalPath("foo//bar");
  EXPECT_EQ("foo/bar", path.value());

  path = CanonicalPath("foo//.//..///bar");
  EXPECT_EQ("bar", path.value());

  path = CanonicalPath("./x/../foo/../../bar.h");
  EXPECT_EQ("../bar.h", path.value());

  path = CanonicalPath("foo/./.");
  EXPECT_EQ("foo", path.value());

  path = CanonicalPath("foo/bar/..");
  EXPECT_EQ("foo", path.value());

  path = CanonicalPath("foo/.hidden_bar");
  EXPECT_EQ("foo/.hidden_bar", path.value());

  path = CanonicalPath("/foo");
  EXPECT_EQ("/foo", path.value());

  path = CanonicalPath("//foo");
#ifdef _WIN32
  EXPECT_EQ("//foo", path.value());
#else
  EXPECT_EQ("/foo", path.value());
#endif

  path = CanonicalPath("..");
  EXPECT_EQ("..", path.value());

  path = CanonicalPath("../");
  EXPECT_EQ("..", path.value());

  path = CanonicalPath("../foo");
  EXPECT_EQ("../foo", path.value());

  path = CanonicalPath("../..");
  EXPECT_EQ("../..", path.value());

  path = CanonicalPath("../../");
  EXPECT_EQ("../..", path.value());

  path = CanonicalPath("./../");
  EXPECT_EQ("..", path.value());

  path = CanonicalPath("/../");
  EXPECT_EQ("/..", path.value());

  path = CanonicalPath("/../..");
  EXPECT_EQ("/../..", path.value());

  path = CanonicalPath("/../../");
  EXPECT_EQ("/../..", path.value());

  path = CanonicalPath("/");
  EXPECT_EQ("/", path.value());

  path = CanonicalPath("/foo/..");
  EXPECT_EQ("/", path.value());

  path = CanonicalPath(".");
  EXPECT_EQ(".", path.value());

  path = CanonicalPath("./.");
  EXPECT_EQ(".", path.value());

  path = CanonicalPath("foo/..");
  EXPECT_EQ(".", path.value());

  path = CanonicalPath("foo/.._bar");
  EXPECT_EQ("foo/.._bar", path.value());
}

#ifdef _WIN32
TEST(CanonicalPath, PathSamplesWindows) {
  CanonicalPath path;
  EXPECT_EQ("", path.value());

  path = CanonicalPath("foo.h");
  EXPECT_EQ("foo.h", path.value());

  path = CanonicalPath(".\\foo.h");
  EXPECT_EQ("foo.h", path.value());

  path = CanonicalPath(".\\foo\\.\\bar.h");
  EXPECT_EQ("foo/bar.h", path.value());

  path = CanonicalPath(".\\x\\foo\\..\\bar.h");
  EXPECT_EQ("x/bar.h", path.value());

  path = CanonicalPath(".\\x\\foo\\..\\..\\bar.h");
  EXPECT_EQ("bar.h", path.value());

  path = CanonicalPath("foo\\\\bar");
  EXPECT_EQ("foo/bar", path.value());

  path = CanonicalPath("foo\\\\.\\\\..\\\\\\bar");
  EXPECT_EQ("bar", path.value());

  path = CanonicalPath(".\\x\\..\\foo\\..\\..\\bar.h");
  EXPECT_EQ("../bar.h", path.value());

  path = CanonicalPath("foo\\.\\.");
  EXPECT_EQ("foo", path.value());

  path = CanonicalPath("foo\\bar\\..");
  EXPECT_EQ("foo", path.value());

  path = CanonicalPath("foo\\.hidden_bar");
  EXPECT_EQ("foo/.hidden_bar", path.value());

  path = CanonicalPath("\\foo");
  EXPECT_EQ("/foo", path.value());

  path = CanonicalPath("\\\\foo");
  EXPECT_EQ("//foo", path.value());

  path = CanonicalPath("\\");
  EXPECT_EQ("/", path.value());
}

TEST(CanonicalPath, SlashTracking) {
  CanonicalPath path;

  path = CanonicalPath("foo.h");
  EXPECT_EQ("foo.h", path.value());
  EXPECT_EQ(0, path.slash_bits());

  path = CanonicalPath("a\\foo.h");
  EXPECT_EQ("a/foo.h", path.value());
  EXPECT_EQ(1, path.slash_bits());

  path = CanonicalPath("a/bcd/efh\\foo.h");
  EXPECT_EQ("a/bcd/efh/foo.h", path.value());
  EXPECT_EQ(4, path.slash_bits());

  path = CanonicalPath("a\\bcd/efh\\foo.h");
  EXPECT_EQ("a/bcd/efh/foo.h", path.value());
  EXPECT_EQ(5, path.slash_bits());

  path = CanonicalPath("a\\bcd\\efh\\foo.h");
  EXPECT_EQ("a/bcd/efh/foo.h", path.value());
  EXPECT_EQ(7, path.slash_bits());

  path = CanonicalPath("a/bcd/efh/foo.h");
  EXPECT_EQ("a/bcd/efh/foo.h", path.value());
  EXPECT_EQ(0, path.slash_bits());

  path = CanonicalPath("a\\./efh\\foo.h");
  EXPECT_EQ("a/efh/foo.h", path.value());
  EXPECT_EQ(3, path.slash_bits());

  path = CanonicalPath("a\\../efh\\foo.h");
  EXPECT_EQ("efh/foo.h", path.value());
  EXPECT_EQ(1, path.slash_bits());

  path = CanonicalPath("a\\b\\c\\d\\e\\f\\g\\foo.h");
  EXPECT_EQ("a/b/c/d/e/f/g/foo.h", path.value());
  EXPECT_EQ(127, path.slash_bits());

  path = CanonicalPath("a\\b\\c\\..\\..\\..\\g\\foo.h");
  EXPECT_EQ("g/foo.h", path.value());
  EXPECT_EQ(1, path.slash_bits());

  path = CanonicalPath("a\\b/c\\../../..\\g\\foo.h");
  EXPECT_EQ("g/foo.h", path.value());
  EXPECT_EQ(1, path.slash_bits());

  path = CanonicalPath("a\\b/c\\./../..\\g\\foo.h");
  EXPECT_EQ("a/g/foo.h", path.value());
  EXPECT_EQ(3, path.slash_bits());

  path = CanonicalPath("a\\b/c\\./../..\\g/foo.h");
  EXPECT_EQ("a/g/foo.h", path.value());
  EXPECT_EQ(1, path.slash_bits());

  path = CanonicalPath("a\\\\\\foo.h");
  EXPECT_EQ("a/foo.h", path.value());
  EXPECT_EQ(1, path.slash_bits());

  path = CanonicalPath("a/\\\\foo.h");
  EXPECT_EQ("a/foo.h", path.value());
  EXPECT_EQ(0, path.slash_bits());

  path = CanonicalPath("a\\//foo.h");
  EXPECT_EQ("a/foo.h", path.value());
  EXPECT_EQ(1, path.slash_bits());
}

TEST(CanonicalPath, TooManyComponents) {
  CanonicalPath path;

  // 64 is OK.
  path = CanonicalPath(
      "a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./"
      "a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./x.h");
  EXPECT_EQ(path.slash_bits(), 0u);

  // Backslashes version.
  path = CanonicalPath(
      "a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\"
      "a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\"
      "a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\"
      "a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\x.h");

  EXPECT_EQ(path.slash_bits(), 0xffffffffu);

  // 65 is OK if #component is less than 60 after path canonicalization.
  path = CanonicalPath(
      "a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./"
      "a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./a/./x/y.h");
  EXPECT_EQ(path.slash_bits(), 0u);

  // Backslashes version.
  path = CanonicalPath(
      "a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\"
      "a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\"
      "a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\"
      "a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\a\\.\\x\\y.h");
  EXPECT_EQ(path.slash_bits(), 0x1ffffffffu);

  // 59 after canonicalization is OK.
  path = CanonicalPath(
      "a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/"
      "a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/x/y.h");
  EXPECT_EQ(58, std::count(path.value().begin(), path.value().end(), '/'));
  EXPECT_EQ(path.slash_bits(), 0x0u);

  // Backslashes version.
  path = CanonicalPath(
      "a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\"
      "a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\"
      "a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\"
      "a\\a\\a\\a\\a\\a\\a\\a\\a\\x\\y.h");
  EXPECT_EQ(58, std::count(path.value().begin(), path.value().end(), '/'));
  EXPECT_EQ(path.slash_bits(), 0x3ffffffffffffffu);

  // More than 60 components is now completely ok too.
  path = CanonicalPath(
      "a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\"
      "a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\"
      "a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\"
      "a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\"
      "a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\"
      "a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\"
      "a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\"
      "a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\"
      "a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\"
      "a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\"
      "a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\"
      "a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\"
      "a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\a\\"
      "a\\a\\a\\a\\a\\a\\a\\a\\a\\x\\y.h");
  EXPECT_EQ(218, std::count(path.value().begin(), path.value().end(), '/'));
  EXPECT_EQ(path.slash_bits(), 0xffffffffffffffff);
}
#else   // !_WIN32
TEST(CanonicalPath, TooManyComponents) {
  // More than 60 components is now completely ok.
  CanonicalPath path(
      "a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/"
      "a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/"
      "a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/"
      "a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/"
      "a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/"
      "a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/"
      "a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/"
      "a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/"
      "a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/"
      "a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/"
      "a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/"
      "a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/"
      "a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/"
      "a/a/a/a/a/a/a/a/a/x/y.h");
  EXPECT_EQ(218, std::count(path.value().begin(), path.value().end(), '/'));
}
#endif  // !_WIN32

TEST(CanonicalPath, UpDir) {
  CanonicalPath path("../../foo/bar.h");
  EXPECT_EQ("../../foo/bar.h", path.value());

  path = CanonicalPath("test/../../foo/bar.h");
  EXPECT_EQ("../foo/bar.h", path.value());
}

TEST(CanonicalPath, AbsolutePath) {
  CanonicalPath path("/usr/include/stdio.h");
  EXPECT_EQ("/usr/include/stdio.h", path.value());
}
