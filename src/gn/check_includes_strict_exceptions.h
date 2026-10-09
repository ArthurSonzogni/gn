// Copyright 2026 The GN Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_CHECK_INCLUDES_STRICT_EXCEPTIONS_H_
#define TOOLS_GN_CHECK_INCLUDES_STRICT_EXCEPTIONS_H_

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "gn/err.h"
#include "gn/label.h"
#include "gn/label_pattern.h"
#include "gn/source_dir.h"

class Value;

// CheckIncludesStrictExceptions stores allowlist rules for
// check_includes_strict. An includer LabelPattern can be allowed to include
// from any target (global) or from a specific target Label.
class CheckIncludesStrictExceptions {
 public:
  static Result<CheckIncludesStrictExceptions> FromValue(
      const Value& value,
      const SourceDir& current_dir,
      std::string_view source_root);

  void AddGlobalException(const LabelPattern& from) { global_.push_back(from); }
  void AddException(const LabelPattern& from, const Label& to) {
    specific_targets_[to.GetWithNoToolchain()].push_back(from);
  }

  // Checks whether `from` is allowed to include headers from `to`.
  bool CanInclude(const Label& from, const Label& to) const;

 private:
  std::vector<LabelPattern> global_;
  std::unordered_map<Label, std::vector<LabelPattern>> specific_targets_;
};

#endif  // TOOLS_GN_CHECK_INCLUDES_STRICT_EXCEPTIONS_H_
