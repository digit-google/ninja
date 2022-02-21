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

#include <cstring>

// static
const InputsType::TypeInfo* InputsType::GetTypeInfos(int* count) {
  static const TypeInfo kInfo[] = {
    { "all", InputsType::All,
      "all inputs: explicit, implicits and order-only" },
    { "explicit", InputsType::Explicit, "explicit inputs only" },
    { "explicit-implicit", InputsType::ExplicitImplicit,
      "explicit and implicit inputs only" },
  };
  *count = 3;
  return kInfo;
}

bool InputsType::ParseArg(const char* arg) {
  int count;
  const TypeInfo* infos = GetTypeInfos(&count);
  for (int n = 0; n < count; ++n) {
    if (!strcmp(infos[n].name, arg)) {
      value = infos[n].value;
      return true;
    }
  }
  return false;
}
