// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use types::Label;

/// Errors returned by the FFI layer.
#[derive(thiserror::Error, Debug)]
pub(crate) enum Error {
    #[error("Key '{0}' not found in module '{1}'")]
    KeyNotFound(String, Label),
    #[error("Passing a custom starlark type to builtin GN target types is not supported: {0}")]
    PassingNonFrozenStarlarkValueToGn(String),
    #[error("This is not allowed while executing a function loaded from a starlark file.")]
    RequiresBzlFile,
    #[error("This is only allowed while executing a macro (function called from GN).")]
    RequiresMacro,
    #[error("Rules require exactly one string argument for the target name.")]
    RuleRequiresTargetName,
}

impl From<Error> for starlark::Error {
    fn from(err: Error) -> Self {
        Self::new_other(err)
    }
}
