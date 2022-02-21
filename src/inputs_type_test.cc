// Copyright 2022 Google Inc. All Rights Reserved.
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

#include "inputs_type.h"

#include "test.h"

TEST(InputsType, Test) {
  InputsType type;
  EXPECT_EQ(InputsType::All, type.value);

  EXPECT_TRUE(type.ParseArg("all"));
  EXPECT_EQ(InputsType::All, type.value);

  EXPECT_TRUE(type.ParseArg("explicit"));
  EXPECT_EQ(InputsType::Explicit, type.value);

  EXPECT_TRUE(type.ParseArg("explicit-implicit"));
  EXPECT_EQ(InputsType::ExplicitImplicit, type.value);

  EXPECT_FALSE(type.ParseArg("unknown-type"));
  EXPECT_EQ(InputsType::ExplicitImplicit, type.value);
}

TEST(InputsType, GetTypeInfos) {
  int count = 0;
  const InputsType::TypeInfo* infos = InputsType::GetTypeInfos(&count);
  EXPECT_GT(count, 0);
  EXPECT_TRUE(infos);
}
