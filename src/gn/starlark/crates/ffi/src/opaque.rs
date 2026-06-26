// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Declares a C++ type that is opaque to rust.
///
/// | C++ Type            | Rust Type                    |
/// |---------------------|------------------------------|
/// | `const OpaqueType&` | `&'a OpaqueType`             |
/// | `OpaqueType&`       | `&'a mut OpaqueType`         |
/// | `const OpaqueType*` | `Option<&'a OpaqueType>`     |
/// | `OpaqueType*`       | `Option<&'a mut OpaqueType>` |
/// | `OpaqueType`        | **DO NOT USE**               |
#[macro_export]
macro_rules! declare_opaque_type {
    ($name:ident) => {
        $crate::declare_opaque_type!(pub $name);
    };
    ($vis:vis $name:ident) => {
        #[repr(C)]
        $vis struct $name {
            // Private member prevents external construction or instantiation by-value.
            // Non-zero size (1 byte) prevents Rust from optimizing references as ZSTs.
            _private: u8,
        }
    };
}
