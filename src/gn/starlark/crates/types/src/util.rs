// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Like std::mem::transmute, but can only affect lifetime.
///
/// # Safety
///
/// The caller must ensure that the returned reference is not used after the
/// underlying data is dropped.
pub unsafe fn extend_lifetime<'to, T: ?Sized>(val: &T) -> &'to T {
    // Safety: Transmuting lifetime of reference is unsafe, safety is guaranteed by
    // the caller.
    unsafe { std::mem::transmute(val) }
}

/// De-duplicates elements in a small vector in-place while preserving relative order.
///
/// This performs an O(N^2) linear scan, which is faster than allocating a
/// hash set for small vectors.
pub fn uniquify_small_vec<T: PartialEq>(vec: &mut Vec<T>) {
    let mut write = 0;
    for read in 0..vec.len() {
        if !vec[..write].contains(&vec[read]) {
            if write != read {
                vec.swap(write, read);
            }
            write += 1;
        }
    }
    vec.truncate(write);
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_uniquify_empty() {
        let mut vec: Vec<i32> = Vec::new();
        uniquify_small_vec(&mut vec);
        assert!(vec.is_empty());
    }

    #[test]
    fn test_uniquify_already_unique() {
        let mut vec = vec![10, 20, 30];
        uniquify_small_vec(&mut vec);
        assert_eq!(vec, [10, 20, 30]);
    }

    #[test]
    fn test_uniquify_duplicates_preserve_order() {
        let mut vec = vec![10, 20, 10, 30, 20, 40];
        uniquify_small_vec(&mut vec);
        assert_eq!(vec, [10, 20, 30, 40]);
    }
}
