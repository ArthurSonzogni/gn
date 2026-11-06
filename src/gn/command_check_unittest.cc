// Copyright 2026 The GN Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "gn/commands.h"
#include "gn/filesystem_utils.h"
#include "gn/setup.h"
#include "gn/source_file.h"
#include "gn/switches.h"
#include "gn/target.h"
#include "gn/test_with_scheduler.h"
#include "util/test/test.h"

namespace {

using CommandCheckTest = TestWithScheduler;

// //:a depends on //:b, which depends on //:d. //:a also has a data dep on
// //:c. //:unrelated is not reachable from //:a.
constexpr char kDotFile[] = R"(
buildconfig = "//BUILDCONFIG.gn"
no_check_targets = [ "//:d" ]
no_check_generated_targets = [ "//:b" ]
)";

constexpr char kBuildConfig[] = R"(
set_default_toolchain("//toolchain:default")
)";

constexpr char kToolchain[] = R"(
toolchain("default") {
  tool("cxx") {
    command = "cxx"
    outputs = [ "{{source_out_dir}}/{{source_file_part}}.o" ]
  }
  tool("link") {
    command = "link"
    outputs = [ "{{root_out_dir}}/{{target_output_name}}{{output_extension}}" ]
  }
  tool("stamp") {
    command = "stamp"
  }
}
)";

constexpr char kBuildFile[] = R"(
executable("a") {
  sources = [ "a.cc" ]
  deps = [ ":b" ]
  data_deps = [ ":c" ]
}
source_set("b") {
  sources = [ "b.h" ]
  deps = [ ":d" ]
}
source_set("c") {
  sources = [ "c.h" ]
}
source_set("d") {
  sources = [ "d.h" ]
}
source_set("unrelated") {
  sources = [ "unrelated.h" ]
}
)";

class TestProject {
 public:
  explicit TestProject(const char* dot_file = kDotFile) {
    EXPECT_TRUE(in_temp_dir_.CreateUniqueTempDir());
    EXPECT_TRUE(build_temp_dir_.CreateUniqueTempDir());
    base::FilePath in_path = base::MakeAbsoluteFilePath(in_temp_dir_.GetPath());
    base::FilePath build_path =
        base::MakeAbsoluteFilePath(build_temp_dir_.GetPath());

    const std::map<std::string, std::string> files = {
        {".gn", dot_file},
        {"BUILDCONFIG.gn", kBuildConfig},
        {"toolchain/BUILD.gn", kToolchain},
        {"BUILD.gn", kBuildFile},
    };
    for (const auto& [name, content] : files) {
      base::FilePath full_path = in_path.AppendASCII(name);
      base::CreateDirectory(full_path.DirName());
      WriteFile(full_path, content, nullptr);
    }

    base::CommandLine cmdline(base::CommandLine::NO_PROGRAM);
    cmdline.AppendSwitchPath(switches::kRoot, in_path);
    EXPECT_TRUE(setup_.DoSetup(FilePathToUTF8(build_path), true, cmdline));
    EXPECT_TRUE(setup_.Run());
  }

  Setup* setup() { return &setup_; }
  std::vector<const Target*> all_targets() {
    return setup_.builder().GetAllResolvedTargets();
  }

 private:
  base::ScopedTempDir in_temp_dir_;
  base::ScopedTempDir build_temp_dir_;
  Setup setup_;
};

std::set<std::string> Labels(const std::vector<const Target*>& targets) {
  std::set<std::string> labels;
  for (const Target* target : targets)
    labels.insert(target->label().GetUserVisibleName(false));
  return labels;
}

}  // namespace

TEST_F(CommandCheckTest, NoLabelPatterns) {
  TestProject project;
  std::optional<commands::TargetsToCheck> targets =
      commands::GetTargetsToCheck(project.setup(), project.all_targets(), {},
                                  /*with_transitive_deps=*/false,
                                  /*default_toolchain_only=*/false);
  ASSERT_TRUE(targets);

  EXPECT_EQ(
      (std::set<std::string>{"//:a", "//:b", "//:c", "//:d", "//:unrelated"}),
      Labels(targets->candidates));
  EXPECT_EQ((std::set<std::string>{"//:a", "//:b", "//:c", "//:unrelated"}),
            Labels(targets->to_check));
  EXPECT_EQ((std::set<std::string>{"//:b"}),
            Labels(targets->no_check_generated));
}

