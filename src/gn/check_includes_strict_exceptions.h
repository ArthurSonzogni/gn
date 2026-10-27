// Copyright 2026 The GN Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_CHECK_INCLUDES_STRICT_EXCEPTIONS_H_
#define TOOLS_GN_CHECK_INCLUDES_STRICT_EXCEPTIONS_H_

#include <string_view>
#include <unordered_map>
#include <unordered_set>
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

  void AddGlobalException(const LabelPattern& from) {
    global_.push_back(from);
    includers_.insert(from);
  }
  void AddLocalException(const LabelPattern& from, const Label& to) {
    specific_targets_[to.GetWithNoToolchain()].push_back(from);
    includers_.insert(from);
  }

  // Checks whether `(from, to)` matches any local or global exception.
  bool HasException(const Label& from, const Label& to) const;

  // Checks whether `from` matches the includer of any local or global exception
  // rule.
  bool HasExceptionFrom(const Label& from) const;

  // Checks whether `from` matches any global exception rule.
  bool HasGlobalException(const Label& from) const;

  // Checks whether `(from, to)` matches any local exception rule.
  bool HasLocalException(const Label& from, const Label& to) const;

  // Returns true if there are no exception rules.
  bool empty() const { return includers_.empty(); }

 private:
  std::vector<LabelPattern> global_;
  std::unordered_map<Label, std::vector<LabelPattern>> specific_targets_;
  // All `from` patterns across global and local rules, indexed for fast lookup
  // in HasExceptionFrom().
  std::unordered_set<LabelPattern> includers_;
};

#endif  // TOOLS_GN_CHECK_INCLUDES_STRICT_EXCEPTIONS_H_
