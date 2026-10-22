// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gn/setup.h"

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "gn/builder_record.h"
#include "gn/commands.h"
#include "gn/filesystem_utils.h"
#include "gn/switches.h"
#include "gn/test_with_scheduler.h"
#include "util/build_config.h"
#include "util/msg_loop.h"

using SetupTest = TestWithScheduler;

static void WriteFile(const base::FilePath& file, const std::string& data) {
  CHECK_EQ(static_cast<int>(data.size()),  // Way smaller than INT_MAX.
           base::WriteFile(file, data.data(), data.size()));
}

static std::string ReadFile(const base::FilePath& file) {
  std::string data;
  CHECK(base::ReadFileToString(file, &data));
  return data;
}

TEST_F(SetupTest, DotGNFileIsGenDep) {
  base::CommandLine cmdline(base::CommandLine::NO_PROGRAM);

  // Create a temp directory containing a .gn file and a BUILDCONFIG.gn file,
  // pass it as --root.
  base::ScopedTempDir in_temp_dir;
  ASSERT_TRUE(in_temp_dir.CreateUniqueTempDir());
  base::FilePath in_path = base::MakeAbsoluteFilePath(in_temp_dir.GetPath());
  base::FilePath dot_gn_name = in_path.Append(FILE_PATH_LITERAL(".gn"));
  WriteFile(dot_gn_name, "buildconfig = \"//BUILDCONFIG.gn\"\n");
  WriteFile(in_path.Append(FILE_PATH_LITERAL("BUILDCONFIG.gn")), "");
  cmdline.AppendSwitchPath(switches::kRoot, in_path);

  // Create another temp dir for writing the generated files to.
  base::ScopedTempDir build_temp_dir;
  ASSERT_TRUE(build_temp_dir.CreateUniqueTempDir());

  // Run setup and check that the .gn file is in the scheduler's gen deps.
  Setup setup;
  EXPECT_TRUE(
      setup.DoSetup(FilePathToUTF8(build_temp_dir.GetPath()), true, cmdline));
  std::vector<base::FilePath> gen_deps = g_scheduler->GetGenDependencies();
  ASSERT_EQ(1u, gen_deps.size());
  EXPECT_EQ(gen_deps[0], base::MakeAbsoluteFilePath(dot_gn_name));
}

TEST_F(SetupTest, EmptyScriptExecutableDoesNotGenerateError) {
  base::CommandLine cmdline(base::CommandLine::NO_PROGRAM);

  const char kDotfileContents[] = R"(
buildconfig = "//BUILDCONFIG.gn"
script_executable = ""
)";

  // Create a temp directory containing a .gn file and a BUILDCONFIG.gn file,
  // pass it as --root.
  base::ScopedTempDir in_temp_dir;
  ASSERT_TRUE(in_temp_dir.CreateUniqueTempDir());
  base::FilePath in_path = base::MakeAbsoluteFilePath(in_temp_dir.GetPath());
  base::FilePath dot_gn_name = in_path.Append(FILE_PATH_LITERAL(".gn"));
  WriteFile(dot_gn_name, kDotfileContents);

  WriteFile(in_path.Append(FILE_PATH_LITERAL("BUILDCONFIG.gn")), "");
  cmdline.AppendSwitchPath(switches::kRoot, in_path);

  // Create another temp dir for writing the generated files to.
  base::ScopedTempDir build_temp_dir;
  ASSERT_TRUE(build_temp_dir.CreateUniqueTempDir());

  // Run setup and check that the .gn file is in the scheduler's gen deps.
  Setup setup;
  Err err;
  EXPECT_TRUE(setup.DoSetupWithErr(FilePathToUTF8(build_temp_dir.GetPath()),
                                   true, cmdline, &err));
}

#if defined(OS_WIN)
TEST_F(SetupTest, MissingScriptExeGeneratesSetupErrorOnWindows) {
  base::CommandLine cmdline(base::CommandLine::NO_PROGRAM);

  const char kDotfileContents[] = R"(
buildconfig = "//BUILDCONFIG.gn"
script_executable = "this_does_not_exist"
)";

  // Create a temp directory containing a .gn file and a BUILDCONFIG.gn file,
  // pass it as --root.
  base::ScopedTempDir in_temp_dir;
  ASSERT_TRUE(in_temp_dir.CreateUniqueTempDir());
  base::FilePath in_path = base::MakeAbsoluteFilePath(in_temp_dir.GetPath());
  base::FilePath dot_gn_name = in_path.Append(FILE_PATH_LITERAL(".gn"));
  WriteFile(dot_gn_name, kDotfileContents);

  WriteFile(in_path.Append(FILE_PATH_LITERAL("BUILDCONFIG.gn")), "");
  cmdline.AppendSwitchPath(switches::kRoot, in_path);

  // Create another temp dir for writing the generated files to.
  base::ScopedTempDir build_temp_dir;
  ASSERT_TRUE(build_temp_dir.CreateUniqueTempDir());

  // Run setup and check that the .gn file is in the scheduler's gen deps.
  Setup setup;
  Err err;
  EXPECT_FALSE(setup.DoSetupWithErr(FilePathToUTF8(build_temp_dir.GetPath()),
                                    true, cmdline, &err));
  EXPECT_TRUE(err.has_error());
}
#endif  // defined(OS_WIN)

