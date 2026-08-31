// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gn/ffi/target.h"

#include <string_view>

#include "gn/label.h"
#include "gn/label_ptr.h"
#include "gn/source_dir.h"
#include "gn/target.h"
#include "gn/target_generator.h"

uint8_t output_type_u8(const Target& target) {
  return static_cast<uint8_t>(target.output_type());
}

Target* create_target(Scope& scope,
                      rust::Str name,
                      rust::Str output_type,
                      Err& err) {
  return TargetGenerator::GenerateTarget(&scope, nullptr,
                                         std::string_view(name),
                                         std::string_view(output_type), &err);
}

void register_dependency(Target& target,
                         rust::Str package,
                         rust::Str name,
                         rust::Str toolchain_package,
                         rust::Str toolchain_name) {
  target.starlark_deps().push_back(LabelTargetPair(
      Label(SourceDir(std::string_view(package)), std::string_view(name),
            SourceDir(std::string_view(toolchain_package)),
            std::string_view(toolchain_name))));
}
