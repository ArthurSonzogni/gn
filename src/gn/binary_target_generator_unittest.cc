// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gn/binary_target_generator.h"
#include "gn/err.h"
#include "gn/label_pattern.h"
#include "gn/scheduler.h"
#include "gn/target.h"
#include "gn/test_with_scheduler.h"
#include "gn/test_with_scope.h"
#include "util/test/test.h"

using BinaryTargetGeneratorTest = TestWithScheduler;

TEST_F(BinaryTargetGeneratorTest, NonModuleTarget) {
  TestWithScope setup;
  Scope::ItemVector items_;
  setup.scope()->set_item_collector(&items_);
  setup.scope()->set_source_dir(SourceDir("//test/"));

  TestParseInput input(
      R"(static_library("foo") {
           generate_modulemap = "textual"
           sources = [ "//foo.c" ]
         })");
  ASSERT_SUCCESS(input);

  Err err;
  input.parsed()->Execute(setup.scope(), &err);
  ASSERT_SUCCESS(err);

  ASSERT_EQ(1u, items_.size());
  Target* target = items_[0]->AsTarget();
  ASSERT_TRUE(target);

  EXPECT_TRUE(target->module_type().none());
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
  ASSERT_SUCCESS(input);

  Err err;
  input.parsed()->Execute(setup.scope(), &err);
  ASSERT_SUCCESS(err);

  ASSERT_EQ(1u, items_.size());
  Target* target = items_[0]->AsTarget();
  ASSERT_TRUE(target);

  EXPECT_TRUE(target->module_type().test(Target::HAS_MODULEMAP));
  EXPECT_TRUE(target->module_type().test(Target::MODULEMAP_IS_GENERATED));
  EXPECT_TRUE(target->module_type().test(Target::MODULEMAP_IS_TEXTUAL));
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
  ASSERT_SUCCESS(input);

  Err err;
  input.parsed()->Execute(setup.scope(), &err);
  ASSERT_SUCCESS(err);

  ASSERT_EQ(1u, items_.size());
  Target* target = items_[0]->AsTarget();
  ASSERT_TRUE(target);

  EXPECT_TRUE(target->module_type().test(Target::HAS_MODULEMAP));
  EXPECT_TRUE(target->module_type().test(Target::MODULEMAP_IS_GENERATED));
  EXPECT_TRUE(target->module_type().test(Target::MODULEMAP_IS_TEXTUAL));
}
TEST_F(BinaryTargetGeneratorTest, CheckIncludesStrict) {
  TestWithScope setup;
  Scope::ItemVector items_;
  setup.scope()->set_item_collector(&items_);
  setup.scope()->set_source_dir(SourceDir("//test/"));

  // Check default value is false.
  {
    TestParseInput input(
        R"(static_library("foo") {
             sources = [ "//foo.cc" ]
           })");
    ASSERT_SUCCESS(input);

    Err err;
    input.parsed()->Execute(setup.scope(), &err);
    ASSERT_SUCCESS(err);

    ASSERT_EQ(1u, items_.size());
    Target* target = items_[0]->AsTarget();
    ASSERT_TRUE(target);
    EXPECT_FALSE(target->check_includes_strict());
    items_.clear();
  }

  // Check overriding to true.
  {
    TestParseInput input(
        R"(static_library("bar") {
             sources = [ "//bar.cc" ]
             check_includes_strict = true
           })");
    ASSERT_SUCCESS(input);

    Err err;
    input.parsed()->Execute(setup.scope(), &err);
    ASSERT_SUCCESS(err);

    ASSERT_EQ(1u, items_.size());
    Target* target = items_[0]->AsTarget();
    ASSERT_TRUE(target);
    EXPECT_TRUE(target->check_includes_strict());
    items_.clear();
  }
}

TEST_F(BinaryTargetGeneratorTest, AllowCircularIncludesAllowlist) {
  TestWithScope setup;
  Scope::ItemVector items_;
  setup.scope()->set_item_collector(&items_);
  setup.scope()->set_source_dir(SourceDir("//test/"));

  // Create a dep for foo to reference.
  Target* dep =
      new Target(setup.settings(), Label(SourceDir("//test/"), "dep"));
  dep->set_output_type(Target::SOURCE_SET);
  dep->visibility().SetPublic();
  Err err_setup;
  dep->SetToolchain(setup.toolchain(), &err_setup);
  dep->OnResolved(&err_setup);
  items_.push_back(std::unique_ptr<Item>(dep));

  TestParseInput input(
      R"(static_library("foo") {
          sources = [ "//foo.cc" ]
          deps = [ ":dep" ]
          allow_circular_includes_from = [ ":dep" ]
        })");
  ASSERT_FALSE(input.has_error());

  // No allowlist -> allowed
  {
    Err err;
    input.parsed()->Execute(setup.scope(), &err);
    EXPECT_FALSE(err.has_error()) << err.message();
  }

  // In allowlist -> Allowed
  {
    auto allowlist = std::make_unique<std::vector<LabelPattern>>();
    allowlist->push_back(LabelPattern(LabelPattern::MATCH, SourceDir("//test/"),
                                      "foo", Label()));
    setup.build_settings()->set_allow_circular_includes_from_allowlist(
        std::move(allowlist));

    Err err;
    input.parsed()->Execute(setup.scope(), &err);
    EXPECT_FALSE(err.has_error()) << err.message();
  }

  // Pattern in allowlist -> Allowed
  {
    auto allowlist = std::make_unique<std::vector<LabelPattern>>();
    allowlist->push_back(LabelPattern(LabelPattern::DIRECTORY,
                                      SourceDir("//test/"), "", Label()));
    setup.build_settings()->set_allow_circular_includes_from_allowlist(
        std::move(allowlist));

    Err err;
    input.parsed()->Execute(setup.scope(), &err);
    EXPECT_FALSE(err.has_error()) << err.message();
  }

  // Not in allowlist -> Denied
  {
    auto allowlist = std::make_unique<std::vector<LabelPattern>>();
    allowlist->push_back(LabelPattern(LabelPattern::MATCH,
                                      SourceDir("//other/"), "other", Label()));
    setup.build_settings()->set_allow_circular_includes_from_allowlist(
        std::move(allowlist));

    Err err;
    input.parsed()->Execute(setup.scope(), &err);
    EXPECT_TRUE(err.has_error());
  }
}
