// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_GENERATED_FILE_WRITER_H_
#define TOOLS_GN_GENERATED_FILE_WRITER_H_

#include "gn/unique_vector.h"

class Err;
class Target;

// Write the content of a given generated_file() target to disk. Targets found
// through its dep_keys are added to |deps_collected|.
bool WriteGeneratedFileToDisk(const Target* target,
                              Err* err,
                              UniqueVector<const Target*>* deps_collected);

#endif  // TOOLS_GN_GENERATED_FILE_WRITER_H_
