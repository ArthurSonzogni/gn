// Copyright 2026 The GN Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <string_view>
#include <vector>

#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "gn/commands.h"
#include "gn/filesystem_utils.h"
#include "gn/input_file.h"
#include "gn/location.h"
#include "gn/setup.h"
#include "gn/standard_out.h"
#include "gn/target.h"
#include "gn/test_with_scope.h"
#include "util/test/test.h"

TEST(Suggest, ResolveModuleName) {
  TestWithScope setup_scope;
  SourceDir current_dir("//");
  Label default_toolchain(SourceDir("//toolchain/"), "default");
  Err err;

  Target target(setup_scope.settings(), Label(SourceDir("//foo/"), "bar"));
  target.set_module_name("my_module");

  std::vector<const Target*> all_targets = {&target};

  {
    auto [results, ok] = commands::ResolveSuggestionToTarget(
        setup_scope.build_settings(), all_targets, default_toolchain,
        "my_module");
    std::vector<std::pair<const Target*, commands::ApiScope>> expected = {
        {&target, commands::ApiScope::kPublic}};
    EXPECT_EQ(expected, results);
    EXPECT_TRUE(ok);
  }

  // Test resolving module name "my_module_Private"
  {
    auto [results, ok] = commands::ResolveSuggestionToTarget(
        setup_scope.build_settings(), all_targets, default_toolchain,
        "my_module_Private");
    std::vector<std::pair<const Target*, commands::ApiScope>> expected = {
        {&target, commands::ApiScope::kPrivate}};
    EXPECT_EQ(expected, results);
    EXPECT_TRUE(ok);
  }
}

TEST(Suggest, ResolveTargetName) {
  TestWithScope setup_scope;
  SourceDir current_dir("//");
  Label default_toolchain = setup_scope.toolchain()->label();
  Err err;

  Target target(
      setup_scope.settings(),
      Label(SourceDir("//"), "hello", setup_scope.toolchain()->label().dir(),
            setup_scope.toolchain()->label().name()));
  Target target_gcc(
      setup_scope.settings(),
      Label(SourceDir("//"), "hello", SourceDir("//build/toolchain/"), "gcc"));
  std::vector<const Target*> all_targets = {&target, &target_gcc};

  // Test resolving "//:hello"
  auto [results_label, ok_label] = commands::ResolveSuggestionToTarget(
      setup_scope.build_settings(), all_targets,
      setup_scope.toolchain()->label(), "//:hello");

  std::vector<std::pair<const Target*, commands::ApiScope>> expected_label = {
      {&target, commands::ApiScope::kPublic}};
  EXPECT_EQ(expected_label, results_label);
  EXPECT_TRUE(ok_label);

  // Test resolving "//:hello(//build/toolchain:gcc)"
  auto [results_toolchain, ok_toolchain] = commands::ResolveSuggestionToTarget(
      setup_scope.build_settings(), all_targets, default_toolchain,
      "//:hello(//build/toolchain:gcc)");

  std::vector<std::pair<const Target*, commands::ApiScope>> expected_toolchain =
      {{&target_gcc, commands::ApiScope::kPublic}};
  EXPECT_EQ(expected_toolchain, results_toolchain);
  EXPECT_TRUE(ok_toolchain);
}

