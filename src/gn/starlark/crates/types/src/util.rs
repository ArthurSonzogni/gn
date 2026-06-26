// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Like std::mem::transmute, but can only affect lifetime.
pub unsafe fn extend_lifetime<'to, 'from, T: ?Sized>(val: &'from T) -> &'to T {
    // Safety: None - this function is marked unsafe.
    unsafe { std::mem::transmute(val) }
}