static void RunExtensionCheckTest(std::string extension,
                                  bool success,
                                  const std::string& expected_error_message) {
  base::CommandLine cmdline(base::CommandLine::NO_PROGRAM);

  // Create a temp directory containing a .gn file and a BUILDCONFIG.gn file,
  // pass it as --root.
  base::ScopedTempDir in_temp_dir;
  ASSERT_TRUE(in_temp_dir.CreateUniqueTempDir());
  base::FilePath in_path = base::MakeAbsoluteFilePath(in_temp_dir.GetPath());
  base::FilePath dot_gn_name = in_path.Append(FILE_PATH_LITERAL(".gn"));
  WriteFile(dot_gn_name,
            "buildconfig = \"//BUILDCONFIG.gn\"\n\
      build_file_extension = \"" +
                extension + "\"");
  WriteFile(in_path.Append(FILE_PATH_LITERAL("BUILDCONFIG.gn")), "");
  cmdline.AppendSwitchPath(switches::kRoot, in_path);

  // Create another temp dir for writing the generated files to.
  base::ScopedTempDir build_temp_dir;
  ASSERT_TRUE(build_temp_dir.CreateUniqueTempDir());

  // Run setup and check that its status.
  Setup setup;
  Err err;
  EXPECT_EQ(success,
            setup.DoSetupWithErr(FilePathToUTF8(build_temp_dir.GetPath()), true,
                                 cmdline, &err));
  EXPECT_EQ(success, !err.has_error());
}

TEST_F(SetupTest, NoSeparatorInExtension) {
  RunExtensionCheckTest(
      "hello" + std::string(1, base::FilePath::kSeparators[0]) + "world", false,
#if defined(OS_WIN)
      "Build file extension 'hello\\world' cannot contain a path separator"
#else
      "Build file extension 'hello/world' cannot contain a path separator"
#endif
  );
}

TEST_F(SetupTest, Extension) {
  RunExtensionCheckTest("yay", true, "");
}

TEST_F(SetupTest, AddExportCompileCommands) {
  base::CommandLine cmdline(base::CommandLine::NO_PROGRAM);

  // Provide a project default export compile command list.
  const char kDotfileContents[] = R"(
buildconfig = "//BUILDCONFIG.gn"
export_compile_commands = [ "//base/*" ]
)";

  // Create a temp directory containing the build.
  base::ScopedTempDir in_temp_dir;
  ASSERT_TRUE(in_temp_dir.CreateUniqueTempDir());
  base::FilePath in_path = base::MakeAbsoluteFilePath(in_temp_dir.GetPath());
  base::FilePath dot_gn_name = in_path.Append(FILE_PATH_LITERAL(".gn"));
  WriteFile(dot_gn_name, kDotfileContents);

  WriteFile(in_path.Append(FILE_PATH_LITERAL("BUILDCONFIG.gn")), "");
  cmdline.AppendSwitch(switches::kRoot, FilePathToUTF8(in_path));

  // Two additions to the compile commands list.
  cmdline.AppendSwitch(switches::kAddExportCompileCommands,
                       "//tools:doom_melon");
  cmdline.AppendSwitch(switches::kAddExportCompileCommands, "//src/gn:*");

  // Create another temp dir for writing the generated files to.
  base::ScopedTempDir build_temp_dir;
  ASSERT_TRUE(build_temp_dir.CreateUniqueTempDir());

  // Run setup and check that the .gn file is in the scheduler's gen deps.
  Setup setup;
  Err err;
  EXPECT_TRUE(setup.DoSetupWithErr(FilePathToUTF8(build_temp_dir.GetPath()),
                                   true, cmdline, &err));

  // The export compile commands should have three items.
  const std::vector<LabelPattern>& export_cc = setup.export_compile_commands();
  ASSERT_EQ(3u, export_cc.size());
  EXPECT_EQ("//base/*", export_cc[0].Describe());
  EXPECT_EQ("//tools:doom_melon", export_cc[1].Describe());
  EXPECT_EQ("//src/gn:*", export_cc[2].Describe());
}

