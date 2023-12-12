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

#ifndef NINJA_CANONICAL_PATH_
#define NINJA_CANONICAL_PATH_

#include <stdint.h>

#include <functional>
#include <string>

#include "string_piece.h"

/// A CanonicalPath an UTF-8 path used internally by Ninja to identify
/// targets in the build graph. Intermediate directory separators, '.' and
/// '..' path fragments are automatically removed when creating new instances.
///
/// On Win32, each backslash that appears in the canonical path representation
/// is also converted into a forward slash, but this operation is recorded into
/// a slash_bits() bit mask for the first 64-bit separators in the input.
/// This allows retrieving the original path
class CanonicalPath {
 public:
  /// Default constructor creates empty path.
  CanonicalPath() = default;

  /// Constructor takes input path and canonicalizes it before storing it
  /// in the instance. Use value() and slash_bits() to retrieve the result
  /// of canonicalization.
  ///
  /// This is non-explicit intentionally, to allow automatic conversion from
  /// std::string and C string literal values automatically.
  CanonicalPath(std::string&& path);
  CanonicalPath(const std::string& path);
  CanonicalPath(const char* path);

  static CanonicalPath MakeRaw(StringPiece str, uint64_t slash_bits);

  static CanonicalPath MakeFullBackwards(StringPiece str) {
    return MakeRaw(str, ~static_cast<uint64_t>(0));
  }

  static CanonicalPath MakeFullForwards(StringPiece str) {
    return MakeRaw(str, 0);
  }

  /// Return path value as UTF8-string. Always contains forward slashes on
  /// Win32.
  const std::string& value() const { return value_; }

  /// Return the non-canonical version of this path. For Posix, this is the
  /// same as value(), but for Win32, this is value() with forward slashes
  /// converted into backward slashes according to the value of slash_bits().
  std::string Decanonicalized() const;

  /// Return the bit mask used to record back-to-forward conversions that
  /// happened during construction. Only used for tests.
  uint64_t slash_bits() const {
#ifdef _WIN32
    return slash_bits_;
#else
    return 0;
#endif
  }

  /// Comparison & hashing operators.
  bool operator==(const CanonicalPath& other) const;

  bool operator!=(const CanonicalPath& other) const {
    return !(*this == other);
  }

  bool operator<(const CanonicalPath& other) const {
    return value() < other.value();
  }

  size_t hash() const { return std::hash<std::string>()(value_); }

 private:
  // Private constructor.
#ifdef _WIN32
  CanonicalPath(std::string&& value, uint64_t slash_bits)
      : value_(std::move(value)), slash_bits_(slash_bits) {}

  std::string value_;
  uint64_t slash_bits_ = 0;

#else   // !_WIN32
  CanonicalPath(std::string&& value, uint64_t) : value_(std::move(value)) {}

  std::string value_;
#endif  // !_WIN32
};

namespace std {
template <>
struct hash<CanonicalPath> {
  size_t operator()(const CanonicalPath& p) const { return p.hash(); }
};

}  // namespace std

#endif  // NINJA_CANONICAL_PATH_
