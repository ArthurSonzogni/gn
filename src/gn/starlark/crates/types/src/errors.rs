// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use starlark::values::UnpackValueError;

use crate::Package;

/// Errors returned by this crate.
#[derive(thiserror::Error, Debug)]
pub(crate) enum Error {
    /// The string is not a valid label (e.g. doesn't start with "//" or ":").
    #[error("Not a label: {0}")]
    NotALabel(String),
    /// An absolute label is invalid (e.g. missing a colon, or has too many
    /// colons).
    #[error("Invalid absolute label, must contain exactly one colon: {0}")]
    InvalidAbsoluteLabel(String),
    /// A relative label contains a colon (which is invalid).
    #[error("Relative label cannot contain a colon: {0}")]
    ColonInRelativeLabel(String),
    /// The referenced file does not exist on disk.
    #[error("File {1} does not exist in {0}")]
    FileNotFound(Package, String),
    #[error("Invalid package, must start with \"//\": \"{0}\"")]
    NotAPackage(String),
    /// The output file was not declared by this target.
    #[error("Output file '{0}' was not declared by this target")]
    OutputNotDeclaredByTarget(crate::File),
    /// The output file has already been generated.
    #[error("Output file '{0}' has already been generated")]
    OutputAlreadyGenerated(crate::File),
    /// Declared file name is unsupported.
    #[error("Invalid ctx.actions.declare_file filename: {0:?}")]
    UnsupportedFilename(String),
    /// The output file was already declared.
    #[error("Output file '{0}' was already declared")]
    DuplicateDeclaredOutput(crate::File),
    /// Declared output file was never generated.
    #[error("Declared output file '{0}' was never generated")]
    DeclaredOutputNeverGenerated(crate::File),
}

impl From<Error> for starlark::Error {
    fn from(err: Error) -> Self {
        Self::new_other(err)
    }
}

impl UnpackValueError for Error {
    fn into_error(this: Self) -> starlark::Error {
        starlark::Error::new_other(this)
    }
}
