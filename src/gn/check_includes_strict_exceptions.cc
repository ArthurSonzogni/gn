// Copyright 2026 The GN Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gn/check_includes_strict_exceptions.h"

#include <string_view>

#include "gn/scope.h"
#include "gn/value.h"

bool CheckIncludesStrictExceptions::HasGlobalException(
    const Label& from) const {
  return LabelPattern::VectorMatches(global_, from);
}

bool CheckIncludesStrictExceptions::HasLocalException(const Label& from,
                                                      const Label& to) const {
  if (specific_targets_.empty())
    return false;
  auto it = specific_targets_.find(to.GetWithNoToolchain());
  if (it != specific_targets_.end())
    return LabelPattern::VectorMatches(it->second, from);

  return false;
}

bool CheckIncludesStrictExceptions::HasException(const Label& from,
                                                 const Label& to) const {
  return HasGlobalException(from) || HasLocalException(from, to);
}

Result<CheckIncludesStrictExceptions> CheckIncludesStrictExceptions::FromValue(
    const Value& value,
    const SourceDir& current_dir,
    std::string_view source_root) {
  RETURN_IF_ERROR(value.VerifyTypeIs(Value::LIST));

  CheckIncludesStrictExceptions exceptions;
  for (const auto& item : value.list_value()) {
    RETURN_IF_ERROR(item.VerifyTypeIs(Value::SCOPE));

    const Scope* scope = item.scope_value();
    const Value* includer = scope->GetValue("includer");
    if (!includer)
      return Err(item, "Exception rule requires \"includer\".");

    for (auto [key, val] : scope->GetCurrentScopeValues()) {
      if (key != "includer" && key != "included") {
        return Err(
            *val,
            "Unknown key in exception rule: \"" + std::string(key) + "\".",
            "Only \"includer\" and \"included\" are allowed.");
      }
    }

    Err err;
    LabelPattern from =
        LabelPattern::GetPattern(current_dir, source_root, *includer, &err);
    RETURN_IF_ERROR(err);

    if (!includer->string_value().starts_with("//"))
      return Err(*includer,
                 "Label pattern must be absolute (starting with '//').");

    const Value* included = scope->GetValue("included");
    if (included) {
      Label to =
          Label::Resolve(current_dir, source_root, Label(), *included, &err);
      RETURN_IF_ERROR(err);

      if (!included->string_value().starts_with("//"))
        return Err(*included,
                   "Target label must be absolute (starting with '//').");
      exceptions.AddLocalException(from, to);
    } else {
      exceptions.AddGlobalException(from);
    }
  }

  return exceptions;
}
