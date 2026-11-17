// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gn/ninja_generated_file_target_writer.h"

#include <vector>

#include "gn/generated_file_writer.h"
#include "gn/output_conversion.h"
#include "gn/output_file.h"
#include "gn/scheduler.h"
#include "gn/settings.h"
#include "gn/string_output_buffer.h"
#include "gn/string_utils.h"
#include "gn/target.h"
#include "gn/trace.h"

NinjaGeneratedFileTargetWriter::NinjaGeneratedFileTargetWriter(
    const Target* target,
    StringOutputBuffer& out)
    : NinjaTargetWriter(target, out) {}

NinjaGeneratedFileTargetWriter::~NinjaGeneratedFileTargetWriter() = default;

void NinjaGeneratedFileTargetWriter::Run() {
  // Write the file. Targets whose metadata walks require a fully resolved
  // graph are only written after all targets are resolved (see command_gen.cc).
  UniqueVector<const Target*> deps_collected;
  Err err;
  if (!WriteGeneratedFileToDisk(target_, &err, &deps_collected)) {
    g_scheduler->FailWithError(err);
    return;
  }

  // A generated_file target should generate a phony target with dependencies
  // on each of the deps, data_deps, and dep_keys targets. The actual collection
  // is done at gen time, but to have correct input deps in ninja, we add output
  // from generated_file targets as deps for the stamp. Any dependency cycles
  // introduced via dep_keys (when combined with walk_deps or validations) are
  // detected by Ninja.
  std::vector<OutputFile> output_files = target_->computed_outputs();
  std::vector<OutputFile> data_output_files;
  TargetSet seen;
  auto add_deps = [&](const auto& deps, bool data_only) {
    for (const Target* dep : deps) {
      if (dep->has_dependency_output() && seen.add(dep)) {
        if (data_only || dep->IsDataOnly())
          data_output_files.push_back(dep->dependency_output());
        else
          output_files.push_back(dep->dependency_output());
      }
    }
  };
  const auto& target_deps = resolved().GetTargetDeps(target_);
  add_deps(target_deps.linked_deps(), /*data_only=*/false);
  add_deps(deps_collected, /*data_only=*/false);
  add_deps(target_deps.data_deps(), /*data_only=*/true);

  WriteStampOrPhonyForTarget(output_files, data_output_files);
}
