// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::declare_opaque_type;
use types::{LabelRef, PackageRef};

declare_opaque_type!(pub Label);

impl Label {
    /// Returns the directory part of the label (the package path).
    pub fn package(&self) -> &PackageRef {
        extern "C" {
            fn GetLabelDir(label: &Label) -> &str;
        }
        // Safety: GetLabelDir is guarunteed to return a string that starts with "//".
        unsafe { PackageRef::new_unchecked(GetLabelDir(self)) }
    }

    /// Returns the name part of the label.
    pub fn name(&self) -> &str {
        extern "C" {
            fn GetLabelName(label: &Label) -> &str;
        }
        unsafe { GetLabelName(self) }
    }

    /// Returns a `LabelRef` referencing the directory and name of this label.
    pub fn as_ref<'a>(&'a self) -> LabelRef<'a> {
        LabelRef {
            package: self.package(),
            name: self.name(),
        }
    }
}
