// Copyright 2026 The GN Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gn/check_includes_strict_exceptions.h"

#include <memory>
#include <string>
#include <string_view>

#include "gn/label.h"
#include "gn/scope.h"
#include "gn/source_dir.h"
#include "gn/value.h"
#include "util/test/test.h"

TEST(CheckIncludesStrictExceptionsTest, FromValue) {
  auto parse = [](const Value& val) {
    return CheckIncludesStrictExceptions::FromValue(val, SourceDir("//"), "");
  };

  auto make_rule = [](std::string_view from, std::string_view to) {
    auto scope = std::make_unique<Scope>(static_cast<const Settings*>(nullptr));
    scope->SetValue("includer", Value(nullptr, std::string(from)), nullptr);
    if (!to.empty())
      scope->SetValue("included", Value(nullptr, std::string(to)), nullptr);
    Value v(nullptr, Value::SCOPE);
    v.SetScopeValue(std::move(scope));
    return v;
  };

  EXPECT_FAILURE(parse(Value(nullptr, "not_a_list")));

  Value bad_list(nullptr, Value::LIST);
  bad_list.list_value().push_back(Value(nullptr, "not_a_scope"));
  EXPECT_FAILURE(parse(bad_list));

  Value non_abs(nullptr, Value::LIST);
  non_abs.list_value().push_back(make_rule("not_abs:a", "//b:b"));
  EXPECT_FAILURE(parse(non_abs));

  // Specific target exception.
  Value specific_list(nullptr, Value::LIST);
  specific_list.list_value().push_back(make_rule("//:includer", "//:included"));
  auto specific_exceptions = parse(specific_list);
  ASSERT_SUCCESS(specific_exceptions);
  EXPECT_TRUE(specific_exceptions->HasException(
      Label(SourceDir("//"), "includer"), Label(SourceDir("//"), "included")));
  EXPECT_FALSE(specific_exceptions->HasException(
      Label(SourceDir("//"), "includer"), Label(SourceDir("//"), "other")));

  // Global exception (included omitted).
  Value global_list(nullptr, Value::LIST);
  global_list.list_value().push_back(make_rule("//:includer", ""));
  auto global_exceptions = parse(global_list);
  ASSERT_SUCCESS(global_exceptions);
  EXPECT_TRUE(
      global_exceptions->HasException(Label(SourceDir("//"), "includer"),
                                      Label(SourceDir("//any/"), "target")));
  EXPECT_FALSE(
      global_exceptions->HasException(Label(SourceDir("//other/"), "target"),
                                      Label(SourceDir("//any/"), "target")));
}

TEST(CheckIncludesStrictExceptionsTest, HasException) {
  CheckIncludesStrictExceptions exceptions;

  exceptions.AddLocalException(
      LabelPattern(LabelPattern::RECURSIVE_DIRECTORY,
                   SourceDir("//includer_recursive/"), "", Label()),
      Label(SourceDir("//included/"), "target"));

  EXPECT_TRUE(exceptions.HasException(
      Label(SourceDir("//includer_recursive/sub/"), "target"),
      Label(SourceDir("//included/"), "target")));
  EXPECT_FALSE(
      exceptions.HasException(Label(SourceDir("//other_dir/"), "target"),
                              Label(SourceDir("//included/"), "target")));
  EXPECT_FALSE(exceptions.HasException(
      Label(SourceDir("//includer_recursive/sub/"), "target"),
      Label(SourceDir("//included/"), "other_target")));

  exceptions.AddGlobalException(LabelPattern(
      LabelPattern::DIRECTORY, SourceDir("//includer_global/"), "", Label()));

  EXPECT_TRUE(
      exceptions.HasException(Label(SourceDir("//includer_global/"), "foo"),
                              Label(SourceDir("//any_dir/"), "any_target")));
  EXPECT_FALSE(
      exceptions.HasException(Label(SourceDir("//other_dir/"), "foo"),
                              Label(SourceDir("//any_dir/"), "any_target")));
}