TEST_F(SetupTest, NoCheckGeneratedTargets) {
  base::CommandLine cmdline(base::CommandLine::NO_PROGRAM);

  // no_check_generated_targets can be combined with no_check_targets.
  const char kDotfileContents[] = R"(
buildconfig = "//BUILDCONFIG.gn"
no_check_targets = [ "//third_party/*" ]
no_check_generated_targets = [
  "//base:base",
  "//foo/*",
]
)";

  // Create a temp directory containing the build.
  base::ScopedTempDir in_temp_dir;
  ASSERT_TRUE(in_temp_dir.CreateUniqueTempDir());
  base::FilePath in_path = base::MakeAbsoluteFilePath(in_temp_dir.GetPath());
  WriteFile(in_path.Append(FILE_PATH_LITERAL(".gn")), kDotfileContents);
  WriteFile(in_path.Append(FILE_PATH_LITERAL("BUILDCONFIG.gn")), "");
  cmdline.AppendSwitch(switches::kRoot, FilePathToUTF8(in_path));

  // Create another temp dir for writing the generated files to.
  base::ScopedTempDir build_temp_dir;
  ASSERT_TRUE(build_temp_dir.CreateUniqueTempDir());

  Setup setup;
  Err err;
  EXPECT_TRUE(setup.DoSetupWithErr(FilePathToUTF8(build_temp_dir.GetPath()),
                                   true, cmdline, &err));

  ASSERT_TRUE(setup.no_check_patterns());
  EXPECT_EQ(1u, setup.no_check_patterns()->size());
  const std::vector<LabelPattern>& patterns =
      setup.no_check_generated_patterns();
  ASSERT_EQ(2u, patterns.size());
  EXPECT_EQ("//base:base", patterns[0].Describe());
  EXPECT_EQ("//foo/*", patterns[1].Describe());
}

TEST_F(SetupTest, RootPatternsInGnConfig) {
  base::CommandLine cmdline(base::CommandLine::NO_PROGRAM);

  // Provide a default root pattern for all top-level targets from //BUILD.gn
  const char kDotfileContents[] = R"(
buildconfig = "//BUILDCONFIG.gn"
root_patterns = [ "//:*" ]
)";

  // Create a temp directory containing the build.
  base::ScopedTempDir in_temp_dir;
  ASSERT_TRUE(in_temp_dir.CreateUniqueTempDir());
  base::FilePath in_path = base::MakeAbsoluteFilePath(in_temp_dir.GetPath());

  WriteFile(in_path.Append(FILE_PATH_LITERAL("BUILDCONFIG.gn")), "");
  WriteFile(in_path.Append(FILE_PATH_LITERAL(".gn")), kDotfileContents);

  cmdline.AppendSwitch(switches::kRoot, FilePathToUTF8(in_path));

  // Create another temp dir for writing the generated files to.
  base::ScopedTempDir build_temp_dir;
  ASSERT_TRUE(build_temp_dir.CreateUniqueTempDir());

  // Run setup and check that the .gn file is in the scheduler's gen deps.
  Setup setup;
  Err err;
  EXPECT_TRUE(setup.DoSetupWithErr(FilePathToUTF8(build_temp_dir.GetPath()),
                                   true, cmdline, &err));

  const std::vector<LabelPattern>& root_patterns =
      setup.build_settings().root_patterns();
  ASSERT_EQ(1u, root_patterns.size());
  EXPECT_EQ("//.:*", root_patterns[0].Describe());
}

TEST_F(SetupTest, RootPatternsOnCommandLineOverrideGnConfig) {
  base::CommandLine cmdline(base::CommandLine::NO_PROGRAM);

  // Provide a default root pattern for only //:foo
  const char kDotfileContents[] = R"(
buildconfig = "//BUILDCONFIG.gn"
root_patterns = [ "//:foo" ]
)";

  // Create a temp directory containing the build.
  base::ScopedTempDir in_temp_dir;
  ASSERT_TRUE(in_temp_dir.CreateUniqueTempDir());
  base::FilePath in_path = base::MakeAbsoluteFilePath(in_temp_dir.GetPath());

  WriteFile(in_path.Append(FILE_PATH_LITERAL("BUILDCONFIG.gn")), "");
  WriteFile(in_path.Append(FILE_PATH_LITERAL(".gn")), kDotfileContents);

  cmdline.AppendSwitch(switches::kRoot, FilePathToUTF8(in_path));

  // Override the default root pattern list.
  cmdline.AppendSwitch(switches::kRootPattern, "//:bar");
  cmdline.AppendSwitch(switches::kRootPattern, "//:qux");

  // Create another temp dir for writing the generated files to.
  base::ScopedTempDir build_temp_dir;
  ASSERT_TRUE(build_temp_dir.CreateUniqueTempDir());

  // Run setup and check that the .gn file is in the scheduler's gen deps.
  Setup setup;
  Err err;
  EXPECT_TRUE(setup.DoSetupWithErr(FilePathToUTF8(build_temp_dir.GetPath()),
                                   true, cmdline, &err));

  const std::vector<LabelPattern>& root_patterns =
      setup.build_settings().root_patterns();
  ASSERT_EQ(2u, root_patterns.size());
  EXPECT_EQ("//.:bar", root_patterns[0].Describe());
  EXPECT_EQ("//.:qux", root_patterns[1].Describe());
}

