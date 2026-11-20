// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use allocative::Allocative;
use starlark_derive::Freeze;

use crate::PackageRef;

/// A package, conceptually, is a directory in the source tree.
/// Always prefer &PackageRef over Package to avoid copies.
///
/// It represents the same concept as a SourceDir, but differs in its internal
/// representation: unlike SourceDir, it *never* ends with a '/'.
///
/// A package *always* starts with //, and may be either the root package "//",
/// or a subdirectory "//foo/bar".
#[derive(Allocative, Clone, Debug, Eq, Freeze, Hash, PartialEq)]
pub struct Package(pub(crate) String);

impl Package {
    /// Creates a `Package` from a source-relative directory path (e.g.
    /// `"foo/bar"` -> `"//foo/bar"`, `""` -> `"//"`).
    pub fn from_source_relative(s: &str) -> Self {
        debug_assert!(!s.starts_with('/') && !s.ends_with('/'));
        Self(format!("//{s}"))
    }
}

impl std::ops::Deref for Package {
    type Target = PackageRef;

    fn deref(&self) -> &Self::Target {
        // Safety: already validated
        unsafe { PackageRef::new_unchecked(&self.0) }
    }
}

impl AsRef<PackageRef> for Package {
    fn as_ref(&self) -> &PackageRef {
        self
    }
}

impl std::borrow::Borrow<PackageRef> for Package {
    fn borrow(&self) -> &PackageRef {
        self
    }
}

impl std::fmt::Display for Package {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.as_str())
    }
}
