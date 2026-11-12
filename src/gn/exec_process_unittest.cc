// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gn/exec_process.h"

#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/logging.h"
#include "base/strings/string_util.h"
#include "gn/filesystem_utils.h"
#include "util/build_config.h"
#include "util/test/test.h"

#if defined(OS_WIN)
#include <windows.h>

#include "base/strings/utf_string_conversions.h"
#else
#include <errno.h>
#include <signal.h>
#endif

namespace internal {

// TODO(cjhopman): Enable these tests when windows ExecProcess handles stderr.
// 'python' is not runnable on Windows. Adding ["cmd", "/c"] fails because
// CommandLine does unusual reordering of args.
#if !defined(OS_WIN)
namespace {
bool ExecPython(const std::string& command,
                std::string* std_out,
                std::string* std_err,
                int* exit_code) {
  base::ScopedTempDir temp_dir;
  CHECK(temp_dir.CreateUniqueTempDir());
  base::CommandLine::StringVector args;
#if defined(OS_WIN)
  args.push_back(L"python");
  args.push_back(L"-c");
  args.push_back(base::UTF8ToUTF16(command));
#else
  args.push_back("python3");
  args.push_back("-c");
  args.push_back(command);
#endif
  return ExecProcess(base::CommandLine(args), temp_dir.GetPath(), std_out,
                     std_err, exit_code);
}
}  // namespace

TEST(ExecProcessTest, TestExitCode) {
  std::string std_out, std_err;
  int exit_code;

  ASSERT_TRUE(
      ExecPython("import sys; sys.exit(0)", &std_out, &std_err, &exit_code));
  EXPECT_EQ(0, exit_code);

  ASSERT_TRUE(
      ExecPython("import sys; sys.exit(1)", &std_out, &std_err, &exit_code));
  EXPECT_EQ(1, exit_code);

  ASSERT_TRUE(
      ExecPython("import sys; sys.exit(253)", &std_out, &std_err, &exit_code));
  EXPECT_EQ(253, exit_code);

  ASSERT_TRUE(ExecPython("throw Exception()", &std_out, &std_err, &exit_code));
  EXPECT_EQ(1, exit_code);
}

// Test that large output is handled correctly. There are various ways that this
// could potentially fail. For example, non-blocking Linux pipes have a 65536
// byte buffer and, if stdout is non-blocking, python will throw an IOError when
// a write exceeds the buffer size.
TEST(ExecProcessTest, TestLargeOutput) {
  std::string std_out, std_err;
  int exit_code;

  ASSERT_TRUE(ExecPython("import sys; print('o' * 1000000)", &std_out, &std_err,
                         &exit_code));
  EXPECT_EQ(0, exit_code);
  EXPECT_EQ(1000001u, std_out.size());
}

TEST(ExecProcessTest, TestStdoutAndStderrOutput) {
  std::string std_out, std_err;
  int exit_code;

  ASSERT_TRUE(
      ExecPython("from __future__ import print_function; import sys; print('o' "
                 "* 10000); print('e' * 10000, file=sys.stderr)",
                 &std_out, &std_err, &exit_code));
  EXPECT_EQ(0, exit_code);
  EXPECT_EQ(10001u, std_out.size());
  EXPECT_EQ(10001u, std_err.size());

  std_out.clear();
  std_err.clear();
  ASSERT_TRUE(
      ExecPython("from __future__ import print_function; import sys; print('e' "
                 "* 10000, file=sys.stderr); print('o' * 10000)",
                 &std_out, &std_err, &exit_code));
  EXPECT_EQ(0, exit_code);
  EXPECT_EQ(10001u, std_out.size());
  EXPECT_EQ(10001u, std_err.size());
}

TEST(ExecProcessTest, TestOneOutputClosed) {
  std::string std_out, std_err;
  int exit_code;

  ASSERT_TRUE(ExecPython("import sys; sys.stderr.close(); print('o' * 10000)",
                         &std_out, &std_err, &exit_code));
  EXPECT_EQ(0, exit_code);
  EXPECT_EQ(10001u, std_out.size());
  EXPECT_EQ(std_err.size(), 0u);

  std_out.clear();
  std_err.clear();
  ASSERT_TRUE(
      ExecPython("from __future__ import print_function; import sys; "
                 "sys.stdout.close(); print('e' * 10000, file=sys.stderr)",
                 &std_out, &std_err, &exit_code));
  EXPECT_EQ(0, exit_code);
  EXPECT_EQ(0u, std_out.size());
  EXPECT_EQ(10001u, std_err.size());
}
#endif

namespace {

// Expects `program` not to start because of `error`, which `std_err` reports.
void ExpectStartFailure(const base::FilePath& program,
                        logging::SystemErrorCode error) {
  base::CommandLine cmdline(program);
  std::string std_out, std_err;
  int exit_code;
  EXPECT_FALSE(
      ExecProcess(cmdline, program.DirName(), &std_out, &std_err, &exit_code));
  std::string reason = logging::SystemErrorCodeToString(error);
#if defined(OS_WIN)
  base::ReplaceSubstringsAfterOffset(&reason, 0, "%1", FilePathToUTF8(program));
#endif
  EXPECT_EQ(reason, std_err);
}

}  // namespace

// A program that can't be started is a failure, rather than a process that
// exited with an error.
TEST(ExecProcessTest, MissingProgram) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath program =
      temp_dir.GetPath().Append(FILE_PATH_LITERAL("missing"));
#if defined(OS_WIN)
  ExpectStartFailure(program, ERROR_FILE_NOT_FOUND);
#else
  ExpectStartFailure(program, ENOENT);
#endif
}

// A file that exists but can't be run fails for another reason.
TEST(ExecProcessTest, ProgramNotExecutable) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  // It's not a valid program on Windows, and not executable on POSIX. Without
  // the extension, CreateProcess() would look for "not_executable.exe".
  base::FilePath program =
      temp_dir.GetPath().Append(FILE_PATH_LITERAL("not_executable.exe"));
  ASSERT_EQ(0, base::WriteFile(program, "", 0));
#if defined(OS_WIN)
  ExpectStartFailure(program, ERROR_BAD_EXE_FORMAT);
#else
  ExpectStartFailure(program, EACCES);
#endif
}

#if !defined(OS_WIN)
// A program killed by a signal exits with 128 plus the signal, like in shells.
TEST(ExecProcessTest, ProgramKilledBySignal) {
  base::CommandLine cmdline(base::FilePath("/bin/sh"));
  cmdline.AppendArg("-c");
  cmdline.AppendArg("kill -KILL $$");
  std::string std_out, std_err;
  int exit_code;
  EXPECT_TRUE(ExecProcess(cmdline, base::FilePath("/"), &std_out, &std_err,
                          &exit_code));
  EXPECT_EQ(128 + SIGKILL, exit_code);
}
#endif

}  // namespace internal