TEST_F(CommandCheckTest, LabelPatternsBypassDotFileLists) {
  TestProject project;
  std::optional<commands::TargetsToCheck> targets = commands::GetTargetsToCheck(
      project.setup(), project.all_targets(), {"//:b", "//:d"},
      /*with_transitive_deps=*/false,
      /*default_toolchain_only=*/false);
  ASSERT_TRUE(targets);

  EXPECT_EQ((std::set<std::string>{"//:b", "//:d"}),
            Labels(targets->candidates));
  EXPECT_EQ((std::set<std::string>{"//:b", "//:d"}), Labels(targets->to_check));
  EXPECT_TRUE(targets->no_check_generated.empty());
}

TEST_F(CommandCheckTest, NoLabelPatternsCheckTargets) {
  TestProject project(R"(
buildconfig = "//BUILDCONFIG.gn"
check_targets = [ "//:a", "//:c" ]
)");
  std::optional<commands::TargetsToCheck> targets =
      commands::GetTargetsToCheck(project.setup(), project.all_targets(), {},
                                  /*with_transitive_deps=*/false,
                                  /*default_toolchain_only=*/false);
  ASSERT_TRUE(targets);

  EXPECT_EQ(
      (std::set<std::string>{"//:a", "//:b", "//:c", "//:d", "//:unrelated"}),
      Labels(targets->candidates));
  EXPECT_EQ((std::set<std::string>{"//:a", "//:c"}), Labels(targets->to_check));
  EXPECT_TRUE(targets->no_check_generated.empty());
}

TEST_F(CommandCheckTest, WithTransitiveDeps) {
  TestProject project;
  std::optional<commands::TargetsToCheck> targets =
      commands::GetTargetsToCheck(project.setup(), project.all_targets(),
                                  {"//:a"}, /*with_transitive_deps=*/true,
                                  /*default_toolchain_only=*/false);
  ASSERT_TRUE(targets);

  EXPECT_EQ((std::set<std::string>{"//:a", "//:b", "//:c", "//:d"}),
            Labels(targets->candidates));
  EXPECT_EQ((std::set<std::string>{"//:a", "//:b", "//:c"}),
            Labels(targets->to_check));
  EXPECT_EQ((std::set<std::string>{"//:b"}),
            Labels(targets->no_check_generated));
}

TEST_F(CommandCheckTest, WithTransitiveDepsMultipleLabelPatterns) {
  TestProject project;
  std::optional<commands::TargetsToCheck> targets = commands::GetTargetsToCheck(
      project.setup(), project.all_targets(), {"//:c", "//:b"},
      /*with_transitive_deps=*/true,
      /*default_toolchain_only=*/false);
  ASSERT_TRUE(targets);

  EXPECT_EQ((std::set<std::string>{"//:b", "//:c", "//:d"}),
            Labels(targets->candidates));
  EXPECT_EQ((std::set<std::string>{"//:b", "//:c"}), Labels(targets->to_check));
  EXPECT_EQ((std::set<std::string>{"//:b"}),
            Labels(targets->no_check_generated));
}

TEST_F(CommandCheckTest, WithTransitiveDepsCheckTargets) {
  TestProject project(R"(
buildconfig = "//BUILDCONFIG.gn"
check_targets = [ "//:a", "//:c" ]
)");
  std::optional<commands::TargetsToCheck> targets =
      commands::GetTargetsToCheck(project.setup(), project.all_targets(),
                                  {"//:a"}, /*with_transitive_deps=*/true,
                                  /*default_toolchain_only=*/false);
  ASSERT_TRUE(targets);

  EXPECT_EQ((std::set<std::string>{"//:a", "//:b", "//:c", "//:d"}),
            Labels(targets->candidates));
  EXPECT_EQ((std::set<std::string>{"//:a", "//:c"}), Labels(targets->to_check));
  EXPECT_TRUE(targets->no_check_generated.empty());
}

TEST_F(CommandCheckTest, NoMatchingTargets) {
  TestProject project;
  EXPECT_FALSE(commands::GetTargetsToCheck(
      project.setup(), project.all_targets(), {"//nonexistent/*"},
      /*with_transitive_deps=*/true,
      /*default_toolchain_only=*/false));
}