TEST(Suggest, ResolveFileName) {
  TestWithScope setup_scope;
  SourceDir current_dir("//");
  Label default_toolchain = setup_scope.toolchain()->label();
  Label current_toolchain(SourceDir("//build/toolchain/"), "gcc");
  Label secondary_toolchain(SourceDir("//build/toolchain/"), "clang");
  Err err;

  // Follow standard practice to create temporary directories in tests.
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath root_dir = temp_dir.GetPath();
  setup_scope.build_settings()->SetRootPath(root_dir);

  base::WriteFile(root_dir.AppendASCII("public.h"), "", 0);
  base::WriteFile(root_dir.AppendASCII("private.h"), "", 0);
  base::WriteFile(root_dir.AppendASCII("implicit_public.h"), "", 0);
  base::WriteFile(root_dir.AppendASCII("no_target.h"), "", 0);
  base::WriteFile(root_dir.AppendASCII("simple.h"), "", 0);
  base::WriteFile(root_dir.AppendASCII("default_toolchain.h"), "", 0);
  base::WriteFile(root_dir.AppendASCII("secondary_toolchain.h"), "", 0);

  Target explicit_target(
      setup_scope.settings(),
      Label(SourceDir("//"), "explicit", current_toolchain.dir(),
            current_toolchain.name()));
  explicit_target.set_all_headers_public(false);
  explicit_target.sources().push_back(SourceFile("//private.h"));
  explicit_target.public_headers().push_back(SourceFile("//public.h"));
  explicit_target.public_headers().push_back(
      SourceFile("//nonexistent_file.h"));

  Target implicit_target(
      setup_scope.settings(),
      Label(SourceDir("//"), "implicit", default_toolchain.dir(),
            default_toolchain.name()));
  implicit_target.set_all_headers_public(true);
  implicit_target.sources().push_back(SourceFile("//implicit_public.h"));
  implicit_target.sources().push_back(SourceFile("//private.cc"));

  Target simple_default(
      setup_scope.settings(),
      Label(SourceDir("//"), "simple", default_toolchain.dir(),
            default_toolchain.name()));
  simple_default.public_headers().push_back(SourceFile("//public.h"));
  simple_default.public_headers().push_back(
      SourceFile("//default_toolchain.h"));

  Target simple_secondary(
      setup_scope.settings(),
      Label(SourceDir("//"), "simple", secondary_toolchain.dir(),
            secondary_toolchain.name()));
  simple_secondary.public_headers().push_back(SourceFile("//public.h"));
  simple_secondary.public_headers().push_back(
      SourceFile("//default_toolchain.h"));
  simple_secondary.public_headers().push_back(
      SourceFile("//secondary_toolchain.h"));

  std::vector<const Target*> all_targets = {&explicit_target, &implicit_target,
                                            &simple_default, &simple_secondary};

  {
    auto [results, ok] = commands::ResolveSuggestionToTarget(
        setup_scope.build_settings(), all_targets, current_toolchain,
        "//public.h");
    std::vector<std::pair<const Target*, commands::ApiScope>> expected = {
        {&explicit_target, commands::ApiScope::kPublic}};
    EXPECT_TRUE(ok);
    EXPECT_EQ(expected, results);
  }

  {
    auto [results, ok] = commands::ResolveSuggestionToTarget(
        setup_scope.build_settings(), all_targets, current_toolchain,
        "../../private.h");
    std::vector<std::pair<const Target*, commands::ApiScope>> expected = {
        {&explicit_target, commands::ApiScope::kPrivate}};
    EXPECT_TRUE(ok);
    EXPECT_EQ(expected, results);
  }

  {
    auto [results, ok] = commands::ResolveSuggestionToTarget(
        setup_scope.build_settings(), all_targets, current_toolchain,
        "//implicit_public.h");
    std::vector<std::pair<const Target*, commands::ApiScope>> expected = {
        {&implicit_target, commands::ApiScope::kPublic}};
    EXPECT_TRUE(ok);
    EXPECT_EQ(expected, results);
  }

  {
    auto [results, ok] = commands::ResolveSuggestionToTarget(
        setup_scope.build_settings(), all_targets, current_toolchain,
        "nonexistent_file.h");
    EXPECT_FALSE(ok);
  }

  {
    auto [results, ok] = commands::ResolveSuggestionToTarget(
        setup_scope.build_settings(), all_targets, current_toolchain,
        "//no_target.h");
    std::vector<std::pair<const Target*, commands::ApiScope>> expected_targets;
    EXPECT_TRUE(ok);
    EXPECT_EQ(expected_targets, results);
  }

  {
    auto [results, ok] = commands::ResolveSuggestionToTarget(
        setup_scope.build_settings(), all_targets, current_toolchain,
        "//default_toolchain.h");
    std::vector<std::pair<const Target*, commands::ApiScope>> expected_targets =
        {
            {&simple_secondary, commands::ApiScope::kPublic},
            {&simple_default, commands::ApiScope::kPublic},
        };
    EXPECT_TRUE(ok);
    EXPECT_EQ(expected_targets, results);
  }

  {
    auto [results, ok] = commands::ResolveSuggestionToTarget(
        setup_scope.build_settings(), all_targets, current_toolchain,
        "//secondary_toolchain.h");
    std::vector<std::pair<const Target*, commands::ApiScope>> expected_targets =
        {{{&simple_secondary, commands::ApiScope::kPublic}}};
    EXPECT_TRUE(ok);
    EXPECT_EQ(expected_targets, results);
  }
}

