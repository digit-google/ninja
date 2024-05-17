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

#include "elide_middle.h"

#include "test.h"

TEST(ElideMiddle, NothingToElide) {
  std::string input = "Nothing to elide in this short string.";
  EXPECT_EQ(input, ElideMiddle(input, 80));
  EXPECT_EQ(input, ElideMiddle(input, 38));
  EXPECT_EQ("", ElideMiddle(input, 0));
  EXPECT_EQ(".", ElideMiddle(input, 1));
  EXPECT_EQ("..", ElideMiddle(input, 2));
  EXPECT_EQ("...", ElideMiddle(input, 3));
}

TEST(ElideMiddle, ElideInTheMiddle) {
  std::string input = "01234567890123456789";
  EXPECT_EQ("...9", ElideMiddle(input, 4));
  EXPECT_EQ("0...9", ElideMiddle(input, 5));
  EXPECT_EQ("012...789", ElideMiddle(input, 9));
  EXPECT_EQ("012...6789", ElideMiddle(input, 10));
  EXPECT_EQ("0123...6789", ElideMiddle(input, 11));
  EXPECT_EQ("01234567...23456789", ElideMiddle(input, 19));
  EXPECT_EQ("01234567890123456789", ElideMiddle(input, 20));
}

TEST(ElideMiddle, ElideAnsiEscapeCodes) {
  std::string input = "012345\x1B[0;35m67890123456789";
  EXPECT_EQ("012...\x1B[0;35m6789", ElideMiddle(input, 10));
  EXPECT_EQ("012345\x1B[0;35m67...23456789", ElideMiddle(input, 19));

  EXPECT_EQ("Nothing \33[m string.", ElideMiddle("Nothing \33[m string.", 18));
  EXPECT_EQ("0\33[m12...6789", ElideMiddle("0\33[m1234567890123456789", 10));

  input = "abcd\x1b[1;31mefg\x1b[0mhlkmnopqrstuvwxyz";
  EXPECT_EQ("", ElideMiddle(input, 0));
  EXPECT_EQ(".", ElideMiddle(input, 1));
  EXPECT_EQ("..", ElideMiddle(input, 2));
  EXPECT_EQ("...", ElideMiddle(input, 3));
  EXPECT_EQ("...\x1B[1;31m\x1B[0mz", ElideMiddle(input, 4));
  EXPECT_EQ("a...\x1B[1;31m\x1B[0mz", ElideMiddle(input, 5));
  EXPECT_EQ("a...\x1B[1;31m\x1B[0myz", ElideMiddle(input, 6));
  EXPECT_EQ("ab...\x1B[1;31m\x1B[0myz", ElideMiddle(input, 7));
  EXPECT_EQ("ab...\x1B[1;31m\x1B[0mxyz", ElideMiddle(input, 8));
  EXPECT_EQ("abc...\x1B[1;31m\x1B[0mxyz", ElideMiddle(input, 9));
  EXPECT_EQ("abc...\x1B[1;31m\x1B[0mwxyz", ElideMiddle(input, 10));
  EXPECT_EQ("abcd\x1B[1;31m...\x1B[0mwxyz", ElideMiddle(input, 11));
  EXPECT_EQ("abcd\x1B[1;31m...\x1B[0mvwxyz", ElideMiddle(input, 12));

  EXPECT_EQ("abcd\x1B[1;31mef...\x1B[0muvwxyz", ElideMiddle(input, 15));
  EXPECT_EQ("abcd\x1B[1;31mef...\x1B[0mtuvwxyz", ElideMiddle(input, 16));
  EXPECT_EQ("abcd\x1B[1;31mefg\x1B[0m...tuvwxyz", ElideMiddle(input, 17));
  EXPECT_EQ("abcd\x1B[1;31mefg\x1B[0m...stuvwxyz", ElideMiddle(input, 18));
  EXPECT_EQ("abcd\x1B[1;31mefg\x1B[0mh...stuvwxyz", ElideMiddle(input, 19));

  input = "abcdef\x1b[31mA\x1b[0mBC";
  EXPECT_EQ("...\x1B[31m\x1B[0mC", ElideMiddle(input, 4));
  EXPECT_EQ("a...\x1B[31m\x1B[0mC", ElideMiddle(input, 5));
  EXPECT_EQ("a...\x1B[31m\x1B[0mBC", ElideMiddle(input, 6));
  EXPECT_EQ("ab...\x1B[31m\x1B[0mBC", ElideMiddle(input, 7));
  EXPECT_EQ("ab...\x1B[31mA\x1B[0mBC", ElideMiddle(input, 8));
  EXPECT_EQ("abcdef\x1b[31mA\x1b[0mBC", ElideMiddle(input, 9));
}
