// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::cell::OnceCell;

use providers::Providers;
use starlark::collections::SmallSet;
use types::Phony;

use crate::{StringOutputBuffer, TargetRef, escape::Ninja, eval_context::EvalContext};

/// The results of running the rule implementation on a target.
#[derive(Debug)]
pub(crate) struct EvaluatedTarget {
    pub(crate) providers: Providers,
    pub(crate) phonies: Vec<Phony>,
}

pub struct Target {
    // We maintain a 0-1 relationship between starlark Targets and rust targets.
    // starlark targets store a reference to C++ targets, and C++ targets store an optional
    // reference to starlark targets.
    pub(crate) cxx: &'static crate::bridge::CxxTarget,
    pub(crate) rule: &'static rule::Rule<'static, EvalContext>,
    pub(crate) attrs: Vec<attr::Attr>,
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
        // Safety: Targets in GN are pinned in the session and outlive rule execution.
        let static_self: &'static Self = unsafe { types::util::extend_lifetime(self) };
        let target_ref = TargetRef(static_self);
        let res = rule::run(&target_ref, |t| {
            crate::eval_context::EvalContext::new_rule_impl(session, *t)
        })
        .and_then(|(phonies, frozen)| {
            providers::Providers::try_from(frozen).map(|providers| (phonies, providers))
        });
        if let Some((phonies, providers)) = err.handle(res) {
            let phony = providers
                .outputs_phony
                .as_ref()
                .map(|f| f.as_str())
                .unwrap_or_default();
            self.evaluated
                .set(EvaluatedTarget { providers, phonies })
                .expect("Rules can only be executed once");
            phony
        } else {
            ""
        }
    }

    /// Writes out Ninja phony rules declared by this target into the provided buffer.
    pub fn write_phonies(&self, buffer: std::pin::Pin<&mut crate::bridge::StringOutputBuffer>) {
        if let Some(evaluated) = self.evaluated.get() {
            let mut buf = StringOutputBuffer::new(buffer);
            for phony in &evaluated.phonies {
                buf.append(("build ", Ninja(phony.phony.as_str()), ": phony"));
                for dep in &phony.expansion {
                    buf.append((b' ', Ninja(dep.as_str())));
                }
                buf.append(b'\n');
            }
        }
    }

    /// Writes the substitutions declared by GnSubstitutionsInfo to the ninja file.
    pub fn write_substitutions(
        &self,
        buffer: std::pin::Pin<&mut crate::bridge::StringOutputBuffer>,
        platform: crate::bridge::EscapingPlatform,
        mut err: std::pin::Pin<&mut crate::bridge::Err>,
    ) {
        err.as_mut().handle((|| -> starlark::Result<()> {
            if let Some(evaluated) = self.evaluated.get() {
                let mut buf = StringOutputBuffer::new(buffer);
                evaluated.providers.substitutions(|substitutions, eval| {
                    for (name, args) in substitutions {
                        buf.append(("starlark_", name.as_str(), " = "));
                        crate::args::write_args(&mut buf, eval, args, platform)?;
                        buf.append(b'\n');
                    }
                    Ok(())
                })?;
            }
            Ok(())
        })());
    }

    /// Writes the inputs for `outputs` declared by GnInputsInfo to the ninja
    /// file.
    pub fn write_inputs(
        &self,
        outputs: &cxx::CxxVector<crate::bridge::OutputFile>,
        mut has_inputs: bool,
        buffer: std::pin::Pin<&mut crate::bridge::StringOutputBuffer>,
    ) -> bool {
        if let Some(evaluated) = self.evaluated.get() {
            let mut buf = StringOutputBuffer::new(buffer);
            let mut seen = SmallSet::new();
            for (suffix, phony) in &evaluated.providers.inputs_phony {
                if outputs
                    .iter()
                    .any(|out| out.value().ends_with(suffix.as_str()))
                    && seen.insert(phony.as_str())
                {
                    if !has_inputs {
                        buf.append(" |");
                        has_inputs = true;
                    }
                    buf.append((b' ', Ninja(phony.as_str())));
                }
            }
        }
        has_inputs
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
