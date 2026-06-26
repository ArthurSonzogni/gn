// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::File;
use starlark::values::{AllocValue, StarlarkValue};

/// An interface for a target in the build graph.
///
/// Since the real Target involves a lot of C++ interop, this allows us to
/// decouple the target from C++
pub trait TargetRef: for<'v> StarlarkValue<'v> + for<'v> AllocValue<'v> + Clone {
    /// Returns the output files produced by this target.
    fn outputs(&self) -> Vec<File>;

    /// Returns the target's output directory path string.
    /// Toolchain_prefix goes right at the very front, before the toolchain
    /// Label_prefix goes in between the toolchain and the label
    /// Package_name_separator is what separates packages and labels (usually ":" or "/").
    fn target_out_dir(
        &self,
        toolchain_prefix: &str,
        label_prefix: &str,
        package_name_separator: &str,
    ) -> String;
}
