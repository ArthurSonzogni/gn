// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_EXEC_PROCESS_H_
#define TOOLS_GN_EXEC_PROCESS_H_

#include <string>

#include "util/build_config.h"

namespace base {
class CommandLine;
class FilePath;
}  // namespace base

namespace internal {

// Runs `cmdline` in `startup_dir`, returning what it prints in `std_out` and
// `std_err`, and its exit code in `exit_code`. If the program can't be
// started, returns false with the reason in `std_err`.
bool ExecProcess(const base::CommandLine& cmdline,
                 const base::FilePath& startup_dir,
                 std::string* std_out,
                 std::string* std_err,
                 int* exit_code);

#if defined(OS_WIN)
bool ExecProcess(const std::u16string& cmdline_str,
                 const base::FilePath& startup_dir,
                 std::string* std_out,
                 std::string* std_err,
                 int* exit_code);
#endif  // OS_WIN

// Describes why the program of `cmdline` couldn't be run, from the `std_err`
// that ExecProcess() returned along with false.
std::string DescribeStartFailure(const base::CommandLine& cmdline,
                                 const std::string& std_err);

}  // namespace internal

#endif  // TOOLS_GN_EXEC_PROCESS_H_
