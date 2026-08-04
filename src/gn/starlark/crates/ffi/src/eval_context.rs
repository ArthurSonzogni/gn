// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use allocative::Allocative;
use starlark::values::ProvidesStaticType;
use types::{LabelRef, PackageRef, PathResolver};

use crate::{errors::Error, Scope};

enum EvalContextKind {
    BzlFile,
    Macro(&'static Scope),
}

#[derive(Allocative, ProvidesStaticType)]
pub struct EvalContext {
    #[allocative(skip)]
    session: &'static crate::session::Session,
    #[allocative(skip)]
    package: &'static PackageRef,
    #[allocative(skip)]
    kind: EvalContextKind,
}

impl EvalContext {
    pub fn new_bzl_file(
        session: &'static crate::session::Session,
        package: &'static PackageRef,
    ) -> Self {
        Self {
            session,
            package,
            kind: EvalContextKind::BzlFile,
        }
    }

    pub fn new_macro(
        session: &'static crate::session::Session,
        package: &'static PackageRef,
        scope: &'static Scope,
    ) -> Self {
        Self {
            session,
            package,
            kind: EvalContextKind::Macro(scope),
        }
    }
}

impl types::EvalContext for EvalContext {
    type Scope = crate::Scope;
    type Session = crate::session::Session;

    fn current_package(&self) -> &types::PackageRef {
        self.package
    }

    fn path_resolver(&self) -> &PathResolver {
        &self.session.path_resolver
    }

    fn session(&self) -> &Self::Session {
        self.session
    }

    fn current_toolchain(&self) -> LabelRef<'_> {
        todo!()
    }

    fn require_macro(&self) -> starlark::Result<&Self::Scope> {
        match &self.kind {
            EvalContextKind::Macro(scope) => Ok(*scope),
            _ => Err(Error::RequiresMacro.into()),
        }
    }

    fn require_bzl(&self) -> starlark::Result<()> {
        matches!(self.kind, EvalContextKind::BzlFile)
            .then_some(())
            .ok_or_else(|| Error::RequiresBzlFile.into())
    }

    fn require_rule_impl(
        &self,
    ) -> starlark::Result<&mut types::CtxState<crate::target_ref::TargetRef>> {
        todo!()
    }
}

impl attr::traits::EvalContextAttrExt for EvalContext {
    fn create_target(
        &self,
        _target_type: Option<types::OutputType>,
        _target_name: &str,
        _scope: &Scope,
        _rule: starlark::values::FrozenValue,
        _attrs: Vec<attr::Attr>,
    ) -> starlark::Result<<Self::Session as types::Session>::TargetRef> {
        todo!("Create C++ target and register dependencies");
    }
}
