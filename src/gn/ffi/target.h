// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_FFI_TARGET_H_
#define TOOLS_GN_FFI_TARGET_H_

#include <stdint.h>

#include "cxx.h"
#include "gn/label_ptr.h"

class Err;
class Scope;
class Target;

// Returns the output type of the target as a uint8_t discriminant.
uint8_t output_type_u8(const Target& target);

// Creates and generates a new target in the given scope.
Target* create_target(Scope& scope,
                      rust::Str name,
                      rust::Str output_type,
                      Err& err);

// Enforces that `target` cannot be resolved until the target for the label has
// been resolved.
void register_dependency(Target& target,
                         rust::Str package,
                         rust::Str name,
                         rust::Str toolchain_package,
                         rust::Str toolchain_name);

// Returns the resolved target from a LabelTargetPair.
// Must only be called if the target is already resolved.
const Target& label_target_pair_target(const LabelTargetPair& pair);

#endif  // TOOLS_GN_FFI_TARGET_H_