TEST_F(SetupTest, RootPatternsFiltersPatterns) {
  base::CommandLine cmdline(base::CommandLine::NO_PROGRAM);

  const char kDotfileContents[] = R"(
buildconfig = "//BUILDCONFIG.gn"
root_patterns = [ "//:foo" ]
)";

  const char kBuildConfigContents[] = R"(
set_default_toolchain("//:toolchain")
)";

  const char kBuildGnContents[] = R"(
group("foo") {
  deps = [ ":bar" ]
}

group("bar") {
}

group("zoo") {
}

group("qux") {
}

# Minimal default toolchain definition for this test. Non-functional.
toolchain("toolchain") {
  tool("stamp") {
    command = "stamp"
  }
}
)";

  // Create a temp directory containing the build.
  base::ScopedTempDir in_temp_dir;
  ASSERT_TRUE(in_temp_dir.CreateUniqueTempDir());
  base::FilePath in_path = base::MakeAbsoluteFilePath(in_temp_dir.GetPath());

  WriteFile(in_path.Append(FILE_PATH_LITERAL("BUILD.gn")), kBuildGnContents);
  WriteFile(in_path.Append(FILE_PATH_LITERAL("BUILDCONFIG.gn")),
            kBuildConfigContents);
  WriteFile(in_path.Append(FILE_PATH_LITERAL(".gn")), kDotfileContents);

  cmdline.AppendSwitch(switches::kRoot, FilePathToUTF8(in_path));

  // Create another temp dir for writing the generated files to.
  base::ScopedTempDir build_temp_dir;
  ASSERT_TRUE(build_temp_dir.CreateUniqueTempDir());

  // Run setup and check that the .gn file is in the scheduler's gen deps.
  Setup setup;
  Err err;
  EXPECT_TRUE(setup.DoSetupWithErr(FilePathToUTF8(build_temp_dir.GetPath()),
                                   true, cmdline, &err));

  const std::vector<LabelPattern>& root_patterns =
      setup.build_settings().root_patterns();
  ASSERT_EQ(1u, root_patterns.size());
  EXPECT_EQ("//.:foo", root_patterns[0].Describe());

  // Now build the graph, then verify it only includes //:foo and //:bar
  ASSERT_TRUE(setup.Run(cmdline));

  SourceDir top_dir("//");

  const BuilderRecord* foo_record =
      setup.builder().GetRecord(Label(top_dir, "foo", top_dir, "toolchain"));
  const BuilderRecord* bar_record =
      setup.builder().GetRecord(Label(top_dir, "bar", top_dir, "toolchain"));
  const BuilderRecord* qux_record =
      setup.builder().GetRecord(Label(top_dir, "qux", top_dir, "toolchain"));
  const BuilderRecord* zoo_record =
      setup.builder().GetRecord(Label(top_dir, "zoo", top_dir, "toolchain"));

  // All four targets were added as build graph records.
  ASSERT_TRUE(foo_record);
  ASSERT_TRUE(bar_record);
  ASSERT_TRUE(zoo_record);
  ASSERT_TRUE(qux_record);

  // But only foo and bar should be generated in the Ninja plan.
  EXPECT_TRUE(foo_record->should_generate());
  EXPECT_TRUE(bar_record->should_generate());
  EXPECT_FALSE(qux_record->should_generate());
  EXPECT_FALSE(zoo_record->should_generate());
}

TEST_F(SetupTest, ArgsGnRelativeAndAbsoluteImports) {
  base::CommandLine cmdline(base::CommandLine::NO_PROGRAM);

  const char kDotfileContents[] = R"(
    buildconfig = "//BUILDCONFIG.gn"
    script_executable = ""
  )";

  // Create a temp directory containing a .gn file and a BUILDCONFIG.gn file,
  // pass it as --root.
  base::ScopedTempDir in_temp_dir;
  ASSERT_TRUE(in_temp_dir.CreateUniqueTempDir());
  base::FilePath in_path = base::MakeAbsoluteFilePath(in_temp_dir.GetPath());
  base::FilePath dot_gn_name = in_path.Append(FILE_PATH_LITERAL(".gn"));
  WriteFile(dot_gn_name, kDotfileContents);

  WriteFile(in_path.Append(FILE_PATH_LITERAL("BUILDCONFIG.gn")), "");
  cmdline.AppendSwitchPath(switches::kRoot, in_path);

  WriteFile(in_path.Append(FILE_PATH_LITERAL("build_defines.gni")),
            "variable1 = true");

  // Create another temp dir and write the args.gn with imports.
  base::ScopedTempDir build_temp_dir;
  ASSERT_TRUE(build_temp_dir.CreateUniqueTempDir());
  WriteFile(build_temp_dir.GetPath().Append(FILE_PATH_LITERAL("args.gn")), R"(
    import("//build_defines.gni")
    import("params.gni")
  )");
  WriteFile(build_temp_dir.GetPath().Append(FILE_PATH_LITERAL("params.gni")),
            "variable2 = true");

  // Run setup and check that the args.gn imports are in dependency files.
  Setup setup;
  Err err;
  EXPECT_TRUE(setup.DoSetupWithErr(FilePathToUTF8(build_temp_dir.GetPath()),
                                   true, cmdline, &err));

  const auto& dependency_files =
      setup.build_settings().build_args().build_args_dependency_files();
  ASSERT_EQ(2u, dependency_files.size());
  const auto dependency_includes_file = [&](std::string_view file_name) {
    return std::any_of(
        dependency_files.begin(), dependency_files.end(),
        [&](const SourceFile& file) { return file.GetName() == file_name; });
  };
  EXPECT_TRUE(dependency_includes_file("build_defines.gni"));
  EXPECT_TRUE(dependency_includes_file("params.gni"));
}