TEST(Suggest, OutputSuggestions) {
  TestWithScope setup_scope;
  Label default_toolchain = setup_scope.toolchain()->label();

  InputFile build_file(SourceFile("//BUILD.gn"));
  Location dummy_loc(&build_file, 1, 1);
  std::vector<const Target*> all_targets;

  auto set_visibility = [&](Target* target, std::string_view pattern) {
    Value visibility_value(nullptr, Value::LIST);
    visibility_value.list_value().push_back(
        Value(nullptr, std::string(pattern)));
    Err err;
    EXPECT_TRUE(
        target->visibility().Set(SourceDir("//"), "", visibility_value, &err));
  };

  auto create_target = [&](std::string_view name, Target::OutputType type,
                           auto fn) {
    auto target = std::make_unique<Target>(
        setup_scope.settings(),
        Label(SourceDir("//"), name, default_toolchain.dir(),
              default_toolchain.name()));
    target->set_output_type(type);
    target->SetToolchain(setup_scope.toolchain());
    target->set_user_friendly_location(dummy_loc);
    if (type == Target::SOURCE_SET) {
      Target::ModuleType module_type;
      module_type.set(Target::HAS_MODULEMAP);
      target->set_module_type(module_type);
      target->set_module_name(std::string(name));
      target->public_headers().push_back(
          SourceFile("//" + std::string(name) + ".h"));
    }
    fn(target.get());
    Err err;
    EXPECT_TRUE(target->OnResolvedWithoutChecks(&err));
    all_targets.push_back(target.get());
    return target;
  };

  auto includer = create_target("includer", Target::GROUP, [](Target*) {});

  auto run_suggest = [&](const Target& want) {
    std::string output;
    auto collect = [&](std::string_view s, TextDecoration, HtmlEscaping) {
      output.append(s);
    };
    commands::OutputSuggestions(all_targets, setup_scope.build_settings(),
                                default_toolchain, "//:includer",
                                want.module_name(), collect);
    return output;
  };

  auto visible = create_target("visible", Target::SOURCE_SET,
                               [&](Target* t) { t->visibility().SetPublic(); });
  auto visible_group =
      create_target("visible_group", Target::GROUP, [&](Target* t) {
        t->public_deps().push_back(LabelTargetPair(visible.get()));
        t->visibility().SetPublic();
      });
  // Prefer the real target over the group that exposes it.
  EXPECT_EQ(
      "Suggestion: Add public_deps = [ \":visible\" ] to :includer (defined at "
      "//BUILD.gn:1)\n",
      run_suggest(*visible));

  auto invisible =
      create_target("invisible", Target::SOURCE_SET, [&](Target* t) {});
  EXPECT_EQ(
      "Warning: //:invisible is not visible to //:includer\n"
      "Suggestion: Carefully consider whether you want to change the "
      "visibility so that you can depend on it\n"
      "Suggestion: Add public_deps = [ \":invisible\" ] to :includer (defined "
      "at //BUILD.gn:1)\n",
      run_suggest(*invisible));

  auto exposer_invisible =
      create_target("exposer_invisible", Target::GROUP, [&](Target* t) {
        t->private_deps().push_back(LabelTargetPair(invisible.get()));
      });
  EXPECT_EQ(
      "Warning: //:invisible is exposed via the following targets, but none "
      "are visible to //:includer\n"
      "Suggestion: Carefully consider whether you want to change the "
      "visibility so that you can depend on one of them\n"
      "Suggestion: Add one of the following to public_deps in :includer "
      "(defined at //BUILD.gn:1):\n"
      "* :exposer_invisible\n"
      "* :invisible\n",
      run_suggest(*invisible));

  auto exposer_visible =
      create_target("exposer_visible", Target::GROUP, [&](Target* t) {
        t->private_deps().push_back(LabelTargetPair(invisible.get()));
        t->visibility().SetPublic();
      });
  EXPECT_EQ(
      "Suggestion: Add public_deps = [ \":exposer_visible\" ] to :includer "
      "(defined at //BUILD.gn:1)\n",
      run_suggest(*invisible));

  auto exposer_visible2 =
      create_target("exposer_visible2", Target::GROUP, [&](Target* t) {
        t->private_deps().push_back(LabelTargetPair(exposer_visible.get()));
        t->visibility().SetPublic();
      });
  EXPECT_EQ(
      "Warning: //:invisible is exposed via multiple targets\n"
      "Suggestion: Clean up the visibility so that only one of the below "
      "targets is visible to //:includer\n"
      "Suggestion: Add one of the following to public_deps in :includer "
      "(defined at //BUILD.gn:1):\n"
      "* :exposer_visible\n"
      "* :exposer_visible2\n",
      run_suggest(*invisible));

  auto exposer_specific =
      create_target("exposer_specific", Target::GROUP, [&](Target* t) {
        t->private_deps().push_back(LabelTargetPair(exposer_visible.get()));
        set_visibility(t, "//:includer");
      });
  EXPECT_EQ(
      "Suggestion: Add public_deps = [ \":exposer_specific\" ] to :includer "
      "(defined at //BUILD.gn:1)\n",
      run_suggest(*invisible));

  auto cyclic = create_target("cyclic", Target::SOURCE_SET, [&](Target* t) {
    t->public_deps().push_back(LabelTargetPair(includer.get()));
    t->visibility().SetPublic();
  });
  EXPECT_EQ(
      "Warning: //:cyclic depends on //:includer, so adding this dependency "
      "will create a dependency loop:\n"
      "  //:includer ->\n"
      "  //:cyclic ->\n"
      "  //:includer\n"
      "Suggestion: Find the part of the dependency chain where there is no "
      "#include and remove that dependency.\n"
      "Suggestion: Add public_deps = [ \":cyclic\" ] to :includer (defined at "
      "//BUILD.gn:1)\n",
      run_suggest(*cyclic));

  auto cyclic_circular_includes = create_target(
      "cyclic_circular_includes", Target::STATIC_LIBRARY, [&](Target* t) {
        t->public_deps().push_back(LabelTargetPair(includer.get()));
        t->visibility().SetPublic();
        t->allow_circular_includes_from().insert(includer->label());
      });
  EXPECT_EQ(
      "Warning: //:cyclic_circular_includes depends on //:includer, so adding "
      "this "
      "dependency will create a dependency loop:\n"
      "  //:includer ->\n"
      "  //:cyclic_circular_includes ->\n"
      "  //:includer\n"
      "Suggestion: :cyclic_circular_includes (defined at //BUILD.gn:1) "
      "declares "
      "allow_circular_includes_from, which is bad style. Instead, you should "
      "remove allow_circular_includes_from by doing the following:\n"
      "source_set(\"cyclic_circular_includes_sources\") {\n"
      "  # All attributes from :cyclic_circular_includes except public_deps, "
      "and any link options\n"
      "  # Note that some public_deps may need to be added back based on "
      "#includes of headers.\n"
      "}\n"
      "\n"
      "static_library(\"cyclic_circular_includes\") {\n"
      "  public_deps = [ \":cyclic_circular_includes_sources\" ]\n"
      "  # public_deps, and any link variables from :cyclic_circular_includes\n"
      "}\n"
      "Suggestion: Add public_deps = [ \":cyclic_circular_includes_sources\" ] "
      "to "
      ":includer "
      "(defined at //BUILD.gn:1)\n",
      run_suggest(*cyclic_circular_includes));
}
