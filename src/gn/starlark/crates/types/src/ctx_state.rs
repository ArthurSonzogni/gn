// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::path::{Component, Path};

use starlark::collections::SmallSet;

use crate::{File, TargetRef};

/// The state associated with the `ctx` object during the rule implementation
/// function.
#[derive(Clone, Debug, allocative::Allocative)]
pub struct CtxState<T: TargetRef> {
    /// Reference to the underlying target.
    pub target: T,
    /// All files declared by ctx.actions.declare_file that were never
    /// generated.
    unused_declared_outputs: SmallSet<File>,
    /// All files declared by ctx.actions.declare_file.
    declared_outputs: SmallSet<File>,
    /// A list of phonies declared during this execution step.
    pub phonies: Vec<(File, Vec<File>)>,
}

impl<T: TargetRef> CtxState<T> {
    /// Creates a new `CtxState` for the given target.
    pub fn new(target: T) -> Self {
        Self {
            target,
            unused_declared_outputs: SmallSet::new(),
            declared_outputs: SmallSet::new(),
            phonies: Vec::new(),
        }
    }

    /// Declares a new phony build step in the target's build state.
    pub fn new_phony(&mut self, deps: Vec<File>) -> File {
        let mut path = String::from("phony/");
        if !self.target.is_default_toolchain() {
            path.push_str(self.target.toolchain().name());
            path.push('/');
        }
        let label = self.target.label();
        path.push_str(label.package().as_source_relative());
        path.push(':');
        path.push_str(label.name());
        let count = self.phonies.len();
        path.push('_');
        path.push_str(&count.to_string());
        let phony = File::intern(&path);
        self.phonies.push((phony.clone(), deps));
        phony
    }

    /// Declares a new output file relative to the target's output directory.
    pub fn declare_file(&mut self, name: &str) -> starlark::Result<File> {
        if name.is_empty()
            // Even on windows, for declare_file, we require using / for a path separator.
            || name.contains('\\')
            // Validate that the path is both relative and normalized.
            || !Path::new(name).components().all(|c| matches!(c, Component::Normal(_)))
        {
            return Err(crate::errors::Error::UnsupportedFilename(name.to_owned()).into());
        }
        let mut path = String::new();
        if !self.target.is_default_toolchain() {
            path.push_str(self.target.toolchain().name());
            path.push('/');
        }
        path.push_str("obj/");
        let label = self.target.label();
        path.push_str(label.package().as_source_relative());
        if !path.ends_with('/') {
            path.push('/');
        }
        path.push_str(label.name());
        path.push('/');
        path.push_str(name);
        let file = File::intern(&path);
        if !self.declared_outputs.insert(file.clone()) {
            return Err(crate::errors::Error::DuplicateDeclaredOutput(file).into());
        }
        self.unused_declared_outputs.insert(file.clone());
        Ok(file)
    }

    /// Declares that we are generating a given file.
    pub fn generates_file(&mut self, f: &File) -> starlark::Result<()> {
        if !self.declared_outputs.contains(f) {
            return Err(crate::errors::Error::OutputNotDeclaredByTarget(f.clone()).into());
        }
        if !self.unused_declared_outputs.shift_remove(f) {
            return Err(crate::errors::Error::OutputAlreadyGenerated(f.clone()).into());
        }
        Ok(())
    }

    /// Verifies that all declared outputs were generated.
    pub fn rule_impl_complete(&self) -> starlark::Result<()> {
        match self.unused_declared_outputs.first() {
            Some(unused) => {
                Err(crate::errors::Error::DeclaredOutputNeverGenerated(unused.clone()).into())
            },
            None => Ok(()),
        }
    }
}
