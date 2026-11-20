// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{collections::HashMap, ops::Range, path::PathBuf, sync::RwLock};

use normalize_path::NormalizePath as _;

use crate::{File, Package, PackageRef};

/// Resolves paths to Files relative to the root_build_dir.
#[derive(Debug)]
pub struct PathResolver {
    /// Absolute path to the root source dir on disk.
    source_root: PathBuf,
    /// Absolute path to the root build dir on disk.
    build_root: PathBuf,
    /// Path to the root source dir relative to the root_build_dir.
    /// *Must* end with a trailing slash.
    source_root_rel: String,
    /// Cache mapping packages to whether their `BUILD.gn` exists on disk.
    package_exists_cache: RwLock<HashMap<Package, bool>>,
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
            package_exists_cache: Default::default(),
        }
    }

    /// Creates a new PathResolver preconfigured for the starlark testdata
    /// directory.
    pub fn new_for_testing() -> Self {
        let src = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../src/testdata");
        Self::new(
            src,
            std::env::temp_dir().join("gn_starlark_test_out"),
            "../../".to_string(),
        )
    }

    /// Returns `true` if `package` contains a `BUILD.gn` file on disk, caching
    /// the result for future lookups.
    fn package_exists(&self, package: &PackageRef) -> bool {
        if let Some(exists) = self
            .package_exists_cache
            .read()
            .unwrap()
            .get(package)
            .copied()
        {
            return exists;
        }
        let exists = self.absolute_path(package, "BUILD.gn").is_file();
        self.package_exists_cache
            .write()
            .unwrap()
            .insert(package.to_owned(), exists);
        exists
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

    /// Creates a `File` object for a file path relative to a package along with
    /// the byte range of the source-relative package directory inside `File::as_str()`.
    /// Validates that the file is a normalized path within the package and
    /// exists on disk.
    pub fn existing_source_file(
        &self,
        pkg: &PackageRef,
        s: &str,
    ) -> starlark::Result<(File, Range<usize>)> {
        if !crate::util::is_normalized_relative_path(s) {
            return Err(crate::Error::InvalidSourcePath(pkg.to_owned(), s.to_owned()).into());
        }
        // Match Bazel's package boundary rule: files inside any subdirectory
        // containing a BUILD.gn belong to that subpackage instead of `pkg`.
        for (slash_idx, _) in s.match_indices('/') {
            let subdir = &s[..slash_idx];
            let subpackage = if pkg.is_root() {
                format!("//{subdir}")
            } else {
                format!("{pkg}/{subdir}")
            };
            if self.package_exists(PackageRef::new(&subpackage).unwrap()) {
                return Err(crate::Error::SourceInSubpackage {
                    package: pkg.to_owned(),
                    path: s.to_owned(),
                    subpackage,
                }
                .into());
            }
        }
        // Gn *does not* check that the files you refer to exist on disk.
        // This is because native GN allows referencing generated files that do
        // not yet exist at `gn gen` time.
        // We explicitly disallow referencing generated files in starlark, so
        // we should validate that the files exist on disk for correctness.
        if self.absolute_path(pkg, s).is_file() {
            let pkg_start = self.source_root_rel.len();
            let pkg_end = pkg_start + pkg.as_source_relative().len();
            Ok((self.source_file(pkg, s), pkg_start..pkg_end))
        } else {
            Err(crate::Error::FileNotFound(pkg.to_owned(), s.to_owned()).into())
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_existing_source_file() {
        let root = tempfile::tempdir().unwrap();
        for rel in [
            "BUILD.gn",
            "other/file.cc",
            "pkg/BUILD.gn",
            "pkg/file.cc",
            "pkg/plain/file.cc",
            "pkg/plain/nested/ok.txt",
            "pkg/subpkg/BUILD.gn",
            "pkg/subpkg/direct.txt",
            "pkg/subpkg/nested/deep.txt",
            "root_subpkg/BUILD.gn",
            "root_subpkg/in_root_sub.txt",
        ] {
            let p = root.path().join(rel);
            std::fs::create_dir_all(p.parent().unwrap()).unwrap();
            std::fs::write(p, "").unwrap();
        }

        let resolver = PathResolver::new(
            root.path().to_path_buf(),
            root.path().join("out"),
            "../".to_string(),
        );
        let pkg = PackageRef::new("//pkg").unwrap();
        let subpkg = PackageRef::new("//pkg/subpkg").unwrap();

        let existing_source_file = |pkg, name| -> starlark::Result<(String, String)> {
            let (file, pkg_range) = resolver.existing_source_file(pkg, name)?;
            let s = file.as_str();
            Ok((s.to_owned(), s[pkg_range].to_owned()))
        };

        // Files in the package itself and non-subpackage directories are allowed.
        assert_eq!(
            existing_source_file(PackageRef::root(), "other/file.cc").unwrap(),
            ("../other/file.cc".to_owned(), "".to_owned())
        );
        assert_eq!(
            existing_source_file(pkg, "file.cc").unwrap(),
            ("../pkg/file.cc".to_owned(), "pkg".to_owned())
        );
        assert_eq!(
            existing_source_file(pkg, "plain/file.cc").unwrap(),
            ("../pkg/plain/file.cc".to_owned(), "pkg".to_owned())
        );
        assert_eq!(
            existing_source_file(pkg, "plain/nested/ok.txt").unwrap(),
            ("../pkg/plain/nested/ok.txt".to_owned(), "pkg".to_owned())
        );

        // Files inside a subpackage are allowed only when resolved from that subpackage.
        assert_eq!(
            existing_source_file(subpkg, "direct.txt").unwrap(),
            (
                "../pkg/subpkg/direct.txt".to_owned(),
                "pkg/subpkg".to_owned()
            )
        );
        existing_source_file(PackageRef::root(), "root_subpkg/in_root_sub.txt").unwrap_err();
        existing_source_file(pkg, "subpkg/direct.txt").unwrap_err();
        existing_source_file(pkg, "subpkg/nested/deep.txt").unwrap_err();

        // Reject unnormalized, absolute, directory, missing, and cross-subpackage paths.
        existing_source_file(pkg, "../other/file.cc").unwrap_err();
        existing_source_file(pkg, "plain/../file.cc").unwrap_err();
        existing_source_file(pkg, "./file.cc").unwrap_err();
        existing_source_file(pkg, "plain/./file.cc").unwrap_err();
        existing_source_file(pkg, "/file.cc").unwrap_err();
        existing_source_file(pkg, "//pkg/file.cc").unwrap_err();
        existing_source_file(pkg, "plain\\file.cc").unwrap_err();
        existing_source_file(pkg, "file.cc/").unwrap_err();
        existing_source_file(pkg, "plain/").unwrap_err();
        existing_source_file(pkg, "plain").unwrap_err();
        existing_source_file(pkg, "subpkg").unwrap_err();
        existing_source_file(PackageRef::root(), "root_subpkg").unwrap_err();
        existing_source_file(pkg, "plain//file.cc").unwrap_err();
        existing_source_file(pkg, "nonexistent.cc").unwrap_err();
        existing_source_file(pkg, "").unwrap_err();
    }
}
