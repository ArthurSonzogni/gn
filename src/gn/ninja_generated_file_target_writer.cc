// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gn/ninja_generated_file_target_writer.h"

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
    std::ostream& out)
    : NinjaTargetWriter(target, out) {}

NinjaGeneratedFileTargetWriter::~NinjaGeneratedFileTargetWriter() = default;

void NinjaGeneratedFileTargetWriter::Run() {
  // Write the file.
  if (target_->collect_validations_metadata()) {
    // Do not write the file yet because some transitive dependencies
    // might not be fully resolved, causing runtime crashes during the
    // write. The |delayed_generated_file_write_| pointer must be set
    // when validations metadata is collected.
    CHECK(delayed_generated_file_write_ptr_);
    *delayed_generated_file_write_ptr_ = true;
  } else {
    Err err;
    if (!WriteGeneratedFileToDisk(target_, &err)) {
      g_scheduler->FailWithError(err);
    }
  }

  // A generated_file target should generate a phony target with dependencies
  // on each of the deps and data_deps in the target. The actual collection is
  // done at gen time, but to have correct input deps in ninja, we add output
  // from generated_file targets as deps for the stamp.
  std::vector<OutputFile> output_files = target_->computed_outputs();
  std::vector<OutputFile> data_output_files;
  const auto& target_deps = resolved().GetTargetDeps(target_);
  for (const Target* dep : target_deps.linked_deps()) {
    if (!dep->has_dependency_output()) {
      continue;
    }
    if (dep->IsDataOnly()) {
      data_output_files.push_back(dep->dependency_output());
    } else {
      output_files.push_back(dep->dependency_output());
    }
  }

  for (const Target* data_dep : target_deps.data_deps()) {
    if (data_dep->has_dependency_output())
      data_output_files.push_back(data_dep->dependency_output());
  }

  WriteStampOrPhonyForTarget(output_files, data_output_files);
}
