// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use allocative::Allocative;
use strum::{Display, EnumIter, IntoStaticStr};

#[repr(u8)]
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, EnumIter, Display, IntoStaticStr, Allocative)]
#[strum(serialize_all = "snake_case")]
/// Copied from target.h's OutputType enum.
pub enum OutputType {
    // Unknown = 0
    Group = 1,
    Executable,
    SharedLibrary,
    LoadableModule,
    StaticLibrary,
    SourceSet,
    Copy,
    Action,
    ActionForeach,
    BundleData,
    CreateBundle,
    GeneratedFile,
    RustLibrary,
    RustProcMacro,
    // NOOP = 15,
}

/// Compile-time string equality test
const fn str_eq(a: &str, b: &str) -> bool {
    let (a, b) = (a.as_bytes(), b.as_bytes());
    if a.len() != b.len() {
        return false;
    }
    let mut i = 0;
    while i < a.len() && a[i] == b[i] {
        i += 1;
    }
    i == a.len()
}

/// Compile-time check if slice contains a string.
const fn contains(slice: &[&'static str], needle: &str) -> bool {
    let mut i = 0;
    while i < slice.len() {
        if str_eq(slice[i], needle) {
            return true;
        }
        i += 1;
    }
    false
}

/// Field names associated with a built-in GN target type.
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct BuiltinFieldNames {
    /// Built-in attributes populated only in `ctx.attr`.
    pub attr_only: &'static [&'static str],
    /// Built-in attributes populated in both `ctx.attr` and `ctx.files`.
    pub file_and_attr: &'static [&'static str],
    /// Native GN attributes forwarded as kwargs rather than exposed on `ctx`,
    /// reserved so Starlark rules cannot shadow them.
    pub reserved_fields: &'static [&'static str],
}

impl BuiltinFieldNames {
    /// Returns `true` if `name` is handled or reserved by this built-in target type.
    pub const fn is_reserved(&self, name: &str) -> bool {
        contains(self.attr_only, name)
            || contains(self.file_and_attr, name)
            || contains(self.reserved_fields, name)
    }
}

/// All native GN target attributes across all target types (excluding
/// `testonly` and `visibility`, which are reserved for all rules).
const RESERVED_FIELDS: &[&str] = &[
    "aliased_deps",
    "all_dependent_configs",
    "allow_circular_includes_from",
    "arflags",
    "args",
    "asmflags",
    "assert_no_deps",
    "bridge_header",
    "bundle_contents_dir",
    "bundle_deps_filter",
    "bundle_executable_dir",
    "bundle_resources_dir",
    "bundle_root_dir",
    "c_additional_outputs",
    "cflags",
    "cflags_c",
    "cflags_cc",
    "cflags_objc",
    "cflags_objcc",
    "check_includes",
    "check_includes_strict",
    "collect_validations_metadata",
    "complete_static_lib",
    "configs",
    "contents",
    "crate_name",
    "crate_root",
    "crate_type",
    "data",
    "data_deps",
    "data_keys",
    "datadeps",
    "defines",
    "depfile",
    "deps",
    "externs",
    "framework_dirs",
    "frameworks",
    "friend",
    "gen_deps",
    "generate_modulemap",
    "include_dirs",
    "inputs",
    "ldflags",
    "lib_dirs",
    "libs",
    "metadata",
    "mnemonic",
    "module_name",
    "output_conversion",
    "output_dir",
    "output_extension",
    "output_name",
    "output_prefix_override",
    "outputs",
    "partial_info_plist",
    "pool",
    "post_processing_args",
    "post_processing_manifest",
    "post_processing_outputs",
    "post_processing_script",
    "post_processing_sources",
    "precompiled_header",
    "precompiled_source",
    "product_type",
    "public",
    "public_configs",
    "public_deps",
    "public_inputs",
    "rebase",
    "response_file_contents",
    "rustenv",
    "rustflags",
    "script",
    "sources",
    "swiftflags",
    "transparent",
    "validations",
    "walk_keys",
    "weak_frameworks",
    "weak_libraries",
    "write_runtime_deps",
    "xcasset_compiler_flags",
    "xcode_extra_attributes",
    "xcode_test_application_name",
];

impl OutputType {
    const MAX: u8 = Self::RustProcMacro as u8;
    const NOOP: u8 = 15;

    /// Converts a u8 discriminant from GN C++ Target::OutputType to
    /// `Option<OutputType>`.
    pub fn from_u8(value: u8) -> Option<Self> {
        match value {
            0 | Self::NOOP => None,
            // Safety: OutputType is #[repr(u8)] and contiguous for discriminants 1..=Self::MAX.
            1..=Self::MAX => Some(unsafe { std::mem::transmute::<u8, Self>(value) }),
            _ => panic!("Invalid output type: {value}"),
        }
    }

    /// Returns the built-in and reserved field names for this target type.
    ///
    /// Any attributes that should fill `ctx.files.*` go in `file_and_attr`;
    /// other attributes exposed on `ctx.attr.*` go in `attr_only`; remaining
    /// native GN attributes go in `reserved_fields`.
    pub const fn attrs(self) -> BuiltinFieldNames {
        match self {
            Self::Executable
            | Self::SharedLibrary
            | Self::LoadableModule
            | Self::StaticLibrary
            | Self::SourceSet => BuiltinFieldNames {
                file_and_attr: &["sources", "public"],
                attr_only: &["deps", "public_deps"],
                reserved_fields: RESERVED_FIELDS,
            },
            _ => BuiltinFieldNames {
                file_and_attr: &[],
                attr_only: &[],
                reserved_fields: RESERVED_FIELDS,
            },
        }
    }

    pub const fn has_sources(self) -> bool {
        contains(self.attrs().file_and_attr, "sources")
    }

    pub const fn has_public(self) -> bool {
        contains(self.attrs().file_and_attr, "public")
    }

    pub const fn has_deps(self) -> bool {
        contains(self.attrs().attr_only, "deps")
    }

    pub const fn has_public_deps(self) -> bool {
        contains(self.attrs().attr_only, "public_deps")
    }
}

impl From<OutputType> for usize {
    fn from(value: OutputType) -> Self {
        value as Self
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_has_capabilities() {
        assert!(OutputType::Executable.has_deps());
        assert!(!OutputType::Copy.has_deps());
    }

    #[test]
    fn test_is_reserved() {
        assert!(OutputType::Executable.attrs().is_reserved("deps"));
        assert!(OutputType::Executable.attrs().is_reserved("sources"));
        assert!(OutputType::Executable.attrs().is_reserved("cflags"));
        assert!(!OutputType::Executable.attrs().is_reserved("custom_attr"));

        assert!(OutputType::Group.attrs().is_reserved("deps"));
        assert!(OutputType::Group.attrs().is_reserved("public_inputs"));
        assert!(!OutputType::Group.attrs().is_reserved("custom_attr"));

        assert!(OutputType::Copy.attrs().is_reserved("sources"));
        assert!(OutputType::Copy.attrs().is_reserved("outputs"));

        assert!(OutputType::RustLibrary.attrs().is_reserved("sources"));
        assert!(OutputType::RustLibrary.attrs().is_reserved("crate_root"));
    }
}
