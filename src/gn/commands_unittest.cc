// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gn/commands.h"

#include <stddef.h>

#include "base/command_line.h"
#include "base/values.h"
#include "gn/err.h"
#include "gn/label_pattern.h"
#include "gn/standard_out.h"
#include "gn/substitution_list.h"
#include "gn/target.h"
#include "gn/test_with_scheduler.h"
#include "gn/test_with_scope.h"
#include "util/test/test.h"

TEST(Commands, FilterOutMatch) {
  TestWithScope setup;
  SourceDir current_dir("//");

  Target target_afoo(setup.settings(), Label(SourceDir("//a/"), "foo"));
  Target target_cbar(setup.settings(), Label(SourceDir("//c/"), "bar"));
  std::vector<const Target*> targets{&target_afoo, &target_cbar};

  Err err;
  LabelPattern pattern_a = LabelPattern::GetPattern(
      current_dir, std::string_view(), Value(nullptr, "//a:*"), &err);
  EXPECT_SUCCESS(err);
  LabelPattern pattern_ef = LabelPattern::GetPattern(
      current_dir, std::string_view(), Value(nullptr, "//e:f"), &err);
  EXPECT_SUCCESS(err);
  std::vector<LabelPattern> label_patterns{pattern_a, pattern_ef};

  std::vector<const Target*> output;
  commands::FilterOutTargetsByPatterns(targets, label_patterns, &output);

  EXPECT_EQ(1, output.size());
  EXPECT_EQ(&target_cbar, output[0]);
}

