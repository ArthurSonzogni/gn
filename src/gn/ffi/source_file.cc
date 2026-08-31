// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gn/ffi/source_file.h"

#include "gn/output_file.h"
#include "gn/settings.h"
#include "gn/source_file.h"

rust::Str source_file_to_output_path(const Settings& settings,
                                     const SourceFile& file) {
  OutputFile output_file(settings.build_settings(), file);
  return rust::Str(output_file.value());
}
