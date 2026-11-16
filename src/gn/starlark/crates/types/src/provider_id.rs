// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::sync::atomic::{AtomicU64, Ordering};

use allocative::Allocative;
use starlark::values::{Freeze, Trace};

/// A unique identifier for a provider type within a GN build session.
#[derive(Copy, Clone, Debug, Hash, PartialEq, Eq, PartialOrd, Ord, Allocative, Freeze, Trace)]
pub struct ProviderId(u64);

impl ProviderId {
    /// Generates a new unique `ProviderId`.
    ///
    /// A process-wide counter is fine here because all we care about is that
    /// these type IDs are distinct. Provider IDs are also not persisted
    /// anywhere so we don't have nondeterminism concerns.
    pub fn next() -> Self {
        // Built-in provider IDs are u8s, so starting at 256 is safe.
        static NEXT_ID: AtomicU64 = AtomicU64::new(256);
        Self(NEXT_ID.fetch_add(1, Ordering::Relaxed))
    }

    /// Constant constructor for built-in providers.
    pub const fn builtin(id: u8) -> Self {
        Self(id as u64)
    }
}
