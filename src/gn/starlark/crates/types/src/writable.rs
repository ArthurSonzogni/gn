// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt;

/// A write cursor into the pre-reserved output buffer page.
pub type Cursor = *mut u8;

/// A trait for types that can serialize themselves directly into a pre-reserved
/// output buffer.
///
/// This mirrors the C++ `Writable` concept in `src/gn/string_output_buffer.h`.
///
/// # Safety
///
/// Implementations must guarantee that `write_to_buf` never writes more than
/// `maximum_size()` bytes starting at `cursor`, and returns the advanced cursor pointer.
pub unsafe trait Writable {
    /// Returns the maximum number of bytes this value could write.
    fn maximum_size(&self) -> usize;

    /// Writes the serialized representation starting at `cursor` and returns
    /// the advanced cursor pointer.
    ///
    /// # Safety
    ///
    /// `cursor` must have at least `maximum_size()` bytes of valid writable capacity.
    unsafe fn write_to_buf(&self, cursor: Cursor) -> Cursor;
}

// Safety: `maximum_size` returns `self.len()`, and `write_to_buf` copies exactly
// `self.len()` bytes and returns the pointer advanced by `self.len()`.
unsafe impl Writable for &str {
    fn maximum_size(&self) -> usize {
        self.len()
    }

    unsafe fn write_to_buf(&self, cursor: Cursor) -> Cursor {
        // Safety: Caller guarantees `cursor` has capacity for `self.len()` bytes.
        unsafe {
            std::ptr::copy_nonoverlapping(self.as_ptr(), cursor, self.len());
            cursor.add(self.len())
        }
    }
}

// Safety: `maximum_size` returns 1, and `write_to_buf` writes exactly 1 byte
// and returns the pointer advanced by 1.
unsafe impl Writable for u8 {
    fn maximum_size(&self) -> usize {
        1
    }

    unsafe fn write_to_buf(&self, cursor: Cursor) -> Cursor {
        // Safety: Caller guarantees `cursor` has capacity for 1 byte.
        unsafe {
            cursor.write(*self);
            cursor.add(1)
        }
    }
}

// Safety: `maximum_size` returns 10 (`u32::MAX` has 10 decimal digits), and
// `format_into` produces at most 10 bytes, which `<&str as Writable>::write_to_buf`
// copies and returns the cursor advanced by that length.
unsafe impl Writable for u32 {
    fn maximum_size(&self) -> usize {
        10
    }

    unsafe fn write_to_buf(&self, cursor: Cursor) -> Cursor {
        let mut buf = fmt::NumBuffer::new();
        // Safety: Caller guarantees `cursor` has capacity for at least 10 bytes,
        // which covers any formatted `u32`.
        unsafe { self.format_into(&mut buf).write_to_buf(cursor) }
    }
}

// To write a tuple, we just combine the maximum sizes and then write them all one by one.
macro_rules! impl_writable_for_tuple {
    ($($idx:tt: $T:ident),+) => {
        // Safety: `maximum_size` is the sum of maximum sizes of all elements,
        // and `write_to_buf` writes each element sequentially within that budget.
        unsafe impl<$($T: Writable),+> Writable for ($($T,)+) {
            fn maximum_size(&self) -> usize {
                0 $(+ self.$idx.maximum_size())+
            }

            unsafe fn write_to_buf(&self, mut cursor: Cursor) -> Cursor {
                $(
                    // Safety: Total capacity was reserved for all elements.
                    cursor = unsafe { self.$idx.write_to_buf(cursor) };
                )+
                cursor
            }
        }
    };
}

impl_writable_for_tuple!(0: A);
impl_writable_for_tuple!(0: A, 1: B);
impl_writable_for_tuple!(0: A, 1: B, 2: C);
impl_writable_for_tuple!(0: A, 1: B, 2: C, 3: D);
impl_writable_for_tuple!(0: A, 1: B, 2: C, 3: D, 4: E);
impl_writable_for_tuple!(0: A, 1: B, 2: C, 3: D, 4: E, 5: F);
impl_writable_for_tuple!(0: A, 1: B, 2: C, 3: D, 4: E, 5: F, 6: G);
impl_writable_for_tuple!(0: A, 1: B, 2: C, 3: D, 4: E, 5: F, 6: G, 7: H);