TEST_F(SetupTest, AbsolutePythonPathInsideRootDir) {
  base::CommandLine cmdline(base::CommandLine::NO_PROGRAM);

  const char kDotfileContents[] = R"(
buildconfig = "//BUILDCONFIG.gn"
script_executable = ""
)";

  // Create a temp directory containing a .gn file and a BUILDCONFIG.gn file,
  // pass it as --root.
  base::ScopedTempDir in_temp_dir;
  ASSERT_TRUE(in_temp_dir.CreateUniqueTempDir());
  base::FilePath in_path = base::MakeAbsoluteFilePath(in_temp_dir.GetPath());
  base::FilePath dot_gn_name = in_path.Append(FILE_PATH_LITERAL(".gn"));
  WriteFile(dot_gn_name, kDotfileContents);

  base::FilePath absolute_bin_dir = in_path.Append(FILE_PATH_LITERAL("bin"));
  base::FilePath script_executable =
      absolute_bin_dir.Append(FILE_PATH_LITERAL("python"));

  WriteFile(in_path.Append(FILE_PATH_LITERAL("BUILDCONFIG.gn")), "");
  cmdline.AppendSwitchPath(switches::kRoot, in_path);
  cmdline.AppendSwitchPath(switches::kScriptExecutable, script_executable);

  base::FilePath build_dir = in_path.Append(FILE_PATH_LITERAL("out"));

  // Run setup and check that python_path_is_relative_to_build_dir is true and
  // python_path is relative to the build directory.
  Setup setup;
  Err err;
  EXPECT_TRUE(
      setup.DoSetupWithErr(FilePathToUTF8(build_dir), true, cmdline, &err));
  EXPECT_TRUE(setup.build_settings().python_path_is_relative_to_build_dir());
  EXPECT_EQ(setup.build_settings().python_path(),
            base::FilePath(FILE_PATH_LITERAL("../bin/python")));
}

TEST_F(SetupTest, AbsolutePythonPathOutsideRootDir) {
  base::CommandLine cmdline(base::CommandLine::NO_PROGRAM);

  const char kDotfileContents[] = R"(
buildconfig = "//BUILDCONFIG.gn"
script_executable = ""
)";

  // Create a temp directory containing a .gn file and a BUILDCONFIG.gn file,
  // pass it as --root.
  base::ScopedTempDir in_temp_dir;
  ASSERT_TRUE(in_temp_dir.CreateUniqueTempDir());
  base::FilePath in_path = base::MakeAbsoluteFilePath(in_temp_dir.GetPath());
  base::FilePath dot_gn_name = in_path.Append(FILE_PATH_LITERAL(".gn"));
  WriteFile(dot_gn_name, kDotfileContents);

  base::ScopedTempDir other_temp_dir;
  ASSERT_TRUE(other_temp_dir.CreateUniqueTempDir());
  base::FilePath absolute_bin_dir =
      other_temp_dir.GetPath().Append(FILE_PATH_LITERAL("bin"));
  base::FilePath script_executable =
      absolute_bin_dir.Append(FILE_PATH_LITERAL("python"));

  WriteFile(in_path.Append(FILE_PATH_LITERAL("BUILDCONFIG.gn")), "");
  cmdline.AppendSwitchPath(switches::kRoot, in_path);
  cmdline.AppendSwitchPath(switches::kScriptExecutable, script_executable);

  base::FilePath build_dir = in_path.Append(FILE_PATH_LITERAL("out"));

  // Run setup and check that python_path_is_relative_to_build_dir is false and
  // python_path is unchanged (apart from normalization).
  Setup setup;
  Err err;
  EXPECT_TRUE(
      setup.DoSetupWithErr(FilePathToUTF8(build_dir), true, cmdline, &err));
  EXPECT_FALSE(setup.build_settings().python_path_is_relative_to_build_dir());
  EXPECT_EQ(setup.build_settings().python_path(),
            script_executable.NormalizePathSeparatorsTo('/'));
}

