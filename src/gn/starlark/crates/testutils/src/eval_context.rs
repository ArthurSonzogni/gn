// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use std::collections::HashMap;

use attr::{Attr, EvalContext as AttrEvalContext, EvalContextAttrExt, Session as AttrSession};
use starlark::{
    values::{FrozenValue, Heap, ProvidesStaticType, Value},
    Result,
};
use types::{
    CtxState, Label, LabelRef, OutputType, Package, PackageRef, PathResolver, Scope, Session,
};

use crate::{FakeSession, FakeTarget, FakeTargetRef};

#[derive(Clone, Default, Debug)]
pub struct FakeScope(HashMap<String, Value<'static>>);

impl Scope for FakeScope {
    fn copy_with<'a, 'v>(&self, kv: impl Iterator<Item = (&'a str, Value<'v>)>) -> Self {
        let mut values = self.0.clone();
        for (k, v) in kv {
            // Safety: Transmuting 'v to 'static is safe because this mock scope
            // is only used in tests, where the Starlark heap outlives the evaluation
            // context.
            let static_val = unsafe { std::mem::transmute::<Value<'v>, Value<'static>>(v) };
            values.insert(k.to_owned(), static_val);
        }
        Self(values)
    }

    fn get<'v>(&self, key: &str, _heap: &Heap<'v>) -> Option<Value<'v>> {
        self.0.get(key).map(|v| {
            // Safety: Shortening the lifetime is always safe.
            unsafe { std::mem::transmute::<Value<'static>, Value<'v>>(*v) }
        })
    }
}

/// A simple implementation of the evaluation context used in Starlark unit
/// tests.
#[derive(allocative::Allocative)]
pub struct FakeEvalContext {
    /// The current package being processed.
    pub package: Package,
    /// The current toolchain.
    pub current_toolchain: Label,
    /// The fake starlark session.
    #[allocative(skip)]
    pub session: FakeSession,
    /// The fake path resolver.
    #[allocative(skip)]
    pub path_resolver: PathResolver,
    /// The fake rule state.
    #[allocative(skip)]
    pub rule_state: CtxState<FakeTargetRef>,
    /// The fake scope.
    #[allocative(skip)]
    pub scope: FakeScope,
}

unsafe impl<'v> ProvidesStaticType<'v> for FakeEvalContext {
    type StaticType = Self;
}

impl Default for FakeEvalContext {
    fn default() -> Self {
        Self::new("//")
    }
}

impl FakeEvalContext {
    /// Creates a new `FakeEvalContext` for a given package name.
    pub fn new(package: &str) -> Self {
        let session = FakeSession::new();
        Self {
            package: PackageRef::new(package).unwrap().to_owned(),
            current_toolchain: session.default_toolchain.clone(),
            session,
            path_resolver: PathResolver::new_for_testing(),
            rule_state: CtxState::new(FakeTargetRef::default()),
            scope: FakeScope::default(),
        }
    }
}

impl AttrEvalContext for FakeEvalContext {
    type Session = FakeSession;
    type Scope = FakeScope;

    fn session(&self) -> &Self::Session {
        &self.session
    }

    fn current_package(&self) -> &PackageRef {
        &self.package
    }

    fn path_resolver(&self) -> &PathResolver {
        &self.path_resolver
    }

    fn current_toolchain(&self) -> LabelRef<'_> {
        self.current_toolchain.as_ref()
    }

    fn require_macro(&self) -> Result<&FakeScope> {
        Ok(&self.scope)
    }

    fn require_bzl(&self) -> Result<()> {
        Ok(())
    }

    fn require_rule_impl(&self) -> Result<&CtxState<<Self::Session as Session>::TargetRef>> {
        Ok(&self.rule_state)
    }

    fn require_rule_impl_mut(
        &mut self,
    ) -> Result<&mut CtxState<<Self::Session as Session>::TargetRef>> {
        Ok(&mut self.rule_state)
    }
}

impl EvalContextAttrExt for FakeEvalContext {
    fn create_target(
        &self,
        target_type: Option<OutputType>,
        target_name: &str,
        scope: &FakeScope,
        rule: FrozenValue,
        attrs: Vec<Attr>,
    ) -> Result<<Self::Session as AttrSession>::TargetRef> {
        let label = Label::new(self.package.clone(), target_name.to_owned());
        let target = FakeTargetRef::new(FakeTarget {
            output_type: target_type,
            rule,
            cxx_attrs: scope.0.clone(),
            outputs: vec![],
            attrs,
            ..Default::default()
        });
        self.session.insert_target(label, target.clone());
        Ok(target)
    }
}
