// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use starlark::values::FrozenValueTyped;

use crate::eval_context::EvalContext;

pub(crate) struct StarlarkTarget {
    pub(crate) rule: FrozenValueTyped<'static, rule::FrozenRule<EvalContext>>,
    pub(crate) attrs: Vec<attr::Attr>,
}

pub struct Target {
    // We maintain a 0-1 relationship between starlark Targets and rust targets.
    // starlark targets store a reference to C++ targets, and C++ targets store an optional
    // reference to starlark targets.
    pub(crate) cxx: &'static crate::bridge::CxxTarget,
    pub(crate) starlark: Option<StarlarkTarget>,
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
}
