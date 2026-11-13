// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Expansion of Starlark `Args` into command-line strings inside `StringOutputBuffer`.

use args::{ArgVisitor, ArgsList};
use starlark::eval::Evaluator;

use crate::{Cursor, StringOutputBuffer, escape::EscapingPlatform};

fn backspace_if_moved(buffer: &mut StringOutputBuffer<'_>, start: Cursor) {
    // Safety: `reserve(0)` is safe as long as you don't advance it.
    // We can safely subtract 1 because if `cursor != start` then we must have
    // written at least one byte into the current allocation chunk.
    unsafe {
        let cursor: &mut Cursor = buffer.reserve(0);
        if *cursor != start {
            // If we wrote some data, the last thing we wrote, by design, is
            // always a space (usually via finish_arg, though sometimes via write).
            *cursor = (*cursor).sub(1);
        }
    }
}

struct PosixWriter<'a, 'b> {
    buffer: &'a mut StringOutputBuffer<'b>,
    start: Cursor,
}

impl<'a, 'b> PosixWriter<'a, 'b> {
    fn new(buffer: &'a mut StringOutputBuffer<'b>) -> Self {
        // Safety: We never write to this cursor.
        let start = unsafe { *buffer.reserve(0) };
        Self { buffer, start }
    }
}

impl ArgVisitor for PosixWriter<'_, '_> {
    fn add_arg_chunk(&mut self, part: &str) {
        self.buffer.append(crate::escape::NinjaCommandPosix(part));
    }

    fn finish_arg(&mut self) {
        self.buffer.append(b' ');
    }

    fn write(&mut self, val: impl types::Writable) {
        self.buffer.append(val);
    }

    fn done(&mut self) {
        backspace_if_moved(self.buffer, self.start);
    }
}

// Windows can't perform zero-copy escaping because we don't know whether we
// need to add quotes until we read the entire arg.
struct WinWriter<'a, 'b> {
    buffer: &'a mut StringOutputBuffer<'b>,
    token: String,
    start: Cursor,
}

impl<'a, 'b> WinWriter<'a, 'b> {
    fn new(buffer: &'a mut StringOutputBuffer<'b>) -> Self {
        // Safety: We never write to this cursor.
        let start = unsafe { *buffer.reserve(0) };
        Self {
            buffer,
            token: String::new(),
            start,
        }
    }
}

impl ArgVisitor for WinWriter<'_, '_> {
    // Optimization: Skip the extra copy into token if we're writing a whole arg at a time.
    fn add_arg(&mut self, part: &str) {
        self.buffer
            .append((crate::escape::NinjaCommandWindows(part), b' '));
    }

    fn add_arg_chunk(&mut self, part: &str) {
        self.token.push_str(part);
    }

    fn finish_arg(&mut self) {
        self.buffer
            .append((crate::escape::NinjaCommandWindows(&self.token), b' '));
        self.token.clear();
    }

    fn write(&mut self, val: impl types::Writable) {
        self.buffer.append(val);
    }

    fn done(&mut self) {
        backspace_if_moved(self.buffer, self.start);
    }
}

/// Processes and writes an `ArgsList` directly into `buffer`.
pub fn write_args<'v>(
    buffer: &mut StringOutputBuffer<'_>,
    eval: &mut Evaluator<'v, '_, '_>,
    args: &ArgsList<'v>,
    platform: EscapingPlatform,
) -> starlark::Result<()> {
    match platform {
        EscapingPlatform::Posix => {
            let mut writer = PosixWriter::new(buffer);
            args.expand(eval, &mut writer)?;
            writer.done();
        },
        EscapingPlatform::Win => {
            let mut writer = WinWriter::new(buffer);
            args.expand(eval, &mut writer)?;
            writer.done();
        },
        _ => unreachable!(),
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::bridge;

    #[derive(Debug, Eq, PartialEq)]
    struct EscapedStrings {
        posix: String,
        win: String,
    }

    macro_rules! escape {
        (| $w:ident | $body:expr) => {{
            let mut posix_raw = bridge::NewStringOutputBuffer();
            let mut posix_buf = StringOutputBuffer::new(posix_raw.pin_mut());
            {
                let $w = &mut PosixWriter::new(&mut posix_buf);
                $body;
            }

            let mut win_raw = bridge::NewStringOutputBuffer();
            let mut win_buf = StringOutputBuffer::new(win_raw.pin_mut());
            {
                let $w = &mut WinWriter::new(&mut win_buf);
                $body;
            }

            EscapedStrings {
                posix: posix_buf.to_string(),
                win: win_buf.to_string(),
            }
        }};
    }

    #[test]
    fn test_writer_empty() {
        assert_eq!(
            escape!(|w| w.done()),
            EscapedStrings {
                posix: "".into(),
                win: "".into(),
            },
        );
    }

    #[test]
    fn test_writer_args() {
        assert_eq!(
            escape!(|w| {
                w.add_arg("plain");
                w.add_arg("has space");
                w.done();
            }),
            EscapedStrings {
                posix: "plain has\\$ space".into(),
                win: "plain \"has$ space\"".into(),
            },
        );
    }

    #[test]
    fn test_writer_chunks() {
        assert_eq!(
            escape!(|w| {
                w.add_arg_chunk("my flag");
                w.add_arg_chunk(",");
                w.add_arg_chunk("my value");
                w.finish_arg();
                w.add_arg_chunk("next");
                w.finish_arg();
                w.done();
            }),
            EscapedStrings {
                posix: "my\\$ flag,my\\$ value next".into(),
                win: "\"my$ flag,my$ value\" next".into(),
            },
        );
    }

    #[test]
    fn test_writer_unescaped() {
        assert_eq!(
            escape!(|w| {
                w.add_arg("first");
                w.write(("${shared_", 42u32, "} "));
                w.done();
            }),
            EscapedStrings {
                posix: "first ${shared_42}".into(),
                win: "first ${shared_42}".into(),
            },
        );
    }
}
