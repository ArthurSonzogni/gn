// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gn/binary_target_generator.h"
#include "gn/err.h"
#include "gn/scheduler.h"
#include "gn/target.h"
#include "gn/test_with_scheduler.h"
#include "gn/test_with_scope.h"
#include "util/test/test.h"

using BinaryTargetGeneratorTest = TestWithScheduler;

TEST_F(BinaryTargetGeneratorTest, UnnecessaryModuleMapAllPublic) {
  TestWithScope setup;
  Scope::ItemVector items_;
  setup.scope()->set_item_collector(&items_);
  setup.scope()->set_source_dir(SourceDir("//test/"));

  TestParseInput input(
      R"(static_library("foo") {
           generate_modulemap = "textual"
           sources = [ "//foo.cc" ]
         })");
  ASSERT_FALSE(input.has_error());

  Err err;
  input.parsed()->Execute(setup.scope(), &err);
  ASSERT_FALSE(err.has_error()) << err.message();

  ASSERT_EQ(1u, items_.size());
  Target* target = items_[0]->AsTarget();
  ASSERT_TRUE(target);

  EXPECT_EQ(Target::UNNECESSARY_MODULEMAP, target->module_type());
}

TEST_F(BinaryTargetGeneratorTest, GeneratedModuleMapAllPublic) {
  TestWithScope setup;
  Scope::ItemVector items_;
  setup.scope()->set_item_collector(&items_);
  setup.scope()->set_source_dir(SourceDir("//test/"));

  TestParseInput input(
      R"(static_library("foo") {
           generate_modulemap = "textual"
           sources = [ "//foo.cc", "//foo.h" ]
         })");
  ASSERT_FALSE(input.has_error());

  Err err;
  input.parsed()->Execute(setup.scope(), &err);
  ASSERT_FALSE(err.has_error()) << err.message();

  ASSERT_EQ(1u, items_.size());
  Target* target = items_[0]->AsTarget();
  ASSERT_TRUE(target);

  EXPECT_EQ(Target::GENERATED_TEXTUAL_MODULEMAP, target->module_type());
}

TEST_F(BinaryTargetGeneratorTest, GeneratedModuleMap) {
  TestWithScope setup;
  Scope::ItemVector items_;
  setup.scope()->set_item_collector(&items_);
  setup.scope()->set_source_dir(SourceDir("//test/"));

  TestParseInput input(
      R"(static_library("foo") {
           generate_modulemap = "textual"
           sources = [ "//foo.cc" ]
           public = ["//foo.h"]
         })");
  ASSERT_FALSE(input.has_error());

  Err err;
  input.parsed()->Execute(setup.scope(), &err);
  ASSERT_FALSE(err.has_error()) << err.message();

  ASSERT_EQ(1u, items_.size());
  Target* target = items_[0]->AsTarget();
  ASSERT_TRUE(target);

  EXPECT_EQ(Target::GENERATED_TEXTUAL_MODULEMAP, target->module_type());
}
