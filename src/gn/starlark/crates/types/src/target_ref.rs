// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use starlark::values::{AllocValue, Heap, StarlarkValue, Value};

use crate::{File, LabelRef, OutputType, Session};

/// Unfortunately while we could specify that Eq and Hash are implemented, there
/// is no way to delegate starlark's equality and hash function to it
/// automatically.
pub trait IPromiseToImplementStarlarkEqAndHash {}

/// An interface for a target in the build graph.
///
/// Since the real Target involves a lot of C++ interop, this allows us to
/// decouple the target from C++
pub trait TargetRef:
    for<'v> StarlarkValue<'v> + for<'v> AllocValue<'v> + Clone + IPromiseToImplementStarlarkEqAndHash
{
    /// Returns the label of the target.
    fn label(&self) -> LabelRef<'_>;
    /// Returns the toolchain the label was defined in.
    fn toolchain(&self) -> LabelRef<'_>;

    type Cxx: TargetMut;
    type Rule: for<'v> StarlarkValue<'v>;
    type Session: Session<TargetRef = Self>;

    /// Returns the rule that this target was built from.
    /// May return None if the target is a pure GN target.
    fn rule(&self) -> Option<&'static Self::Rule>;
    /// Returns the output files produced by this target.
    fn outputs(&self) -> Vec<File>;

    /// Returns the target's output directory path string.
    /// Toolchain_prefix goes right at the very front, before the toolchain
    /// Label_prefix goes in between the toolchain and the label
    /// Package_name_separator is what separates packages and labels (usually
    /// ":" or "/").
    fn target_out_dir(
        &self,
        toolchain_prefix: &str,
        label_prefix: &str,
        package_name_separator: &str,
    ) -> String;
    /// Returns the target's output type.
    fn output_type(&self) -> Option<OutputType>;

    /// Returns the resolved built-in attributes as Starlark values.
    fn builtin_attrs<'v>(&self, session: &Self::Session, heap: &Heap<'v>) -> Vec<Value<'v>>;
}

/// A trait for mutating a target before it is registered in the session.
pub trait TargetMut {
    /// Registers a dependency on this target.
    fn register_dependency(
        self: std::pin::Pin<&mut Self>,
        label: LabelRef<'_>,
        toolchain: LabelRef<'_>,
    );
}
