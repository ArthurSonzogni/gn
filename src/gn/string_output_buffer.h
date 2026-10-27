// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_STRING_OUTPUT_BUFFER_H_
#define TOOLS_GN_STRING_OUTPUT_BUFFER_H_

#include <charconv>
#include <concepts>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace base {
class FilePath;
}  // namespace base

class Err;

inline size_t MaximumSize(std::string_view s) {
  return s.size();
}
inline void WriteToBuf(std::string_view s, char buf[], size_t& upto) {
  memcpy(&buf[upto], s.data(), s.size());
  upto += s.size();
}

inline size_t MaximumSize(char) {
  return 1;
}
inline void WriteToBuf(char c, char buf[], size_t& upto) {
  buf[upto++] = c;
}

template <typename T>
  requires(std::integral<T> && !std::same_as<T, char> && !std::same_as<T, bool>)
inline size_t MaximumSize(T) {
  // digits10 is the number of digits such that *every* number of this length
  // can be represented. For int32 for example, this returns the number of
  // digits required to store 999M (since we can store every number < 1B).
  // +1 because although we can't represent *every* number in the billions, we
  // can represent *some* numbers in the billions.
  // +1 for negative sign.
  return std::numeric_limits<T>::digits10 + 2;
}

template <typename T>
  requires(std::integral<T> && !std::same_as<T, char> && !std::same_as<T, bool>)
inline void WriteToBuf(T val, char buf[], size_t& upto) {
  upto =
      std::to_chars(&buf[upto], &buf[upto + MaximumSize(val)], val).ptr - buf;
}

template <typename T>
concept Writable = !std::same_as<std::remove_cvref_t<T>, bool> &&
                   requires(const T& val, char buf[], size_t& upto) {
                     { MaximumSize(val) } -> std::convertible_to<size_t>;
                     WriteToBuf(val, buf, upto);
                   };

// An append-only very large storage area for string data. Useful for the parts
// of GN that need to generate huge output files (e.g. --ide=json will create
// a 139 MiB project.json file for the Fuchsia build).
//
// Usage is the following:
//
//   1) Create instance.
//
//   2) Use operator<<, or Append() to append data to the instance.
//
//   3) Use ContentsEqual() to compare the instance's content with that of a
//      given file.
//
//   4) Use WriteToFile() to write the content to a given file.
//
class StringOutputBuffer {
 public:
  StringOutputBuffer() = default;

  // Reset the buffer contents to empty, while retaining allocated capacity.
  void Clear();

  // Convert content to single std::string instance. Useful for unit-testing.
  std::string str() const;

  // Return the number of characters stored in this instance.
  size_t size() const { return ro_size_ + rw_.size; }

  // Append a value to this instance.
  void Append(const Writable auto& value) {
    Reserve(MaximumSize(value));
    WriteToBuf(value, rw_.buf.get(), rw_.size);
  }
  void Append(const char* str, size_t len) {
    Append(std::string_view(str, len));
  }

  StringOutputBuffer& operator<<(const Writable auto& value) {
    Append(value);
    return *this;
  }

  // Compare the content of this instance with that of the file at |file_path|.
  bool ContentsEqual(const base::FilePath& file_path) const;

  // Write the contents of this instance to a file at |file_path|.
  bool WriteToFile(const base::FilePath& file_path, Err* err) const;

  // Write the contents of this instance to a file at |file_path| unless the
  // file already exists and the contents are equal.
  bool WriteToFileIfChanged(const base::FilePath& file_path, Err* err) const;

  static size_t GetPageSizeForTesting() { return kPageSize; }

 private:
  // Return the number of free bytes in the current page.
  size_t page_free_size() const { return rw_capacity_ - rw_.size; }

  // Ensures that the read-write section of the buffer has at least n bytes
  // available.
  void Reserve(size_t n);

  static constexpr size_t kPageSize = 65536;

  struct Page {
    std::unique_ptr<char[]> buf;
    size_t size = 0;
  };

  // Because this is an extremely hot path, we avoid pointer indirection by
  // storing the current page inline.
  // The current page is read-write.
  Page rw_;
  size_t rw_capacity_ = 0;

  // Completed read-only pages.
  std::vector<Page> ro_;
  // Contains sum([page.size for page in ro_])
  size_t ro_size_ = 0;
};

std::string Pretty(const StringOutputBuffer& buffer);

#endif  // TOOLS_GN_STRING_OUTPUT_BUFFER_H_
