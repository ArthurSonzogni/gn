// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use allocative::Allocative;
use either::Either;
use starlark::{
    any::ProvidesStaticType,
    values::{StringValue, Trace},
};
use starlark_derive::Freeze;
use types::File;

/// An action registered by `ctx.actions.run` during rule evaluation.
#[derive(Allocative, Debug, Freeze, ProvidesStaticType, Trace)]
pub struct Action<'v> {
    /// The output files produced by this action.
    pub outputs: Vec<File>,
    /// Direct input files, tools, and executable required by this action.
    pub inputs: Vec<File>,
    /// Executable binary or path to run.
    pub executable: Either<File, StringValue<'v>>,
    /// Command-line arguments.
    pub arguments: args::ArgsList<'v>,
    /// Short identifier for the action.
    pub mnemonic: Option<StringValue<'v>>,
}
