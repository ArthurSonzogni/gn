// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "generated_file_writer.h"

#include <ostream>
#include <vector>

#include "base/logging.h"
#include "gn/err.h"
#include "gn/output_conversion.h"
#include "gn/source_file.h"
#include "gn/string_output_buffer.h"
#include "gn/target.h"
#include "gn/trace.h"
#include "gn/value.h"

bool WriteGeneratedFileToDisk(const Target* target, Err* err) {
  const Settings* settings = target->settings();

  std::vector<SourceFile> outputs_as_sources;
  target->action_values().GetOutputsAsSourceFiles(target, &outputs_as_sources);
  CHECK(outputs_as_sources.size() == 1);

  base::FilePath output =
      settings->build_settings()->GetFullPath(outputs_as_sources[0]);
  ScopedTrace trace(TraceItem::TRACE_FILE_WRITE_GENERATED,
                    outputs_as_sources[0].value());
  trace.SetToolchain(target->settings()->toolchain_label());

  // If this is a metadata target, populate the write value with the appropriate
  // data.
  Value contents;
  if (target->contents().type() == Value::NONE) {
    // Origin is set to the outputs location, so that errors with this value
    // get flagged on the right target.
    CHECK(target->action_values().outputs().list().size() == 1U);
    contents = Value(target->action_values().outputs().list()[0].origin(),
                     Value::LIST);
    TargetSet targets_walked;
    ScopedTrace metadata_walk_trace(TraceItem::TRACE_WALK_METADATA,
                                    target->label());
    metadata_walk_trace.SetToolchain(target->settings()->toolchain_label());
    if (!target->GetMetadata(target->data_keys(), target->walk_keys(),
                             target->rebase(), /*deps_only = */ true,
                             &contents.list_value(), &targets_walked, err)) {
      return false;
    }
  } else {
    contents = target->contents();
  }

  // Compute output.
  StringOutputBuffer storage;
  std::ostream out(&storage);
  ConvertValueToOutput(settings, contents, target->output_conversion(), out,
                       err);

  if (err->has_error()) {
    return false;
  }
  return storage.WriteToFileIfChanged(output, err);
}
