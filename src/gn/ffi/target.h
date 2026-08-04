// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_FFI_TARGET_H_
#define TOOLS_GN_FFI_TARGET_H_

#include "cxx.h"

class Target;

// Enforces that `target` cannot be resolved until the target for the label has
// been resolved.
void register_dependency(Target& target,
                         rust::Str package,
                         rust::Str name,
                         rust::Str toolchain_package,
                         rust::Str toolchain_name);

#endif  // TOOLS_GN_FFI_TARGET_H_
