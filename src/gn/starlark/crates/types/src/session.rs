// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{CtxState, LabelRef, TargetRef};

/// An interface for the starlark session.
pub trait Session {
    /// The target type managed by this session.
    type TargetRef: TargetRef;

    /// Look up a target in the session by its label and current toolchain.
    fn get_target(&self, label: LabelRef<'_>, toolchain: LabelRef<'_>) -> Self::TargetRef;

    /// Evaluates the built-in rule implementation for the given target,
    /// returning a list of provider instances (e.g. `[DefaultInfo(...)]`).
    fn run_builtin_rule<'v>(
        &self,
        heap: &starlark::values::Heap<'v>,
        target: &Self::TargetRef,
        state: &mut CtxState<Self::TargetRef>,
    ) -> starlark::values::Value<'v>;
}
