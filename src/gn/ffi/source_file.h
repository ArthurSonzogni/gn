// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_FFI_SOURCE_FILE_H_
#define TOOLS_GN_FFI_SOURCE_FILE_H_

#include "cxx.h"

class Settings;
class SourceFile;

// Converts a source file to an output file relative to the build settings and
// returns its path.
rust::Str source_file_to_output_path(const Settings& settings,
                                     const SourceFile& file);

#endif  // TOOLS_GN_FFI_SOURCE_FILE_H_
