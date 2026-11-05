// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Expansion of Starlark `Args` into command-line strings inside `StringOutputBuffer`.

use args::{ArgVisitor, ArgsList};
use starlark::eval::Evaluator;

use crate::{Cursor, StringOutputBuffer, escape::EscapingPlatform};

struct PosixWriter<'a, 'b> {
    buffer: &'a mut StringOutputBuffer<'b>,
}

impl ArgVisitor for PosixWriter<'_, '_> {
    fn add_arg_chunk(&mut self, part: &str) {
        self.buffer.append(crate::escape::NinjaCommandPosix(part));
    }

    fn finish_arg(&mut self) {
        self.buffer.append(b' ');
    }
}

// Windows can't perform zero-copy escaping because we don't know whether we
// need to add quotes until we read the entire arg.
struct WinWriter<'a, 'b> {
    buffer: &'a mut StringOutputBuffer<'b>,
    token: String,
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
}

/// Processes and writes an `ArgsList` directly into `buffer`.
pub fn write_args<'v>(
    buffer: &mut StringOutputBuffer<'_>,
    eval: &mut Evaluator<'v, '_, '_>,
    args: &ArgsList<'v>,
    platform: EscapingPlatform,
) -> starlark::Result<()> {
    // Safety: We never write to this cursor
    let start: Cursor = unsafe { *buffer.reserve(0) };
    match platform {
        EscapingPlatform::Posix => {
            let mut writer = PosixWriter { buffer };
            args.expand(eval, &mut writer)?;
        },
        EscapingPlatform::Win => {
            let mut writer = WinWriter {
                buffer,
                token: String::new(),
            };
            args.expand(eval, &mut writer)?;
        },
        _ => unreachable!(),
    }

    // Safety: reserve(0) is safe as long as you don't advance it.
    // we can safely subtract 1 because if cursor != start then we must have
    // wrote at least one byte into the current allocation chunk.
    unsafe {
        let cursor: &mut Cursor = buffer.reserve(0);
        if *cursor != start {
            // If we wrote some data, the last thing we did must have been to
            // call finish_arg, which always ends with ' '. Strip it.
            *cursor = (*cursor).sub(1);
        }
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
        a.assert_expands_to(
            r#"[args().add_joined(["my flag", "my value"], join_with=",")]"#,
            EscapingPlatform::Posix,
            "my\\$ flag,my\\$ value",
        );
    }

    #[test]
    fn test_args_escape_command_line_windows() {
        let mut a = ArgsAssert::new();
        a.assert_expands_to(
            r#"[args().add_joined(["my flag", "my value"], join_with=",")]"#,
            EscapingPlatform::Win,
            "\"my$ flag,my$ value\"",
        );
    }
}
