// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{fmt, ops::Range};

use allocative::Allocative;
use starlark::{
    collections::StarlarkHasher,
    starlark_simple_value,
    values::{Heap, ProvidesStaticType, StarlarkValue, Value, ValueError, ValueTyped},
};
use starlark_derive::{Freeze, NoSerialize, Trace, starlark_value};
use types::{File, Label, Package};

use crate::{ProviderType, builtins::DEFAULT_INFO_ID};

/// A Starlark `Target` representing a source file.
///
/// Eg. If //foo/BUILD.gn exists but //foo/bar/BUILD.gn does not,
/// then //foo/bar/baz is the FileTarget //foo:bar/baz
#[derive(Allocative, Clone, Copy, Freeze, NoSerialize, ProvidesStaticType, Trace)]
pub struct FileTarget {
    file: File,
    // file.as_str()[package_start..package_end] represents the package, without the "//" prefix
    package_start: usize,
    package_end: usize,
}

starlark_simple_value!(FileTarget);

impl FileTarget {
    /// Creates a new `FileTarget` from `file` and the byte range of the package
    /// directory inside `file.as_str()`.
    pub fn new(file: File, package: Range<usize>) -> Self {
        Self {
            file,
            package_start: package.start,
            package_end: package.end,
        }
    }

    fn package_relative(&self) -> &str {
        &self.file.as_str()[self.package_start..self.package_end]
    }

    fn target_name(&self) -> &str {
        // Root packages ("//") have an empty package slice right after
        // source_root_rel's trailing '/', whereas non-root packages have a '/'
        // separator at `package_end` before the package-relative path.
        let name_start = if self.package_start == self.package_end {
            self.package_end
        } else {
            self.package_end + 1
        };
        &self.file.as_str()[name_start..]
    }

    /// Generates the `Label` identifying this file target.
    pub fn label(&self) -> Label {
        Label::new(
            Package::from_source_relative(self.package_relative()),
            self.target_name().to_owned(),
        )
    }

    /// Returns the underlying `File`.
    pub fn file(&self) -> File {
        self.file
    }
}

impl PartialEq for FileTarget {
    fn eq(&self, other: &Self) -> bool {
        let eq = self.file == other.file;
        if eq {
            debug_assert!(self.label() == other.label());
        }
        eq
    }
}

impl Eq for FileTarget {}

impl std::hash::Hash for FileTarget {
    fn hash<H: std::hash::Hasher>(&self, state: &mut H) {
        self.file.hash(state);
    }
}

impl fmt::Debug for FileTarget {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        // Delegate to Display, matching TargetRef.
        write!(f, "{self}")
    }
}

impl fmt::Display for FileTarget {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "//{}:{}", self.package_relative(), self.target_name())
    }
}

#[starlark_value(type = "Target")]
impl<'v> StarlarkValue<'v> for FileTarget {
    fn equals(&self, other: Value<'v>) -> starlark::Result<bool> {
        Ok(other
            .downcast_ref::<Self>()
            .is_some_and(|other| other == self))
    }

    fn write_hash(&self, hasher: &mut StarlarkHasher) -> starlark::Result<()> {
        use std::hash::Hash as _;
        self.hash(hasher);
        Ok(())
    }

    fn collect_repr(&self, collector: &mut String) {
        use std::fmt::Write as _;
        write!(collector, "{self:?}").unwrap();
    }

    fn collect_str(&self, collector: &mut String) {
        use std::fmt::Write as _;
        write!(collector, "{self}").unwrap();
    }

    fn at(&self, index: Value<'v>, heap: Heap<'v>) -> starlark::Result<Value<'v>> {
        let Some(provider_type) = ValueTyped::<ProviderType>::new(index) else {
            return ValueError::unsupported_with(self, "[]", index);
        };
        if provider_type.id == DEFAULT_INFO_ID {
            let files = heap.alloc(depset::Depset::single_file(self.file, &heap));
            Ok(crate::builtins::new_defaultinfo(
                provider_type,
                files,
                None,
                &heap,
            ))
        } else {
            Err(crate::Error::MissingProvider(self.label(), index.to_string()).into())
        }
    }

    fn is_in(&self, index: Value<'v>) -> starlark::Result<bool> {
        let Some(provider_type) = index.downcast_ref::<ProviderType>() else {
            return ValueError::unsupported_with(self, "in", index);
        };
        Ok(provider_type.id == DEFAULT_INFO_ID)
    }

    fn dir_attr(&self) -> Vec<String> {
        vec!["label".to_owned()]
    }

    fn get_attr(&self, attribute: &str, heap: Heap<'v>) -> Option<Value<'v>> {
        match attribute {
            "label" => Some(heap.alloc(self.label())),
            _ => None,
        }
    }
}

#[cfg(test)]
mod tests {
    use starlark::assert::Assert;

    use super::*;
    use crate::register_providers;

    #[test]
    fn test_file_target() {
        let mut a = Assert::new();
        a.globals_add(register_providers);
        let other = a.pass("OtherInfo = provider(fields = ['x']); OtherInfo");

        let target = FileTarget::new(File::intern("../foo/sub/bar.txt"), 3..6);
        let root_target = FileTarget::new(File::intern("../root.txt"), 3..3);
        a.globals_add(move |builder| {
            register_providers(builder);
            builder.set("OtherInfo", other.clone());
            builder.set("ft", target);
            builder.set("root_ft", root_target);
        });

        a.eq("str(ft)", r#""//foo:sub/bar.txt""#);
        a.eq("repr(ft)", r#""//foo:sub/bar.txt""#);
        a.eq("str(ft.label)", r#""//foo:sub/bar.txt""#);
        a.eq("ft.label.package", r#""//foo""#);
        a.eq("ft.label.name", r#""sub/bar.txt""#);
        a.eq("dir(ft)", r#"["label"]"#);
        a.is_true("DefaultInfo in ft");
        a.eq(
            "ft[DefaultInfo].files.to_list()[0].path",
            r#""../foo/sub/bar.txt""#,
        );
        a.eq("ft[DefaultInfo].executable", "None");
        a.is_true("ft == ft");
        a.is_false("ft == root_ft");
        a.eq("{ft: True}[ft]", "True");

        a.eq("str(root_ft)", r#""//:root.txt""#);
        a.eq("repr(root_ft)", r#""//:root.txt""#);
        a.eq("root_ft.label.package", r#""//""#);
        a.eq("root_ft.label.name", r#""root.txt""#);

        a.is_false("OtherInfo in ft");
        a.fail(
            "ft[OtherInfo]",
            "Unable to look up non-DefaultInfo providers in file //foo:sub/bar.txt (requested OtherInfo)",
        );
        a.fail("ft['foo']", "not supported for types");
        a.fail("'foo' in ft", "not supported for type");
    }
}
