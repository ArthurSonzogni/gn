// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::Package;

/// &PackageRef is to Package as &str is to String.
#[repr(transparent)]
#[derive(Debug, Eq, PartialEq, Hash)]
pub struct PackageRef(str);

impl PackageRef {
    // Creates a PackageRef for the root package "//"
    pub fn root() -> &'static Self {
        // Safety: "//" is a valid package.
        unsafe { Self::new_unchecked("//") }
    }

    /// Creates a new `PackageRef` for testing purposes.
    /// Not #[cfg(test)] because we use it in tests for downstream crates.
    pub fn new_for_testing(s: &str) -> &Self {
        assert!(s.starts_with("//"), "Package name must start with //");
        // Safety: checked above
        unsafe { Self::new_unchecked(s) }
    }

    /// Creates a new `PackageRef` from a string slice.
    ///
    /// Requires that the string starts with "//"
    pub unsafe fn new_unchecked(s: &str) -> &Self {
        debug_assert!(s.starts_with("//"), "Package name must start with //");
        // Safety: PackageRef is #[repr(transparent)] wrapping str, so their memory layouts are identical.
        unsafe { &*(s as *const str as *const PackageRef) }
    }

    /// Returns the full package name, eg. "//foo/bar"
    pub fn as_str(&self) -> &str {
        &self.0
    }

    /// Returns the path to the package relative to the root source dir
    /// (e.g., "//foo/bar" => "foo/bar")
    pub fn as_source_relative(&self) -> &str {
        &self.0[2..]
    }

    /// Returns true if this is the root package ("//").
    pub fn is_root(&self) -> bool {
        // We have an invariant that all packages must start with "//"
        self.0.len() == 2
    }
}

impl std::fmt::Display for PackageRef {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", &self.0)
    }
}

impl ToOwned for PackageRef {
    type Owned = Package;
    fn to_owned(&self) -> Self::Owned {
        Package(self.0.to_owned())
    }
}
