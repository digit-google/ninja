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

#include <assert.h>

#include "util.h"

#if 0
CanonicalPath::CanonicalPath(StringPiece path) : value_(path.AsString()) {
#ifdef _WIN32
  CanonicalizePath(&value_, &slash_bits_);
#else
  uint64_t ignored_bits;
  CanonicalizePath(&value_, &ignored_bits);
#endif
}
#endif

CanonicalPath::CanonicalPath(const std::string& path) : value_(path) {
#ifdef _WIN32
  CanonicalizePath(&value_, &slash_bits_);
#else
  uint64_t ignored_bits;
  CanonicalizePath(&value_, &ignored_bits);
#endif
}

CanonicalPath::CanonicalPath(std::string&& path) : value_(std::move(path)) {
#ifdef _WIN32
  CanonicalizePath(&value_, &slash_bits_);
#else
  uint64_t ignored_bits;
  CanonicalizePath(&value_, &ignored_bits);
#endif
}

CanonicalPath::CanonicalPath(const char* path) : value_(path) {
#ifdef _WIN32
  CanonicalizePath(&value_, &slash_bits_);
#else
  uint64_t ignored_bits;
  CanonicalizePath(&value_, &ignored_bits);
#endif
}

CanonicalPath CanonicalPath::MakeRaw(StringPiece str, uint64_t slash_bits) {
  return CanonicalPath(str.AsString(), slash_bits);
}

// static
std::string CanonicalPath::Decanonicalized() const {
  std::string result = value_;
#ifdef _WIN32
  uint64_t mask = 1;
  for (char* c = &result[0]; (c = strchr(c, '/')) != NULL;) {
    if (slash_bits_ & mask)
      *c = '\\';
    c++;
    mask <<= 1;
  }
#endif
  return result;
}
