// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use allocative::Allocative;
use starlark::values::{AllocValue, Heap, ProvidesStaticType, StarlarkValue, Value};
use starlark_derive::{starlark_value, NoSerialize};
use types::LabelRef;

#[derive(Clone, Allocative, ProvidesStaticType, Debug, NoSerialize, PartialEq, Eq, Hash)]
pub struct TargetRef;

impl std::fmt::Display for TargetRef {
    fn fmt(&self, _f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        todo!()
    }
}

impl types::IPromiseToImplementStarlarkEqAndHash for TargetRef {}

#[starlark_value(type = "Target")]
impl<'v> StarlarkValue<'v> for TargetRef {
    fn equals(&self, _other: Value<'v>) -> starlark::Result<bool> {
        todo!()
    }

    fn write_hash(
        &self,
        _hasher: &mut starlark::collections::StarlarkHasher,
    ) -> starlark::Result<()> {
        todo!()
    }
}

impl<'v> AllocValue<'v> for TargetRef {
    fn alloc_value(self, heap: Heap<'v>) -> Value<'v> {
        heap.alloc_simple(self)
    }
}

impl types::TargetRef for TargetRef {
    type Rule = rule::FrozenRule<crate::eval_context::EvalContext>;

    fn label(&self) -> LabelRef<'_> {
        todo!()
    }

    fn toolchain(&self) -> LabelRef<'_> {
        todo!()
    }

    fn rule(&self) -> Option<&'static Self::Rule> {
        todo!()
    }

    fn outputs(&self) -> Vec<types::File> {
        todo!()
    }

    fn target_out_dir(&self, _prefix: &str, _suffix: &str, _separator: &str) -> String {
        todo!()
    }

    fn register_dependencies<S: types::Session<TargetRef = Self>>(
        &self,
        _session: &S,
        _toolchain: LabelRef<'_>,
    ) {
        todo!()
    }

    fn output_type(&self) -> Option<types::OutputType> {
        todo!()
    }

    fn builtin_attrs<'v>(&self, _heap: &Heap<'v>) -> Vec<Value<'v>> {
        todo!()
    }
}
