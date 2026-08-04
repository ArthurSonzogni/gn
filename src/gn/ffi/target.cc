// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gn/ffi/target.h"

#include <string_view>

#include "gn/label.h"
#include "gn/label_ptr.h"
#include "gn/source_dir.h"
#include "gn/target.h"

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
