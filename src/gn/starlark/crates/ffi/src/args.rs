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
    use args::Args;
    use starlark::{
        environment::GlobalsBuilder,
        values::{UnpackValue as _, Value},
    };
    use starlark_derive::starlark_module;

    use super::*;
    use crate::bridge;

    #[starlark_module]
    fn register_args_test_globals(builder: &mut GlobalsBuilder) {
        fn args<'v>(eval: &mut Evaluator<'v, '_, '_>) -> starlark::Result<Value<'v>> {
            Ok(eval.heap().alloc(Args::default()))
        }
    }

    struct ArgsAssert {
        assert: testutils::Assert,
    }

    impl ArgsAssert {
        fn new() -> Self {
            let mut a = testutils::Assert::default();
            a.modify_globals(|builder| {
                register_args_test_globals(builder);
                depset::depset_globals!(builder, testutils::FakeEvalContext);
            });
            Self { assert: a }
        }

        #[track_caller]
        fn assert_expands_to(&mut self, code: &str, platform: EscapingPlatform, expected: &str) {
            let val = self.assert.pass(code);

            let res = starlark::environment::Module::with_temp_heap(|module| {
                let mut eval = starlark::eval::Evaluator::new(&module);
                let val_ref = val.as_ref().add_to_heap(module.heap());
                let seq = <args::ArgsList>::unpack_value_err(val_ref).unwrap();
                let mut raw_buf = bridge::NewStringOutputBuffer();
                let mut buffer = StringOutputBuffer::new(raw_buf.pin_mut());
                write_args(&mut buffer, &mut eval, &seq, platform)?;
                starlark::Result::Ok(buffer.to_string())
            });

            assert_eq!(res.unwrap(), expected);
        }
    }

    #[test]
    fn test_args_escape_command_line_posix() {
        let mut a = ArgsAssert::new();
        a.assert_expands_to(r#"[]"#, EscapingPlatform::Posix, "");
        a.assert_expands_to(
            r#"[args().add_joined(["my flag", "my value"], join_with=",")]"#,
            EscapingPlatform::Posix,
            "my\\$ flag,my\\$ value",
        );
    }

    #[test]
    fn test_args_escape_command_line_windows() {
        let mut a = ArgsAssert::new();
        a.assert_expands_to(r#"[]"#, EscapingPlatform::Win, "");
        a.assert_expands_to(
            r#"[args().add_joined(["my flag", "my value"], join_with=",")]"#,
            EscapingPlatform::Win,
            "\"my$ flag,my$ value\"",
        );
    }
}
