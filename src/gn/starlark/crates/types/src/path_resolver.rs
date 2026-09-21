// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::path::PathBuf;

use normalize_path::NormalizePath as _;

use crate::{File, PackageRef};

/// Resolves paths to Files relative to the root_build_dir.
#[derive(Clone, Debug)]
pub struct PathResolver {
    /// Absolute path to the root source dir on disk.
    source_root: PathBuf,
    /// Absolute path to the root build dir on disk.
    build_root: PathBuf,
    /// Path to the root source dir relative to the root_build_dir.
    /// *Must* end with a trailing slash.
    source_root_rel: String,
}

impl PathResolver {
    /// Creates a new `PathResolver` with the given absolute and relative root
    /// paths.
    pub fn new(source_root: PathBuf, build_root: PathBuf, source_root_rel: String) -> Self {
        assert!(source_root_rel.ends_with('/'));
        Self {
            source_root: source_root.normalize(),
            build_root: build_root.normalize(),
            source_root_rel,
        }
    }

    /// Creates a new PathResolver preconfigured for the starlark testdata
    /// directory.
    pub fn new_for_testing() -> Self {
        let src = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../src/testdata");
        Self::new(src.clone(), src.join("out/Default"), "../../".to_string())
    }

    /// Resolves an output file to its absolute on-disk path.
    /// The caller is responsible for ensuring that file is in the output
    /// directory.
    pub fn resolve(&self, file: &File) -> PathBuf {
        let joined = self.build_root.join(file.as_path());
        debug_assert!(joined.normalize().starts_with(&self.build_root));
        joined
    }

    /// Calculates where the file should exist on disk.
    pub fn absolute_path(&self, pkg: &PackageRef, s: &str) -> PathBuf {
        self.source_root.join(pkg.as_source_relative()).join(s)
    }

    /// Creates a `File` object for a file path relative to a package.
    pub fn source_file(&self, pkg: &PackageRef, s: &str) -> File {
        let pkg_dir = pkg.as_source_relative();
        let rel_path = if pkg.is_root() {
            format!("{}{s}", self.source_root_rel)
        } else {
            format!("{}{pkg_dir}/{s}", self.source_root_rel)
        };
        File::intern(&rel_path)
    }

    /// Creates a `File` object for a file path relative to a package.
    /// Validates that the file exists on disk.
    pub fn existing_source_file(&self, pkg: &PackageRef, s: &str) -> starlark::Result<File> {
        // Gn *does not* check that the files you refer to exist on disk.
        // This is because native GN allows referencing generated files that do
        // not yet exist at `gn gen` time.
        // We explicitly disallow referencing generated files in starlark, so
        // we should validate that the files exist on disk for correctness.
        if self.absolute_path(pkg, s).exists() {
            Ok(self.source_file(pkg, s))
        } else {
            Err(crate::Error::FileNotFound(pkg.to_owned(), s.to_owned()).into())
        }
    }
}
