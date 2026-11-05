// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use allocative::Allocative;
use starlark::{
    any::ProvidesStaticType,
    typing::Ty,
    values::{Freeze, StringValue, Trace, UnpackValue, Value, type_repr::StarlarkTypeRepr},
};

use crate::errors::Error;

/// Represents a format string such as a%sb.
/// It must contain exactly one %s, and literal percents may be escaped as '%%'.
#[derive(Allocative, Clone, Debug, Freeze, ProvidesStaticType, Trace)]
pub enum Formatter<'v> {
    /// When there is no escaping, we store the reference to the source string, and the index of the
    /// %s.
    Ref {
        template: StringValue<'v>,
        split_idx: usize,
    },
    /// When escaping is involved, we store escaped copies.
    Copy {
        before: String,
        after: Option<String>,
    },
}

impl<'v> Formatter<'v> {
    /// Parses a format string from a Starlark string value and returns a `Formatter` if valid
    /// (must contain exactly one `%s`). Literal percents may be escaped as `%%`.
    pub fn new(fmt: StringValue<'v>) -> starlark::Result<Self> {
        let mut chars = fmt.as_str().char_indices();
        let mut needs_escaping = false;
        let mut split_point = None;

        while let Some((index, c)) = chars.next() {
            if c == '%' {
                match (chars.next(), split_point) {
                    (Some((_, '%')), _) => needs_escaping = true,
                    (Some((_, 's')), None) => split_point = Some(index),
                    _ => return Err(Error::InvalidFormatString(fmt.as_str().to_owned()).into()),
                }
            }
        }

        let Some(split_idx) = split_point else {
            return Err(Error::InvalidFormatString(fmt.as_str().to_owned()).into());
        };

        let formatter = Formatter::Ref {
            template: fmt,
            split_idx,
        };

        Ok(if needs_escaping {
            Formatter::Copy {
                before: formatter.before().replace("%%", "%"),
                after: formatter.after().map(|s| s.replace("%%", "%")),
            }
        } else {
            formatter
        })
    }

    pub(crate) fn before(&self) -> &str {
        match self {
            Self::Ref {
                template,
                split_idx,
            } => &template.as_str()[..*split_idx],
            Self::Copy { before, .. } => before,
        }
    }

    // We have this return Option<&str> because it's quite rare and we don't
    // want users to handle it most of the time.
    pub(crate) fn after(&self) -> Option<&str> {
        match self {
            Self::Ref {
                template,
                split_idx,
            } => {
                let after = &template.as_str()[*split_idx + 2..];
                (!after.is_empty()).then_some(after)
            },
            Self::Copy { after, .. } => after.as_deref(),
        }
    }

    /// Formats the string by replacing `%s` with the input string.
    pub fn format(&self, s: &str) -> String {
        format!("{}{}{}", self.before(), s, self.after().unwrap_or_default())
    }
}

impl StarlarkTypeRepr for Formatter<'_> {
    type Canonical = String;

    fn starlark_type_repr() -> Ty {
        String::starlark_type_repr()
    }
}

impl<'v> UnpackValue<'v> for Formatter<'v> {
    type Error = starlark::Error;

    fn unpack_value_impl(value: Value<'v>) -> Result<Option<Self>, Self::Error> {
        let Some(s) = StringValue::new(value) else {
            return Ok(None);
        };
        Self::new(s).map(Some)
    }
}

#[cfg(test)]
mod tests {
    use starlark::environment::Module;

    use super::Formatter;

    // For consistency with bazel, test cases copied directly from:
    // https://github.com/bazelbuild/bazel/blob/master/src/test/java/com/google/devtools/build/lib/actions/SingleStringArgFormatterTest.java
    #[test]
    fn test_valid_formats() {
        Module::with_temp_heap(|module| {
            let cases = [
                ("hello %s", "hello world", true),
                ("%s hello", "world hello", true),
                ("hello %s, hello", "hello world, hello", true),
                ("hello %%s %s", "hello %s world", false),
            ];
            for (fmt, expected, is_slice) in cases {
                let template = module.heap().alloc_str(fmt);
                let f = Formatter::new(template).unwrap();
                assert_eq!(f.format("world"), expected);
                assert_eq!(matches!(f, Formatter::Ref { .. }), is_slice);
            }
        });
    }

    #[test]
    fn test_invalid_formats() {
        Module::with_temp_heap(|module| {
            let cases = [
                "hello",
                "hello %%s",
                "hello %s %s",
                "%s hello %s",
                "hello %",
                "hello %f",
                "hello %s %f",
                "hello %s %",
            ];
            for fmt in cases {
                let template = module.heap().alloc_str(fmt);
                assert!(
                    Formatter::new(template).is_err(),
                    "Expected invalid format: {fmt}"
                );
            }
        });
    }
}
