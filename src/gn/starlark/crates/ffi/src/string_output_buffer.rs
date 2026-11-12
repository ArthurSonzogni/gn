// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{fmt, pin::Pin};

use types::{Cursor, Writable};

use crate::bridge;

/// Safe wrapper around a C++ `StringOutputBuffer`.
pub struct StringOutputBuffer<'a> {
    inner: Pin<&'a mut bridge::StringOutputBuffer>,
}

impl<'a> StringOutputBuffer<'a> {
    /// Creates a new wrapper around a pinned C++ `StringOutputBuffer`.
    pub fn new(inner: Pin<&'a mut bridge::StringOutputBuffer>) -> Self {
        Self { inner }
    }

    /// Reserves at least `additional` bytes in the buffer and returns a mutable
    /// reference to the write cursor pointer.
    ///
    /// # Safety
    ///
    /// The caller is responsible for ensuring that writes do not exceed `additional`
    /// bytes and that `*cursor` is properly advanced to reflect written bytes.
    pub unsafe fn reserve(&mut self, additional: usize) -> &mut Cursor {
        // Safety: Reserve guarantees the write cursor is non-null and has at least `additional`
        // capacity.
        unsafe { &mut *bridge::Reserve(self.inner.as_mut(), additional).cast::<Cursor>() }
    }

    /// Appends a `Writable` value into the buffer.
    pub fn append<W: Writable>(&mut self, val: W) {
        // Safety: maximum_size should always be >= #bytes written to the buffer.
        let cur = unsafe { self.reserve(val.maximum_size()) };
        // Safety: Total capacity was reserved for `val`.
        *cur = unsafe { val.write_to_buf(*cur) };
    }
}

impl<'a> fmt::Display for StringOutputBuffer<'a> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&bridge::StringOutputBufferToString(&self.inner))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_string_output_buffer() {
        let mut raw_buf = bridge::NewStringOutputBuffer();
        let mut buffer = StringOutputBuffer::new(raw_buf.pin_mut());
        buffer.append("hello");
        buffer.append((" gn", " user", b'\n'));
        assert_eq!(buffer.to_string(), "hello gn user\n");
    }

    #[test]
    fn test_string_output_buffer_u32() {
        let mut raw_buf = bridge::NewStringOutputBuffer();
        let mut buffer = StringOutputBuffer::new(raw_buf.pin_mut());
        buffer.append(0u32);
        buffer.append((" ", 7u32, " ", 42u32, " ", u32::MAX));
        assert_eq!(buffer.to_string(), "0 7 42 4294967295");
    }
}
