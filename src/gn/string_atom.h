// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_STRING_ATOM_H_
#define TOOLS_GN_STRING_ATOM_H_

#include <functional>
#include <string>
#include <string_view>

// A StringAtom models a pointer to a globally unique constant string.
//
// They are useful as key types for sets and map container types, especially
// when a program uses multiple instances that tend to use the same strings
// (as happen very frequently in GN).
//
// Note that default equality and hashing functions compare and hash the
// underlying string pointers, ensuring fast O(1) operations since every
// unique string is interned.
//
// By contrast, default ordering functions (operator< and std::less) compare
// the string content lexicographically, ensuring that standard ordered
// containers have deterministic, predictable ordering.
//
// In addition, _ordered_ containers support heterogeneous lookups (i.e.
// using an std::string_view, and by automatic conversion, a const char*
// of const char[] literal) as a key type.
//
// Additionally, it is also possible to implement very fast pointer-ordered
// containers using the StringAtom::PtrCompare struct on ordered containers,
// forcing containers to compare pointer values instead, for example:
//
//     // A fast set of unique strings, ordered by pointer.
//     using FastStringSet = std::set<StringAtom, StringAtom::PtrCompare>;
//
//     // A fast { string -> VALUE } map, ordered by pointer.
//     template <typename VALUE>
//     using FastStringMap = std::map<StringAtom, VALUE,
//     StringAtom::PtrCompare>;
//
class StringAtom {
 public:
  // Default constructor. Value points to a globally unique empty string.
  StringAtom();

  // Destructor should do nothing at all.
  ~StringAtom() = default;

  // Non-explicit constructors.
  StringAtom(std::string_view str) noexcept;

  // Copy and move operations.
  StringAtom(const StringAtom& other) noexcept : value_(other.value_) {}
  StringAtom& operator=(const StringAtom& other) noexcept {
    if (this != &other) {
      this->~StringAtom();  // really a no-op
      new (this) StringAtom(other);
    }
    return *this;
  }

  StringAtom(StringAtom&& other) noexcept : value_(other.value_) {}
  StringAtom& operator=(const StringAtom&& other) noexcept {
    if (this != &other) {
      this->~StringAtom();  // really a no-op
      new (this) StringAtom(std::move(other));
    }
    return *this;
  }

  bool empty() const { return value_.empty(); }

  // Explicit conversions.
  const std::string& str() const { return value_; }

  // Implicit conversions.
  operator std::string_view() const { return {value_}; }

  // Default comparison functions.
  // Returns true iff string_view(*this) == string_view(other)
  bool operator==(const StringAtom& other) const {
    // The string contents are equal iff they point to the same underlying
    // string.
    return &value_ == &other.value_;
  }

  bool operator!=(const StringAtom& other) const { return !(*this == other); }

  bool operator<(const StringAtom& other) const {
    // Avoid one unnecessary string comparison if values are equal.
    if (*this == other)
      return false;

    return value_ < other.value_;
  }

  // Pointer-based hash for fast O(1) hashing in unordered containers.
  size_t hash() const { return std::hash<const std::string*>()(&value_); }

  // Use the following struct to implement containers that order based on
  // the StringAtom pointer value for speed.
  //
  // E.g.:
  //    using FastSet = std::set<StringAtom, PtrCompare>;
  //    using FastMap = std::map<StringAtom, Value, PtrCompare>;
  //
  // IMPORTANT: Note that containers using PtrCompare are ordered based on the
  //            StringAtom pointer value, not the string content.
  //
  struct PtrCompare {
    bool operator()(const StringAtom& a, const StringAtom& b) const noexcept {
      return &a.value_ < &b.value_;
    }
  };

 protected:
  const std::string& value_;
};

namespace std {

// Ensure default heterogeneous lookups with other types like std::string_view.
template <>
struct less<StringAtom> {
  using is_transparent = int;

  bool operator()(const StringAtom& a, const StringAtom& b) const noexcept {
    return a.str() < b.str();
  }
  template <typename U>
  bool operator()(const StringAtom& a, const U& b) const noexcept {
    return a.str() < b;
  }
  template <typename U>
  bool operator()(const U& a, const StringAtom& b) const noexcept {
    return a < b.str();
  }
};

template <>
struct hash<StringAtom> {
  size_t operator()(const StringAtom& key) const noexcept { return key.hash(); }
};

}  // namespace std

#endif  // TOOLS_GN_STRING_ATOM_H_
