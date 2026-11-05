// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::borrow::Cow;

use depset::Depset;
use starlark::{
    eval::Evaluator,
    values::{Value, list::ListRef, tuple::TupleRef},
};

use crate::{Error, args::ArgValue, formatter::Formatter};

/// Visitor for receiving string parts and argument completion events.
pub trait ArgVisitor {
    /// Appends a string slice as a partial component of the current argument.
    fn add_arg_chunk(&mut self, part: &str);

    /// Completes the current argument.
    ///
    /// Note: This may be called with zero preceding [`add_arg_chunk`](Self::add_arg_chunk)
    /// calls to emit an empty argument (`""`), for example when `add_joined` has empty
    /// values and `omit_if_empty = false` without `format_joined`.
    fn finish_arg(&mut self);

    /// Appends a complete single-chunk argument and completes it.
    fn add_arg(&mut self, arg: &str) {
        self.add_arg_chunk(arg);
        self.finish_arg();
    }

    /// Emits the string chunks corresponding to a formatted value into the current argument
    /// without completing the argument.
    fn add_formatted_chunks(&mut self, formatter: Option<&Formatter<'_>>, value: &str) {
        if let Some(fmt) = formatter {
            self.add_arg_chunk(fmt.before());
            self.add_arg_chunk(value);
            if let Some(after) = fmt.after() {
                self.add_arg_chunk(after);
            }
        } else {
            self.add_arg_chunk(value);
        }
    }

    /// Emits a completed formatted argument, calling [`finish_arg`](Self::finish_arg) afterwards.
    fn add_formatted(&mut self, formatter: Option<&Formatter<'_>>, value: &str) {
        self.add_formatted_chunks(formatter, value);
        self.finish_arg();
    }
}

/// Helper to process a slice of [`ArgValue`]s, invoking `visitor.add_arg_chunk` for each string
/// component of an argument and `visitor.finish_arg` at each argument boundary.
pub(crate) fn expand_args<'v>(
    args: &[ArgValue<'v>],
    eval: &mut Evaluator<'v, '_, '_>,
    visitor: &mut impl ArgVisitor,
) -> starlark::Result<()> {
    for arg in args {
        match arg {
            ArgValue::Scalar {
                arg_name,
                value,
                format,
            } => {
                if !value.is_none() {
                    if let Some(flag) = arg_name.as_deref() {
                        visitor.add_arg(flag);
                    }
                    visitor.add_formatted(format.as_ref(), &value.to_str());
                }
            },
            ArgValue::All {
                flag,
                values,
                map_each,
                format_each,
                before_each,
                terminate_with,
                omit_if_empty,
                uniquify,
            } => {
                let mut wrote_any = false;
                let mut pending_flag = flag.as_deref();

                handle_many_args(*values, *map_each, *uniquify, eval, |item| {
                    if let Some(f) = pending_flag.take() {
                        visitor.add_arg(f);
                    }
                    if let Some(before) = before_each.as_deref() {
                        visitor.add_arg(before);
                    }
                    visitor.add_formatted(format_each.as_ref(), item);
                    wrote_any = true;
                    Ok(())
                })?;

                if wrote_any {
                    if let Some(term) = terminate_with.as_deref() {
                        visitor.add_arg(term);
                    }
                } else if !omit_if_empty {
                    if let Some(f) = pending_flag.take() {
                        visitor.add_arg(f);
                    }
                    if let Some(term) = terminate_with.as_deref() {
                        visitor.add_arg(term);
                    }
                }
            },
            ArgValue::Joined {
                flag,
                values,
                join_with,
                map_each,
                format_each,
                format_joined,
                omit_if_empty,
                uniquify,
            } => {
                let mut wrote_any = false;
                let mut pending_flag = flag.as_deref();

                handle_many_args(*values, *map_each, *uniquify, eval, |item| {
                    if let Some(f) = pending_flag.take() {
                        visitor.add_arg(f);
                    }
                    if !wrote_any {
                        if let Some(fj) = format_joined {
                            visitor.add_arg_chunk(fj.before());
                        }
                        wrote_any = true;
                    } else {
                        visitor.add_arg_chunk(join_with);
                    }
                    visitor.add_formatted_chunks(format_each.as_ref(), item);
                    Ok(())
                })?;

                if wrote_any {
                    if let Some(fj) = format_joined
                        && let Some(after) = fj.after()
                    {
                        visitor.add_arg_chunk(after);
                    }
                    visitor.finish_arg();
                } else if !omit_if_empty {
                    if let Some(f) = pending_flag.take() {
                        visitor.add_arg(f);
                    }
                    if let Some(fj) = format_joined {
                        visitor.add_arg_chunk(fj.before());
                        if let Some(after) = fj.after() {
                            visitor.add_arg_chunk(after);
                        }
                    }
                    visitor.finish_arg();
                }
            },
        }
    }
    Ok(())
}

fn for_each<'v, F>(value: Value<'v>, mut f: F) -> starlark::Result<()>
where
    F: FnMut(Value<'v>) -> starlark::Result<()>,
{
    if let Some(l) = ListRef::from_value(value) {
        for v in l.iter() {
            f(v)?;
        }
        Ok(())
    } else if let Some(depset) = value.downcast_ref::<Depset>() {
        for v in depset.iter() {
            f(v)?;
        }
        Ok(())
    } else if let Some(t) = TupleRef::from_value(value) {
        for v in t.iter() {
            f(v)?;
        }
        Ok(())
    } else {
        Err(Error::ArgumentsMustBeListTupleOrDepset.into())
    }
}

fn handle_many_args<'v>(
    value: Value<'v>,
    map_each: Option<Value<'v>>,
    uniquify: bool,
    eval: &mut Evaluator<'v, '_, '_>,
    mut on_item: impl FnMut(&str) -> starlark::Result<()>,
) -> starlark::Result<()> {
    let mut seen = starlark::collections::SmallSet::new();

    let mut process_item = |item: Value<'v>| -> starlark::Result<()> {
        if item.is_none() {
            return Err(Error::NoneNotAllowed.into());
        }
        if let Some(s) = item.unpack_str() {
            if uniquify && !seen.insert(Cow::Borrowed(s)) {
                return Ok(());
            }
            on_item(s)
        } else {
            let s = item.to_str();
            if uniquify && !seen.insert(Cow::Owned(s.clone())) {
                return Ok(());
            }
            on_item(&s)
        }
    };

    for_each(value, |v| {
        if let Some(map_each) = map_each {
            let mapped = eval.eval_function(map_each, &[v], &[])?;
            if let Some(l) = ListRef::from_value(mapped) {
                for item in l.iter() {
                    if item.unpack_str().is_none() {
                        return Err(Error::MapEachInvalidReturn.into());
                    }
                    process_item(item)?;
                }
            } else if let Some(t) = TupleRef::from_value(mapped) {
                for item in t.iter() {
                    if item.unpack_str().is_none() {
                        return Err(Error::MapEachInvalidReturn.into());
                    }
                    process_item(item)?;
                }
            } else if mapped.unpack_str().is_some() {
                process_item(mapped)?;
            } else if mapped.is_none() {
                // skip
            } else {
                return Err(Error::MapEachInvalidReturn.into());
            }
        } else {
            process_item(v)?;
        }
        Ok(())
    })?;

    Ok(())
}