TEST(Commands, ApplyTypeFilter) {
  TestWithScope setup;

  static const std::pair<std::string, Target::OutputType> cases[] = {
      {"action", Target::ACTION},
      {"bundle_data", Target::BUNDLE_DATA},
      {"copy", Target::COPY_FILES},
      {"create_bundle", Target::CREATE_BUNDLE},
      {"executable", Target::EXECUTABLE},
      {"generated_file", Target::GENERATED_FILE},
      {"group", Target::GROUP},
      {"loadable_module", Target::LOADABLE_MODULE},
      {"rust_library", Target::RUST_LIBRARY},
      {"rust_proc_macro", Target::RUST_PROC_MACRO},
      {"shared_library", Target::SHARED_LIBRARY},
      {"source_set", Target::SOURCE_SET},
      {"static_library", Target::STATIC_LIBRARY},
  };

  std::vector<std::unique_ptr<Target>> created_targets;
  std::vector<const Target*> all_targets;
  for (const auto& test_case : cases) {
    auto target = std::make_unique<Target>(
        setup.settings(), Label(SourceDir("//a/"), test_case.first));
    target->set_output_type(test_case.second);
    all_targets.push_back(target.get());
    created_targets.push_back(std::move(target));
  }

  // Prevent Err()  messages sent to stderr from polluting
  // the tests' own output.
  ScopedBufferedOutput buffered_output;

  for (const auto& test_case : cases) {
    std::vector<const Target*> targets_to_filter = all_targets;

    base::CommandLine cmdline(base::CommandLine::NO_PROGRAM);
    cmdline.AppendSwitch("type", test_case.first);

    commands::CommandSwitches::Init(cmdline);

    base::ListValue out;
    commands::FilterAndPrintTargets(&targets_to_filter, &out);

    ASSERT_EQ(1u, targets_to_filter.size());
    EXPECT_EQ(test_case.second, targets_to_filter[0]->output_type())
        << "Failed for type: " << test_case.first;

    commands::CommandSwitches empty_switches;
    commands::CommandSwitches::Set(empty_switches);
  }

  // Test multiple types separated by a comma.
  {
    std::vector<const Target*> targets_to_filter = all_targets;

    base::CommandLine cmdline(base::CommandLine::NO_PROGRAM);
    cmdline.AppendSwitch("type", "executable,rust_library");

    commands::CommandSwitches::Init(cmdline);

    base::ListValue out;
    commands::FilterAndPrintTargets(&targets_to_filter, &out);

    ASSERT_EQ(2u, targets_to_filter.size());
    EXPECT_TRUE(
        std::ranges::any_of(targets_to_filter, [](const Target* target) {
          return target->output_type() == Target::EXECUTABLE;
        }));
    EXPECT_TRUE(
        std::ranges::any_of(targets_to_filter, [](const Target* target) {
          return target->output_type() == Target::RUST_LIBRARY;
        }));

    commands::CommandSwitches empty_switches;
    commands::CommandSwitches::Set(empty_switches);
  }

  for (const auto& test_case : cases) {
    std::vector<const Target*> targets_to_filter = all_targets;

    base::CommandLine cmdline(base::CommandLine::NO_PROGRAM);
    cmdline.AppendSwitch("exclude-type", test_case.first);

    commands::CommandSwitches::Init(cmdline);

    base::ListValue out;
    commands::FilterAndPrintTargets(&targets_to_filter, &out);

    ASSERT_EQ(all_targets.size() - 1u, targets_to_filter.size())
        << "Failed for type: " << test_case.first;
    EXPECT_FALSE(std::ranges::any_of(targets_to_filter,
                                     [&test_case](const Target* target) {
                                       return target->output_type() ==
                                              test_case.second;
                                     }))
        << "Failed for type: " << test_case.first;

    commands::CommandSwitches empty_switches;
    commands::CommandSwitches::Set(empty_switches);
  }

  // Test multiple exclude types separated by a comma.
  {
    std::vector<const Target*> targets_to_filter = all_targets;

    base::CommandLine cmdline(base::CommandLine::NO_PROGRAM);
    cmdline.AppendSwitch("exclude-type", "executable,rust_library");

    commands::CommandSwitches::Init(cmdline);

    base::ListValue out;
    commands::FilterAndPrintTargets(&targets_to_filter, &out);

    ASSERT_EQ(all_targets.size() - 2u, targets_to_filter.size());
    EXPECT_FALSE(
        std::ranges::any_of(targets_to_filter, [](const Target* target) {
          return target->output_type() == Target::EXECUTABLE;
        }));
    EXPECT_FALSE(
        std::ranges::any_of(targets_to_filter, [](const Target* target) {
          return target->output_type() == Target::RUST_LIBRARY;
        }));

    commands::CommandSwitches empty_switches;
    commands::CommandSwitches::Set(empty_switches);
  }

  // Test that supplying both --type and --exclude-type fails.
  {
    base::CommandLine cmdline(base::CommandLine::NO_PROGRAM);
    cmdline.AppendSwitch("type", "executable");
    cmdline.AppendSwitch("exclude-type", "shared_library");

    EXPECT_FALSE(commands::CommandSwitches::Init(cmdline));
  }

  // Test invalid switch values for --type and --exclude-type.
  {
    base::CommandLine cmdline(base::CommandLine::NO_PROGRAM);
    cmdline.AppendSwitch("type", "invalid_type");
    EXPECT_FALSE(commands::CommandSwitches::Init(cmdline));
  }
  {
    base::CommandLine cmdline(base::CommandLine::NO_PROGRAM);
    cmdline.AppendSwitch("exclude-type", "invalid_type");
    EXPECT_FALSE(commands::CommandSwitches::Init(cmdline));
  }

  // Test Target::ACTION_FOREACH aliasing with --exclude-type=action.
  {
    auto action_foreach_target = std::make_unique<Target>(
        setup.settings(), Label(SourceDir("//a/"), "action_foreach_target"));
    action_foreach_target->set_output_type(Target::ACTION_FOREACH);
    std::vector<const Target*> targets_to_filter = all_targets;
    targets_to_filter.push_back(action_foreach_target.get());

    base::CommandLine cmdline(base::CommandLine::NO_PROGRAM);
    cmdline.AppendSwitch("exclude-type", "action");

    commands::CommandSwitches::Init(cmdline);

    base::ListValue out;
    commands::FilterAndPrintTargets(&targets_to_filter, &out);

    EXPECT_FALSE(
        std::ranges::any_of(targets_to_filter, [](const Target* target) {
          return target->output_type() == Target::ACTION ||
                 target->output_type() == Target::ACTION_FOREACH;
        }));

    commands::CommandSwitches empty_switches;
    commands::CommandSwitches::Set(empty_switches);
  }

  // Test combined --exclude-type and --testonly filtering.
  {
    std::vector<const Target*> targets_to_filter = all_targets;
    // Mark one target as testonly=true.
    created_targets[0]->set_testonly(true);

    base::CommandLine cmdline(base::CommandLine::NO_PROGRAM);
    cmdline.AppendSwitch("exclude-type", "shared_library");
    cmdline.AppendSwitch("testonly", "true");

    commands::CommandSwitches::Init(cmdline);

    base::ListValue out;
    commands::FilterAndPrintTargets(&targets_to_filter, &out);

    for (const Target* target : targets_to_filter) {
      EXPECT_TRUE(target->testonly());
      EXPECT_NE(target->output_type(), Target::SHARED_LIBRARY);
    }

    commands::CommandSwitches empty_switches;
    commands::CommandSwitches::Set(empty_switches);
  }
}

