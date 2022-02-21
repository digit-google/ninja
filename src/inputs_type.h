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

#ifndef NINJA_INPUTS_TYPE_H_
#define NINJA_INPUTS_TYPE_H_

// A small struct representing a value for the --inputs-type option,
// used to select which inputs are printed by `-t inputs`.
//
// The All value prints all inputs (explicit + implicit + order-only)
// The Explicit value prints explicit inputs only.
// The ExplicitIMplicit value prints explicit and implicit inputs only.
struct InputsType {
  enum Type {
    All,
    Explicit,
    ExplicitImplicit,
  };

  InputsType() : value(All) {}

  Type value;

  // Parse |arg| for a vlid --inputs-type argument. On
  // success return true and set |value|. On failure return false.
  bool ParseArg(const char* arg);

  // Small struct describing the name and purpose of each supported
  // argument. Does not include 'list'.
  struct TypeInfo {
    const char* name;
    Type value;
    const char* help;
  };

  // Return the address of an array of TypeInfo items whichi describe
  // all supported argument strings, and their corresponding value,
  // plus some help text. Set |*count| to the size of the array.
  static const TypeInfo* GetTypeInfos(int* count);
};

#endif  // NINJA_INPUTS_TYPE_H_
