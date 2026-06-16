// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_OUTPUT_FILE_H_
#define TOOLS_GN_OUTPUT_FILE_H_

#include <stddef.h>

#include <string_view>
#include <vector>

class BuildSettings;
class SourceDir;
class SourceFile;

// A simple wrapper around a vector of chars that indicates the path
// relative to the output directory.
class OutputFile {
 public:
  OutputFile() = default;

  explicit OutputFile(std::string_view v) { value_.assign(v.begin(), v.end()); }

  OutputFile(const BuildSettings* build_settings,
             const SourceFile& source_file);

  std::string_view value() const {
    return std::string_view(value_.data(), value_.size());
  }

  void resize(std::size_t n) { value_.resize(n); }

  void append(std::string_view v) {
    value_.insert(value_.end(), v.begin(), v.end());
  }

  // Converts to a SourceFile by prepending the build directory to the file.
  // The *Dir version requires that the current OutputFile ends in a slash, and
  // the *File version is the opposite.
  SourceFile AsSourceFile(const BuildSettings* build_settings) const;
  SourceDir AsSourceDir(const BuildSettings* build_settings) const;

  bool operator==(const OutputFile& other) const = default;
  bool operator!=(const OutputFile& other) const = default;
  bool operator<(const OutputFile& other) const = default;
  std::strong_ordering operator<=>(const OutputFile& other) const = default;

 private:
  // Storing this as a vector<char> instead of a std::string has some tradeoffs.
  // * When OutputFile is moved (eg. vector<OutputFile>.push_back), string_views
  //   pointing to OutputFile stay valid
  // * We now lose small string optimization. This is probably fine, and may in
  //   fact even be an improvement, as OutputFiles are almost universally very
  //   long and thus this may help with branch prediction.
  std::vector<char> value_;
};

namespace std {

template <>
struct hash<OutputFile> {
  std::size_t operator()(const OutputFile& v) const {
    return hash<std::string_view>()(v.value());
  }
};

}  // namespace std

#endif  // TOOLS_GN_OUTPUT_FILE_H_