TEST_F(SetupTest, RelativePythonPath) {
  base::CommandLine cmdline(base::CommandLine::NO_PROGRAM);

  const char kDotfileContents[] = R"(
buildconfig = "//BUILDCONFIG.gn"
script_executable = ""
)";

  // Create a temp directory containing a .gn file and a BUILDCONFIG.gn file,
  // pass it as --root.
  base::ScopedTempDir in_temp_dir;
  ASSERT_TRUE(in_temp_dir.CreateUniqueTempDir());
  base::FilePath in_path = base::MakeAbsoluteFilePath(in_temp_dir.GetPath());
  base::FilePath dot_gn_name = in_path.Append(FILE_PATH_LITERAL(".gn"));
  WriteFile(dot_gn_name, kDotfileContents);

  base::FilePath relative_bin_dir(FILE_PATH_LITERAL("bin"));
  base::FilePath script_executable =
      relative_bin_dir.Append(FILE_PATH_LITERAL("python"));

#if defined(OS_WIN)
  // On Windows, if script executable is a relative path, then it must exist in
  // either the current directory or PATH with a `.exe` or `.bat` extension,
  // otherwise `FindWindowsPython` fails.
  base::FilePath original_cwd;
  base::GetCurrentDirectory(&original_cwd);

  // Switch to another temporary directory for the test.
  base::ScopedTempDir cwd_temp_dir;
  ASSERT_TRUE(cwd_temp_dir.CreateUniqueTempDir());

  base::FilePath absolute_script_executable = cwd_temp_dir.GetPath()
                                                  .Append(script_executable)
                                                  .ReplaceExtension(u".exe");
  ASSERT_TRUE(base::CreateDirectoryAndGetError(
      absolute_script_executable.DirName(), nullptr));
  WriteFile(absolute_script_executable, "");
  base::SetCurrentDirectory(cwd_temp_dir.GetPath());
#endif

  WriteFile(in_path.Append(FILE_PATH_LITERAL("BUILDCONFIG.gn")), "");
  cmdline.AppendSwitchPath(switches::kRoot, in_path);
  cmdline.AppendSwitchPath(switches::kScriptExecutable, script_executable);

  base::FilePath build_dir = in_path.Append(FILE_PATH_LITERAL("out"));

  // Run setup and check that python_path_is_relative_to_build_dir is false and
  // python_path is made absolute on Windows, and unchanged on other platforms.
  Setup setup;
  Err err;
  EXPECT_TRUE(
      setup.DoSetupWithErr(FilePathToUTF8(build_dir), true, cmdline, &err));
  EXPECT_FALSE(setup.build_settings().python_path_is_relative_to_build_dir());

#if defined(OS_WIN)
  EXPECT_EQ(setup.build_settings().python_path(),
            absolute_script_executable.NormalizePathSeparatorsTo('/'));

  // Change back to the original cwd.
  base::SetCurrentDirectory(original_cwd);
#else
  EXPECT_EQ(setup.build_settings().python_path(), script_executable);
#endif
}

TEST_F(SetupTest, SourceRelativeScriptExecutable) {
  base::CommandLine cmdline(base::CommandLine::NO_PROGRAM);

  const char kDotfileContents[] = R"(
buildconfig = "//BUILDCONFIG.gn"
script_executable = "//third_party/python/bin/python3"
)";

  base::ScopedTempDir in_temp_dir;
  ASSERT_TRUE(in_temp_dir.CreateUniqueTempDir());
  base::FilePath in_path = base::MakeAbsoluteFilePath(in_temp_dir.GetPath());
  base::FilePath dot_gn_name = in_path.Append(FILE_PATH_LITERAL(".gn"));
  WriteFile(dot_gn_name, kDotfileContents);

  WriteFile(in_path.Append(FILE_PATH_LITERAL("BUILDCONFIG.gn")), "");
  cmdline.AppendSwitchPath(switches::kRoot, in_path);

  base::FilePath build_dir = in_path.Append(FILE_PATH_LITERAL("out"))
                                 .Append(FILE_PATH_LITERAL("default"));

  Setup setup;
  Err err;
  EXPECT_TRUE(
      setup.DoSetupWithErr(FilePathToUTF8(build_dir), true, cmdline, &err));
  EXPECT_TRUE(setup.build_settings().python_path_is_relative_to_build_dir());
  EXPECT_EQ(
      setup.build_settings().python_path(),
      base::FilePath(FILE_PATH_LITERAL("../../third_party/python/bin/python3"))
          .NormalizePathSeparatorsTo('/'));
}

TEST_F(SetupTest, HostOsAndHostCpuInDotFile) {
  base::CommandLine cmdline(base::CommandLine::NO_PROGRAM);

  const char kDotfileContents[] = R"(
buildconfig = "//BUILDCONFIG.gn"
assert(host_os != "")
assert(host_cpu != "")
if (host_os != "nonexistent_os") {
  default_args = {
    test_arg = host_os
  }
}
)";

  base::ScopedTempDir in_temp_dir;
  ASSERT_TRUE(in_temp_dir.CreateUniqueTempDir());
  base::FilePath in_path = base::MakeAbsoluteFilePath(in_temp_dir.GetPath());
  base::FilePath dot_gn_name = in_path.Append(FILE_PATH_LITERAL(".gn"));
  WriteFile(dot_gn_name, kDotfileContents);

  WriteFile(in_path.Append(FILE_PATH_LITERAL("BUILDCONFIG.gn")), "");
  cmdline.AppendSwitchPath(switches::kRoot, in_path);

  base::FilePath build_dir = in_path.Append(FILE_PATH_LITERAL("out"))
                                 .Append(FILE_PATH_LITERAL("default"));

  Setup setup;
  Err err;
  EXPECT_TRUE(
      setup.DoSetupWithErr(FilePathToUTF8(build_dir), true, cmdline, &err));
  EXPECT_FALSE(err.has_error());
}

