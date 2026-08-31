// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use allocative::Allocative;
use strum::{Display, EnumIter, IntoStaticStr};

#[repr(u8)]
#[derive(Debug, Clone, Copy, PartialEq, Eq, EnumIter, Display, IntoStaticStr, Allocative)]
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

    /// Returns (attr-and-files attributes, attr-only attributes).
    ///
    /// Any attributes that should fill ctx.files.* should go in the former.
    /// Other attributes should go in the latter.
    pub fn attrs(&self) -> (&'static [&'static str], &'static [&'static str]) {
        match self {
            Self::Executable
            | Self::SharedLibrary
            | Self::LoadableModule
            | Self::StaticLibrary
            | Self::SourceSet => (&["sources", "public"], &["deps", "public_deps"]),
            _ => (&[], &[]),
        }
    }
}
