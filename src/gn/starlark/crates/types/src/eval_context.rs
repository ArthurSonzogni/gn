// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{LabelRef, PackageRef, PathResolver, Session};

/// The starlark Evaluator has an "extra" object that you can use to store
/// whatever metadata you want, accessible to any custom functions you write
/// in starlark.
///
/// This interface contains the specific features we need to access for our
/// bazel-like API.
pub trait EvalContext:
    for<'v> starlark::values::ProvidesStaticType<'v, StaticType = Self>
    + allocative::Allocative
    + Send
    + Sync
    + 'static
{
    /// The session type associated with this context.
    type Session: Session;

    /// Returns the package currently being evaluated.
    fn current_package(&self) -> &PackageRef;

    /// Returns the path resolver to map source files to output files.
    fn path_resolver(&self) -> &PathResolver;

    /// Returns the Starlark session.
    fn session(&self) -> &Self::Session;

    /// Returns the label of the current toolchain.
    fn current_toolchain(&self) -> LabelRef<'_>;

    /// Asserts that the current evaluation context is a macro context.
    /// Use when a function cannot be called from other contexts (eg. `static_library` cannot be called inside rule evaluation)
    fn require_macro(&self) -> starlark::Result<()>;

    /// Asserts that the current evaluation context is a bzl file context.
    fn require_bzl(&self) -> starlark::Result<()>;

    /// Asserts that the evaluator is executing a rule implementation, and returns the state of the rule implementation.
    fn require_rule_impl(
        &self,
    ) -> starlark::Result<&crate::CtxState<<Self::Session as Session>::TargetRef>>;

    /// Asserts that the evaluator is executing a rule implementation, and returns the mutable state of the rule implementation.
    fn require_rule_impl_mut(
        &mut self,
    ) -> starlark::Result<&mut crate::CtxState<<Self::Session as Session>::TargetRef>>;
}

/// Extension trait to add the methods `.context` and `.context_mut` to the starlark Evaluator.
pub trait EvaluatorContextExt<'v, 'a, 'e> {
    /// Returns a reference to the evaluation context.
    fn context<C: EvalContext>(&self) -> &C;

    /// Returns a mutable reference to the evaluation context.
    fn context_mut<C: EvalContext>(&mut self) -> &mut C;
}

impl<'v, 'a, 'e> EvaluatorContextExt<'v, 'a, 'e> for starlark::eval::Evaluator<'v, 'a, 'e> {
    #[inline]
    fn context<C: EvalContext>(&self) -> &C {
        let extra = self.extra_mut.as_ref();
        debug_assert!(extra.is_some(), "evaluator context not set");
        let dyn_any = unsafe { extra.unwrap_unchecked() };
        debug_assert!(dyn_any.is::<C>(), "failed to downcast evaluator context");
        unsafe { dyn_any.downcast_ref::<C>().unwrap_unchecked() }
    }

    #[inline]
    fn context_mut<C: EvalContext>(&mut self) -> &mut C {
        let extra = self.extra_mut.as_mut();
        debug_assert!(extra.is_some(), "evaluator context not set");
        let dyn_any = unsafe { extra.unwrap_unchecked() };
        debug_assert!(dyn_any.is::<C>(), "failed to downcast evaluator context");
        unsafe { dyn_any.downcast_mut::<C>().unwrap_unchecked() }
    }
}