// Convenience class to save the current process base::CommandLine
// on construction, modify it through the get() method, then restore
// the saved version on scope exit. This is useful for tests that need
// to modify the process global singleton temporarily.
class ScopedCommandLineForTest {
 public:
  ScopedCommandLineForTest()
      : command_line_ref_(*base::CommandLine::ForCurrentProcess()),
        saved_command_line_(command_line_ref_) {}

  ~ScopedCommandLineForTest() { command_line_ref_ = saved_command_line_; }

  base::CommandLine& get() { return command_line_ref_; }

 private:
  base::CommandLine& command_line_ref_;
  base::CommandLine saved_command_line_;
};

// A test that verifies that generated_file() generation doesn't crash.
// See the commands in builder_record.h regarding metadata walks
// requiring all items in the graph to be fully resolved to run safely.
//
// The test creates a graph that looks like:
//
//    A ---validation-->
//       B --validation-->
//         C --deps-->
//           D_0 --deps-->
//              D_1 --deps--> .. --deps--> D_19
//
// Where A will be finalized (written to the Ninja build plan) early while one
// of D_0 or D_19 is still undefined during the load.
//
// There is a potential race condition because GN loads build files concurrently
// and according to the state transition rules described in builder_record.h,
// A could be finalized as soon as C is defined, even if some of its
// dependencies (D_0 to D_19) are still undefined or being loaded in other
// threads, thus that some of the --deps--> edges between D_n and D_n+1 would be
// nullptr. (see https://crbug.com/gn/566346002 for more details).
//
// A's finalization calls NinjaGeneratedTargetWriter::Run() which delays
// metadata walks if collect_validations_metadata is true. If it didn't do
// that, starting a metadata walk would crash at runtime when trying to
// traverse the nullptr edge.
//
// This test sets --threads=4 to ensure that all dependencies cannot be
// defined when A is finalized. Removing the delay in
// NinjaGeneratedTargetWriter::Run() makes it crash consistently.
//
// The test also verifies that the generated file exists with the right content,
// i.e. that metadata collection was done properly, even if it was delayed.
TEST(GenCommandTest, ValidationMetadataRace) {
  MsgLoop msg_loop;
  ScopedCommandLineForTest global_cmdline;

  base::ScopedTempDir in_temp_dir;
  ASSERT_TRUE(in_temp_dir.CreateUniqueTempDir());
  base::FilePath in_path = base::MakeAbsoluteFilePath(in_temp_dir.GetPath());

  WriteFile(in_path.Append(FILE_PATH_LITERAL(".gn")),
            "buildconfig = \"//BUILDCONFIG.gn\"\n");

  WriteFile(in_path.Append(FILE_PATH_LITERAL("BUILDCONFIG.gn")),
            "set_default_toolchain(\"//:toolchain\")\n");

  std::string build_gn = R"(
toolchain("toolchain") {
  tool("stamp") {
    command = "touch {{output}}"
  }
}

generated_file("A") {
  outputs = [ "$target_out_dir/A.json" ]
  data_keys = [ "key" ]
  deps = []
  validations = [ ":B" ]
  collect_validations_metadata = true
}

group("B") {
  deps = []
  validations = [ ":C" ]
  metadata = {
    key = [ "value_b" ]
  }
}

group("C") {
  deps = [ ":D_0" ]
  metadata = {
    key = [ "value_c" ]
  }
}
)";

  // Create a chain of groups:
  //  D_0 --deps--> D_1 --deps--> D_2 --deps--> D_3 .... --deps--> D_19
  int chain_length = 20;
  for (int i = 0; i < chain_length; i++) {
    build_gn += "group(\"D_" + std::to_string(i) + "\") {\n";
    if (i < chain_length - 1) {
      build_gn += "  deps = [ \":D_" + std::to_string(i + 1) + "\" ]\n";
    }
    build_gn += "}\n";
  }

  build_gn += R"(