using CommandsGenTest = TestWithScheduler;

TEST_F(CommandsGenTest, InvalidGeneratedInputErrors) {
  TestWithScope setup;
  Err err;

  // Name the secondary toolchain directory so it sorts before
  // //toolchain:default lexicographically, verifying is_default takes priority.
  Toolchain alt_toolchain(setup.settings(), Label(SourceDir("//aaa/"), "alt"));
  TestWithScope::SetupToolchain(&alt_toolchain);
  Settings alt_settings(setup.build_settings(), "alt/");
  alt_settings.set_toolchain_label(alt_toolchain.label());
  alt_settings.set_default_toolchain_label(setup.toolchain()->label());

  TestTarget gen_alpha(setup, "//gen:alpha", Target::ACTION);
  gen_alpha.action_values().outputs() =
      SubstitutionList::MakeForTest("//out/Debug/z.cc");
  ASSERT_TRUE(gen_alpha.OnResolved(&err));

  TestTarget gen_beta(setup, "//gen:beta", Target::ACTION);
  gen_beta.action_values().outputs() =
      SubstitutionList::MakeForTest("//out/Debug/b.cc", "//out/Debug/a.cc");
  ASSERT_TRUE(gen_beta.OnResolved(&err));

  Target alt_gen(&alt_settings, Label(SourceDir("//gen/"), "alt_gen",
                                      alt_toolchain.label().dir(),
                                      alt_toolchain.label().name()));
  alt_gen.visibility().SetPublic();
  alt_gen.set_output_type(Target::ACTION);
  ASSERT_TRUE(alt_gen.SetToolchain(&alt_toolchain, &err));
  alt_gen.action_values().outputs() =
      SubstitutionList::MakeForTest("//out/Debug/alt/alt.cc");
  ASSERT_TRUE(alt_gen.OnResolved(&err));

  TestTarget single_missing(setup, "//a:single_missing", Target::SOURCE_SET);
  single_missing.sources().emplace_back("//out/Debug/z.cc");
  ASSERT_TRUE(single_missing.OnResolved(&err));

  TestTarget grouped_by_gen(setup, "//b:grouped_by_gen", Target::SOURCE_SET);
  grouped_by_gen.sources().emplace_back("//out/Debug/missing.cc");
  grouped_by_gen.sources().emplace_back("//out/Debug/b.cc");
  grouped_by_gen.sources().emplace_back("//out/Debug/z.cc");
  grouped_by_gen.sources().emplace_back("//out/Debug/a.cc");
  ASSERT_TRUE(grouped_by_gen.OnResolved(&err));

  Target multi_tc_alt(&alt_settings, Label(SourceDir("//e/"), "multi_tc",
                                           alt_toolchain.label().dir(),
                                           alt_toolchain.label().name()));
  multi_tc_alt.visibility().SetPublic();
  multi_tc_alt.set_output_type(Target::SOURCE_SET);
  ASSERT_TRUE(multi_tc_alt.SetToolchain(&alt_toolchain, &err));
  multi_tc_alt.sources().emplace_back("//out/Debug/alt/missing.cc");
  ASSERT_TRUE(multi_tc_alt.OnResolved(&err));

  TestTarget multi_tc_default(setup, "//e:multi_tc", Target::SOURCE_SET);
  multi_tc_default.sources().emplace_back("//out/Debug/alt/alt.cc");
  ASSERT_TRUE(multi_tc_default.OnResolved(&err));

  Target alt_only(&alt_settings, Label(SourceDir("//f/"), "alt_only",
                                       alt_toolchain.label().dir(),
                                       alt_toolchain.label().name()));
  alt_only.visibility().SetPublic();
  alt_only.set_output_type(Target::SOURCE_SET);
  ASSERT_TRUE(alt_only.SetToolchain(&alt_toolchain, &err));
  alt_only.sources().emplace_back("//out/Debug/alt/missing.cc");
  ASSERT_TRUE(alt_only.OnResolved(&err));

  // Pass targets out of order to verify deterministic target sorting.
  std::vector<Err> errors = commands::GetInvalidGeneratedInputErrors({
      &alt_only,
      &multi_tc_default,
      &multi_tc_alt,
      &grouped_by_gen,
      &single_missing,
      &alt_gen,
      &gen_beta,
      &gen_alpha,
  });
  ASSERT_EQ(4u, errors.size());

  // Single missing file uses singular phrasing.
  EXPECT_EQ("Input to target not generated by a dependency.",
            errors[0].message());
  EXPECT_EQ(
      "The target:\n"
      "  //a:single_missing\n"
      "has an input or source not generated by any dependencies:\n"
      "  //out/Debug/z.cc (generated by //gen:alpha)\n"
      "\n"
      "To fix this, run:\n"
      "  gn edit \"add deps //gen:alpha\" //a:single_missing",
      errors[0].help_text());

  // Files are grouped by generator target, sorted by path, with ungenerated
  // files last.
  EXPECT_EQ("Input to target not generated by a dependency.",
            errors[1].message());
  EXPECT_EQ(
      "The target:\n"
      "  //b:grouped_by_gen\n"
      "has inputs or sources not generated by any dependencies:\n"
      "  //out/Debug/z.cc (generated by //gen:alpha)\n"
      "  //out/Debug/a.cc (generated by //gen:beta)\n"
      "  //out/Debug/b.cc (generated by //gen:beta)\n"
      "  //out/Debug/missing.cc (not generated by any target)\n"
      "\n"
      "To fix this, run:\n"
      "  gn edit \"add deps //gen:alpha //gen:beta\" //b:grouped_by_gen",
      errors[1].help_text());

  // Toolchain deduplication prefers default toolchain and prints dep toolchain.
  EXPECT_EQ("Input to target not generated by a dependency.",
            errors[2].message());
  EXPECT_EQ(
      "The target:\n"
      "  //e:multi_tc\n"
      "has an input or source not generated by any dependencies:\n"
      "  //out/Debug/alt/alt.cc (generated by //gen:alt_gen(//aaa:alt))\n"
      "\n"
      "To fix this, run:\n"
      "  gn edit \"add deps //gen:alt_gen(//aaa:alt)\" //e:multi_tc",
      errors[2].help_text());

  // Non-default target with only ungenerated inputs omits gn edit instructions.
  EXPECT_EQ("Input to target not generated by a dependency.",
            errors[3].message());
  EXPECT_EQ(
      "The target:\n"
      "  //f:alt_only(//aaa:alt)\n"
      "has an input or source not generated by any dependencies:\n"
      "  //out/Debug/alt/missing.cc (not generated by any target)",
      errors[3].help_text());
}
