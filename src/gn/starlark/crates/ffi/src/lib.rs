// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Low-level FFI bindings and opaque types for interoperating between the C++ GN
//! codebase and the Rust Starlark interpreter crates.
//!
//! **Type Equivalence**
//!
//! WARNING: We have no way of checking that the C++ type signatures match the
//! rust type signatures. This means that if you change the signature of a
//! function on one side, you *might* get a link error, or might just get UB.
//!
//! As long as C++ and rust see a type identically, we don't care how it works under the hood.
//! * Most custom types will need to be passed by reference
//!   * C++ &T is just a non-null pointer.
//!   * Rust references are just non-null pointers
//!   * Rust Option<&T> are pointers
//! * If you define a struct in C++ and a #[repr(C)] one in rust, you can pass by value instead of by reference.
//!   * cxx.h does this for some rust types in C++ (rust::*, eg. rust::Str = &str)
//!   * cxx.h does this for some C++ types in rust (cxx::*, eg. cxx::CxxVector<T> = std::vector<T>)
//!
//! **Exposing rust functions to C++**
//!
//! Declare a function `#[no_mangle] pub extern "C"` in rust
//! ```rust
//! // foo.rs
//! #[no_mangle]
//! pub extern "C" fn my_function(foo: &Foo) -> &str {
//!     foo.name()
//! }
//! ```
//!
//! Declare the C++ function for this in a header file.
//! A clean C++ API has not yet been implemented.
//!
//! **Exposing C++ functions to rust**
//!
//! Define an `extern "C"` function in C++. Do not define a header file, as it
//! should not be included from C++ code.
//! ```cpp
//! extern "C" rust::Str GetLabelName(const Label& label) {
//!   return rust::Str(label.name());
//! }
//! ```
//!
//! Declare the equivalent extern "C" function in rust. Do so inside a function
//! definition, as the canonical way to access this function *safely*.
//! For example:
//! ```rust
//! declare_opaque_type!(Label);
//! impl Label {
//!     fn name(&self) -> &str {
//!         extern "C" {
//!             fn GetLabelName(label: &Label) -> &str;
//!         }
//!         unsafe { GetLabelName(self) }
//!     }
//! }
//! ```

pub mod label;
pub mod opaque;

pub use label::Label;