group("default") {
  deps = [ ":A" ]
}
)";

  WriteFile(in_path.Append(FILE_PATH_LITERAL("BUILD.gn")), build_gn);

  base::ScopedTempDir build_temp_dir;
  ASSERT_TRUE(build_temp_dir.CreateUniqueTempDir());

  global_cmdline.get().AppendSwitchPath(switches::kRoot, in_path);
  global_cmdline.get().AppendSwitch(switches::kFailOnUnusedArgs);
  global_cmdline.get().AppendSwitch(switches::kQuiet);
  global_cmdline.get().AppendSwitch(switches::kThreads, "4");

  std::vector<std::string> args;
  args.push_back(FilePathToUTF8(build_temp_dir.GetPath()));

  int exit_code = commands::RunGen(args);
  EXPECT_EQ(0, exit_code);

  // Now verify that the file was written with the right content
  std::string data = ReadFile(
      build_temp_dir.GetPath().Append(FILE_PATH_LITERAL("obj/A.json")));
  EXPECT_EQ(data, "value_c\nvalue_b\n");
}

// A variant of the previous test above where the last
// dependency in the chain will depend on a target that is
// never defined.
//
// The test creates a graph that looks like:
//
//    A ---validation-->
//       B --validation-->
//         C --deps-->
//           D_0 --deps-->
//              D_1 --deps--> .. --deps--> D_19 --> //missing:target
//
// Where //missing:target will never be defined.
//
// The test checks that an error is correctly reported in this case,
// to verify that delaying the metadata collection didn't delay
// proper checks.
TEST(GenCommandTest, ValidationMetadataRaceMissingTarget) {
  MsgLoop msg_loop;
  ScopedCommandLineForTest global_cmdline;

  base::ScopedTempDir in_temp_dir;
  ASSERT_TRUE(in_temp_dir.CreateUniqueTempDir());
  base::FilePath in_path = base::MakeAbsoluteFilePath(in_temp_dir.GetPath());

  WriteFile(in_path.Append(FILE_PATH_LITERAL(".gn")),
            "buildconfig = \"//BUILDCONFIG.gn\"\n");

  WriteFile(in_path.Append(FILE_PATH_LITERAL("BUILDCONFIG.gn")),
            "set_default_toolchain(\"//:toolchain\")\n");

  std::string build_gn = R"(
toolchain("toolchain") {
  tool("stamp") {
    command = "touch {{output}}"
  }
}

generated_file("A") {
  outputs = [ "$target_out_dir/A.json" ]
  data_keys = [ "key" ]
  deps = []
  validations = [ ":B" ]
  collect_validations_metadata = true
}

group("B") {
  deps = []
  validations = [ ":C" ]
  metadata = {
    key = [ "value_b" ]
  }
}

group("C") {
  deps = [ ":D_0" ]
  metadata = {
    key = [ "value_c" ]
  }
}
)";

  // Create a chain of groups:
  //  D_0 --deps--> D_1 --deps--> D_2 --deps--> D_3 .... --deps--> D_19
  //  Where D_19 depends on a missing target in an existing file.
  int chain_length = 20;
  for (int i = 0; i < chain_length; i++) {
    build_gn += "group(\"D_" + std::to_string(i) + "\") {\n";
    if (i < chain_length - 1) {
      build_gn += "  deps = [ \":D_" + std::to_string(i + 1) + "\" ]\n";
    } else {
      build_gn += "  deps = [ \"//missing:target\" ]\n";
    }
    build_gn += "}\n";
  }

  build_gn += R"(
group("default") {
  deps = [ ":A" ]
}
)";

  WriteFile(in_path.Append(FILE_PATH_LITERAL("BUILD.gn")), build_gn);

  // Create the missing directory and empty BUILD.gn
  ASSERT_TRUE(
      base::CreateDirectory(in_path.Append(FILE_PATH_LITERAL("missing"))));
  WriteFile(in_path.Append(FILE_PATH_LITERAL("missing/BUILD.gn")), "");

  base::ScopedTempDir build_temp_dir;
  ASSERT_TRUE(build_temp_dir.CreateUniqueTempDir());

  global_cmdline.get().AppendSwitchPath(switches::kRoot, in_path);
  global_cmdline.get().AppendSwitch(switches::kFailOnUnusedArgs);
  global_cmdline.get().AppendSwitch(switches::kQuiet);
  global_cmdline.get().AppendSwitch(switches::kThreads, "4");

  std::vector<std::string> args;
  args.push_back(FilePathToUTF8(build_temp_dir.GetPath()));

  {
    // Use a ScopedBufferedOutput class to prevent the test from printing
    // an "ERROR unresolved dependencies." message.
    ScopedBufferedOutput buffered_out;
    int exit_code = commands::RunGen(args);
    EXPECT_NE(0, exit_code);

    auto output_items = buffered_out.GetItems();
    ASSERT_EQ(3u, output_items.size());
    EXPECT_EQ(output_items[0].output, "ERROR ");
    EXPECT_EQ(output_items[0].decoration, DECORATION_RED);
    EXPECT_EQ(output_items[1].output, "Unresolved dependencies.\n");
    EXPECT_EQ(output_items[1].decoration, DECORATION_NONE);
    EXPECT_EQ(
        output_items[2].output,
        "//:D_19(//:toolchain)\n  needs //missing:target(//:toolchain)\n\n");
    EXPECT_EQ(output_items[2].decoration, DECORATION_NONE);
  }
}
