// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{cell::RefCell, collections::HashSet};

use attr::Session;
use types::{Label, LabelRef, PackageRef, TargetRef as _, util::extend_lifetime};

use crate::{FakeTarget, FakeTargetRef};

/// A fake implementation of the `Session` trait for testing.
pub struct FakeSession {
    /// The preconfigured default toolchain label.
    pub default_toolchain: Label,
    /// A set of fake targets populated for testing.
    pub targets: RefCell<HashSet<FakeTargetRef>>,
    /// Temporary storage for targets being constructed.
    pub targets_under_construction: RefCell<Vec<Box<FakeTarget>>>,
    /// Builtin rules registered for the session.
    builtin_rules: Vec<&'static rule::Rule<'static, crate::FakeEvalContext>>,
    /// The module associated with the default rules.
    /// Never used, just an RAII guard for builtin_rules.
    _module: starlark::environment::FrozenModule,
}

impl Default for FakeSession {
    fn default() -> Self {
        Self::new()
    }
}

impl FakeSession {
    /// Creates a new `FakeSession` instance with empty targets and a
    /// preconfigured default toolchain.
    pub fn new() -> Self {
        let module = rule::register_builtin_rules::<crate::FakeEvalContext>();
        let this = Self {
            default_toolchain: Label::new(
                PackageRef::root().to_owned(),
                "default_toolchain".to_owned(),
            ),
            targets: RefCell::new(HashSet::new()),
            targets_under_construction: RefCell::new(Vec::new()),
            // Safety: The module lives as long as the session lasts, which is as long as the test
            // lasts, which is 'static.
            builtin_rules: rule::extract_builtin_rules(unsafe { extend_lifetime(&module) }),
            _module: module,
        };
        this.insert_empty_target(PackageRef::root(), "default");
        this
    }

    pub fn default_target(&self) -> FakeTargetRef {
        self.get_target(
            LabelRef::new(PackageRef::root(), "default"),
            self.default_toolchain.as_ref(),
        )
    }

    /// Helper to insert a target.
    pub fn insert_target(&self, target: FakeTarget) -> FakeTargetRef {
        let target_ref = FakeTargetRef::new(target);
        let mut targets = self.targets.borrow_mut();
        assert!(
            targets.insert(target_ref.clone()),
            "Inserting an already existing target into the map"
        );
        target_ref
    }

    /// Returns the default rule used for testing.
    pub fn default_rule(&self) -> &'static rule::Rule<'static, crate::FakeEvalContext> {
        self.builtin_rules[types::OutputType::SourceSet as usize]
    }

    /// Helper to create an empty target with the default toolchain.
    pub fn empty_target(&self, package: &PackageRef, name: &str) -> FakeTarget {
        FakeTarget {
            label: LabelRef::new(package, name).to_owned(),
            toolchain: self.default_toolchain.clone(),
            outputs: Default::default(),
            attrs: Default::default(),
            output_type: Default::default(),
            rule: self.default_rule(),
            cxx_attrs: Default::default(),
            dependencies: Default::default(),
        }
    }

    /// Helper to insert an empty target.
    pub fn insert_empty_target(&self, package: &PackageRef, name: &str) -> FakeTargetRef {
        self.insert_target(self.empty_target(package, name))
    }
}

impl Session for FakeSession {
    type TargetRef = FakeTargetRef;

    fn get_target(&self, label: LabelRef<'_>, current_toolchain: LabelRef<'_>) -> Self::TargetRef {
        let targets = self.targets.borrow();
        if let Some(target) = targets
            .iter()
            .find(|target| target.label() == label && target.toolchain() == current_toolchain)
        {
            target.clone()
        } else {
            panic!(
                "get_target failed to find label: {label:?}, toolchain: {current_toolchain:?}. Available targets: {targets:?}"
            );
        }
    }

    fn run_builtin_rule<'v>(
        &self,
        heap: &starlark::values::Heap<'v>,
        _target: &Self::TargetRef,
        _state: &mut types::CtxState<Self::TargetRef>,
    ) -> starlark::values::Value<'v> {
        heap.alloc(Vec::<starlark::values::Value<'v>>::new())
    }
}
