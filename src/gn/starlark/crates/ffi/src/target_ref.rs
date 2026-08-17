// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use allocative::Allocative;
use starlark::values::{
    AllocValue, Heap, ProvidesStaticType, StarlarkValue, Value, ValueLike as _,
};
use starlark_derive::{starlark_value, NoSerialize};
use types::{LabelRef, TargetRef as _};

use crate::target::Target;

#[derive(Clone, Copy, Allocative, ProvidesStaticType, NoSerialize)]
pub struct TargetRef(pub(crate) &'static Target);

impl PartialEq for TargetRef {
    fn eq(&self, other: &Self) -> bool {
        std::ptr::eq(self.0, other.0)
    }
}
impl Eq for TargetRef {}

impl std::hash::Hash for TargetRef {
    fn hash<H: std::hash::Hasher>(&self, state: &mut H) {
        std::ptr::hash(self.0, state);
    }
}

impl std::fmt::Debug for TargetRef {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        // Delegate to Display
        write!(f, "{self}")
    }
}

impl std::fmt::Display for TargetRef {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.label())
    }
}

impl types::IPromiseToImplementStarlarkEqAndHash for TargetRef {}

#[starlark_value(type = "Target")]
impl<'v> StarlarkValue<'v> for TargetRef {
    fn equals(&self, other: Value<'v>) -> starlark::Result<bool> {
        Ok(other
            .downcast_ref::<Self>()
            .is_some_and(|other| other == self))
    }

    fn write_hash(
        &self,
        hasher: &mut starlark::collections::StarlarkHasher,
    ) -> starlark::Result<()> {
        use std::hash::Hash as _;
        self.hash(hasher);
        Ok(())
    }
}

impl<'v> AllocValue<'v> for TargetRef {
    fn alloc_value(self, heap: Heap<'v>) -> Value<'v> {
        heap.alloc_simple(self)
    }
}

impl types::TargetRef for TargetRef {
    type Cxx = crate::bridge::CxxTarget;
    type Rule = rule::FrozenRule<crate::eval_context::EvalContext>;

    fn label(&self) -> LabelRef<'_> {
        self.0.label().as_ref()
    }

    fn toolchain(&self) -> LabelRef<'_> {
        self.0.toolchain()
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

    fn output_type(&self) -> Option<types::OutputType> {
        todo!()
    }

    fn builtin_attrs<'v>(&self, _heap: &Heap<'v>) -> Vec<Value<'v>> {
        todo!()
    }
}

impl attr::TargetAttrExt for TargetRef {
    fn attrs(&self) -> &[attr::Attr] {
        &[]
    }
}

impl types::TargetMut for crate::bridge::CxxTarget {
    fn register_dependency(
        self: std::pin::Pin<&mut Self>,
        label: LabelRef<'_>,
        toolchain: LabelRef<'_>,
    ) {
        crate::bridge::register_dependency(
            self,
            label.package().as_str(),
            label.name(),
            toolchain.package().as_str(),
            toolchain.name(),
        );
    }
}
