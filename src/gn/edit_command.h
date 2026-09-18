// Copyright 2026 The GN Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_EDIT_COMMAND_H_
#define TOOLS_GN_EDIT_COMMAND_H_

#include <string>
#include <vector>

#include "gn/edit_subcommands.h"
#include "gn/err.h"
#include "gn/source_file.h"

class BuildFile;
class Setup;
namespace commands {

// Runs an edit command, and returns a list of files that were modified.
//
// `build_files` is filled with the build files the edit ran against.
Result<std::pair<std::vector<SourceFile>, EditState>> RunEditImpl(
    const std::vector<std::string>& args,
    Setup& setup,
    std::vector<BuildFile>& build_files);

}  // namespace commands

#endif  // TOOLS_GN_EDIT_COMMAND_H_
