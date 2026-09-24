// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::cell::OnceCell;

use providers::Providers;
use starlark::values::FrozenValueTyped;

use crate::{eval_context::EvalContext, TargetRef};

pub(crate) struct StarlarkTarget {
    pub(crate) rule: FrozenValueTyped<'static, rule::Rule<'static, EvalContext>>,
    pub(crate) attrs: Vec<attr::Attr>,
}

/// The results of running the rule implementation on a target.
#[derive(Debug)]
pub(crate) struct EvaluatedTarget {
    pub(crate) providers: Providers,
}

pub struct Target {
    // We maintain a 0-1 relationship between starlark Targets and rust targets.
    // starlark targets store a reference to C++ targets, and C++ targets store an optional
    // reference to starlark targets.
    pub(crate) cxx: &'static crate::bridge::CxxTarget,
    pub(crate) starlark: Option<StarlarkTarget>,
    pub(crate) evaluated: OnceCell<EvaluatedTarget>,
}

impl Target {
    /// Executes the rule implementation for this target if it has a custom
    /// Starlark rule.
    pub fn execute_rule_impl(
        &self,
        session: &'static crate::Session,
        err: std::pin::Pin<&mut crate::bridge::Err>,
    ) -> &'static str {
        let Some(starlark) = &self.starlark else {
            return "";
        };
        if !starlark.rule.has_implementation() {
            return "";
        }
        // Safety: Targets in GN are pinned in the session and outlive rule execution.
        let static_self: &'static Self = unsafe { types::util::extend_lifetime(self) };
        let target_ref = TargetRef(static_self);
        let res = rule::run(&target_ref, |t| {
            crate::eval_context::EvalContext::new_rule_impl(session, *t)
        })
        .and_then(providers::Providers::try_from);
        if let Some(providers) = err.handle(res) {
            let phony = providers
                .outputs_phony
                .as_ref()
                .map(|f| f.as_str())
                .unwrap_or_default();
            self.evaluated
                .set(EvaluatedTarget { providers })
                .expect("Rules can only be executed once");
            phony
        } else {
            ""
        }
    }

    pub fn providers(&self) -> &Providers {
        &self
            .evaluated
            .get()
            .expect("Should only be called on an evaluated target")
            .providers
    }
}

impl std::ops::Deref for Target {
    type Target = crate::bridge::CxxTarget;

    fn deref(&self) -> &Self::Target {
        self.cxx
    }
}

impl allocative::Allocative for Target {
    fn visit<'a, 'b: 'a>(&self, visitor: &'a mut allocative::Visitor<'b>) {
        let visitor = visitor.enter_self_sized::<Self>();
        visitor.exit();
    }
}

// Safety: Target pointers in GN are heap-allocated and thread-safe to transfer
// across evaluation boundaries.
unsafe impl Send for Target {}
// Safety: Target pointers in GN are thread-safe to reference across evaluation
// boundaries.
unsafe impl Sync for Target {}

impl crate::bridge::CxxTarget {
    /// Returns the output type of the target as a u8 discriminant.
    pub fn output_type(&self) -> u8 {
        crate::bridge::output_type_u8(self)
    }

    /// Returns the settings for the target.
    pub fn settings(&self) -> &crate::Settings {
        // Safety: Settings pointer is always valid and non-null on constructed Targets.
        unsafe { self.settings_cxx().as_ref() }.unwrap()
    }

    /// Returns the toolchain label for the target.
    pub fn toolchain(&self) -> types::LabelRef<'_> {
        self.settings().toolchain_label().as_ref()
    }

    /// Returns a reference to the associated Rust Target, registering it with
    /// the session if it doesn't exist yet.
    pub fn to_rust(&self, session: &crate::Session) -> TargetRef {
        TargetRef(self.rust_target(session))
    }
}
