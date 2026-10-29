// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use types::File;

pub use crate::bridge::EscapingPlatform;
use crate::{Cursor, Writable, bridge};

/// Escapes strings for ninja syntax.
pub struct Ninja<'a>(pub &'a str);

impl From<File> for Ninja<'static> {
    fn from(file: File) -> Self {
        Self(file.as_str())
    }
}

// Safety: `maximum_size` returns `self.0.len() * 2`, which matches the maximum bytes
// written by `bridge::EscapeNinja`.
unsafe impl<'a> Writable for Ninja<'a> {
    fn maximum_size(&self) -> usize {
        self.0.len() * 2
    }

    unsafe fn write_to_buf(&self, cursor: Cursor) -> Cursor {
        // Safety: bridge::EscapeNinja writes at most maximum_size() bytes and advances cursor.
        unsafe { bridge::EscapeNinja(self.0, cursor) }
    }
}

/// Escapes strings for ninja command lines on POSIX.
pub struct NinjaCommandPosix<'a>(pub &'a str);

// Safety: `maximum_size` returns `self.0.len() * 3`, which matches the maximum bytes
// written by `bridge::EscapeNinjaCommandPosix`.
unsafe impl<'a> Writable for NinjaCommandPosix<'a> {
    fn maximum_size(&self) -> usize {
        self.0.len() * 3
    }

    unsafe fn write_to_buf(&self, cursor: Cursor) -> Cursor {
        // Safety: bridge::EscapeNinjaCommandPosix writes at most maximum_size() bytes and advances
        // cursor.
        unsafe { bridge::EscapeNinjaCommandPosix(self.0, cursor) }
    }
}

/// Escapes strings for ninja command lines on Windows.
pub struct NinjaCommandWindows<'a>(pub &'a str);

// Safety: `maximum_size` returns `self.0.len() * 2 + 2`, which matches the maximum bytes
// written by `bridge::EscapeNinjaCommandWindows`.
unsafe impl<'a> Writable for NinjaCommandWindows<'a> {
    fn maximum_size(&self) -> usize {
        self.0.len() * 2 + 2
    }

    unsafe fn write_to_buf(&self, cursor: Cursor) -> Cursor {
        // Safety: bridge::EscapeNinjaCommandWindows writes at most maximum_size() bytes and
        // advances cursor.
        unsafe { bridge::EscapeNinjaCommandWindows(self.0, cursor) }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::StringOutputBuffer;

    #[test]
    fn test_escape_ninja_basic() {
        let mut raw_buf = bridge::NewStringOutputBuffer();
        let mut buffer = StringOutputBuffer::new(raw_buf.pin_mut());
        buffer.append((
            Ninja::from(File::new("foo/bar:my_phony")),
            ": ",
            Ninja::from(File::new("real")),
            b'\n',
        ));
        assert_eq!(buffer.to_string(), "foo/bar$:my_phony: real\n");
    }

    #[test]
    fn test_escape_ninja_command_posix() {
        let mut raw_buf = bridge::NewStringOutputBuffer();
        let mut buffer = StringOutputBuffer::new(raw_buf.pin_mut());
        buffer.append((
            NinjaCommandPosix(File::new("my tool").as_str()),
            " --input=",
            NinjaCommandPosix(File::new("foo bar.cc").as_str()),
            b'\n',
        ));
        assert_eq!(buffer.to_string(), "my\\$ tool --input=foo\\$ bar.cc\n");
    }

    #[test]
    fn test_escape_ninja_command_windows() {
        let mut raw_buf = bridge::NewStringOutputBuffer();
        let mut buffer = StringOutputBuffer::new(raw_buf.pin_mut());
        buffer.append((
            NinjaCommandWindows(File::new("my tool").as_str()),
            " --input=",
            NinjaCommandWindows(File::new("foo bar.cc").as_str()),
            b'\n',
        ));
        assert_eq!(buffer.to_string(), "\"my$ tool\" --input=\"foo$ bar.cc\"\n");
    }
}
