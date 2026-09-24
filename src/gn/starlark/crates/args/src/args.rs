// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{cell::RefCell, fmt, fmt::Display};

use allocative::Allocative;
use starlark::{
    environment::{Methods, MethodsBuilder},
    eval::Evaluator,
    starlark_complex_value,
    values::{Freeze, FreezeResult, Freezer, ProvidesStaticType, StarlarkValue, Trace, Value},
};
use starlark_derive::{starlark_module, starlark_value, NoSerialize};

use crate::{formatter::Formatter, Error};

/// Internal representation of individual arguments stored in `Args`.
#[derive(Allocative, Clone, Debug, Freeze, NoSerialize, ProvidesStaticType, Trace)]
pub enum ArgValue<'v> {
    Scalar {
        arg_name: Option<String>,
        value: Value<'v>,
        format: Option<Formatter>,
    },
    All {
        flag: Option<String>,
        values: Value<'v>,
        map_each: Option<Value<'v>>,
        format_each: Option<Formatter>,
        before_each: Option<String>,
        terminate_with: Option<String>,
        omit_if_empty: bool,
        uniquify: bool,
    },
    Joined {
        flag: Option<String>,
        values: Value<'v>,
        join_with: String,
        map_each: Option<Value<'v>>,
        format_each: Option<Formatter>,
        format_joined: Option<Formatter>,
        omit_if_empty: bool,
        uniquify: bool,
    },
}

/// The mutable Starlark `Args` object used to construct command lines for
/// actions.
#[derive(Allocative, Debug, Default, NoSerialize, ProvidesStaticType, Trace)]
pub struct Args<'v> {
    /// List of arguments added to the builder.
    pub(crate) arguments: RefCell<Vec<ArgValue<'v>>>,
}

/// The frozen Starlark `Args` object, which is read-only and thread-safe.
#[derive(Allocative, Debug, Default, Freeze, NoSerialize, ProvidesStaticType, Trace)]
pub struct FrozenArgs<'v> {
    /// List of frozen arguments.
    pub(crate) arguments: Vec<ArgValue<'v>>,
}

starlark_complex_value!(pub FrozenArgs);

impl<'v> starlark::values::AllocValue<'v> for Args<'v> {
    #[inline]
    fn alloc_value(self, heap: starlark::values::Heap<'v>) -> starlark::values::Value<'v> {
        heap.alloc_complex(self)
    }
}

impl<'v> Display for Args<'v> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "args")
    }
}

impl<'v> Display for FrozenArgs<'v> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "args")
    }
}

#[starlark_value(type = "Args")]
impl<'v> StarlarkValue<'v> for Args<'v> {
    type Canonical = FrozenArgs<'v>;

    fn get_methods() -> Option<&'static Methods> {
        starlark::methods_static!(RES = args_methods);
        Some(RES.methods())
    }
}

#[starlark_value(type = "Args")]
impl<'v> StarlarkValue<'v> for FrozenArgs<'v> {
    type Canonical = Self;

    fn get_methods() -> Option<&'static Methods> {
        starlark::methods_static!(RES = args_methods);
        Some(RES.methods())
    }
}

impl<'v> Freeze<'v> for Args<'v> {
    type Frozen<'fv> = FrozenArgs<'fv>;

    fn freeze<'fv>(self, freezer: &Freezer<'v, 'fv>) -> FreezeResult<Self::Frozen<'fv>> {
        let arguments = self
            .arguments
            .into_inner()
            .into_iter()
            .map(|arg| arg.freeze(freezer))
            .collect::<Result<Vec<_>, _>>()?;
        Ok(FrozenArgs { arguments })
    }
}

// Inline it to ensure that the error doesn't actually need to be passed as a
// function parameter.
#[inline]
fn arg_name_and_value<'v>(
    arg_name_or_value: Value<'v>,
    value: Option<Value<'v>>,
    err: Error,
) -> starlark::Result<(Option<String>, Value<'v>)> {
    if let Some(val) = value {
        if let Some(arg_name) = arg_name_or_value.unpack_str() {
            Ok((Some(arg_name.to_owned()), val))
        } else {
            Err(starlark::Error::from(err))
        }
    } else {
        Ok((None, arg_name_or_value))
    }
}

fn get_mutable_args<'v>(
    this: Value<'v>,
) -> starlark::Result<std::cell::RefMut<'v, Vec<ArgValue<'v>>>> {
    if let Some(args) = this.downcast_ref::<Args<'v>>() {
        Ok(args.arguments.borrow_mut())
    } else if this.downcast_ref::<FrozenArgs<'v>>().is_some() {
        Err(starlark::Error::new_other(Error::CannotMutateFrozenArgs))
    } else {
        unreachable!();
    }
}

/// Registers the Starlark methods of the `Args` class (`add`, `add_all`,
/// `add_joined`).
#[starlark_module]
pub fn args_methods(builder: &mut MethodsBuilder) {
    fn add<'v>(
        this: Value<'v>,
        arg_name_or_value: Value<'v>,
        value: Option<Value<'v>>,
        #[starlark(require = named)] format: Option<Formatter>,
    ) -> starlark::Result<Value<'v>> {
        let mut args = get_mutable_args(this)?;

        let (arg_name, val) =
            arg_name_and_value(arg_name_or_value, value, Error::ExpectedAddStringFlag)?;

        args.push(ArgValue::Scalar {
            arg_name,
            value: val,
            format,
        });
        Ok(this)
    }

    fn add_all<'v>(
        this: Value<'v>,
        arg_name_or_values: Value<'v>,
        values: Option<Value<'v>>,
        #[starlark(require = named)] map_each: Option<Value<'v>>,
        #[starlark(require = named)] format_each: Option<Formatter>,
        #[starlark(require = named)] before_each: Option<&str>,
        #[starlark(require = named)] terminate_with: Option<&str>,
        #[starlark(require = named, default = true)] omit_if_empty: bool,
        #[starlark(require = named, default = false)] uniquify: bool,
        #[starlark(require = named)] allow_closure: Option<bool>,
    ) -> starlark::Result<Value<'v>> {
        // See a comment on the error message for more details on why this is needed.
        if map_each.is_some() && allow_closure.is_none() {
            return Err(Error::MapEachRequiresAllowClosure.into());
        }

        let mut args = get_mutable_args(this)?;

        let (flag, values) =
            arg_name_and_value(arg_name_or_values, values, Error::ExpectedAddAllStringFlag)?;

        args.push(ArgValue::All {
            flag,
            values,
            map_each,
            format_each,
            before_each: before_each.map(String::from),
            terminate_with: terminate_with.map(String::from),
            omit_if_empty,
            uniquify,
        });
        Ok(this)
    }

    fn add_joined<'v>(
        this: Value<'v>,
        arg_name_or_values: Value<'v>,
        values: Option<Value<'v>>,
        #[starlark(require = named)] join_with: &str,
        #[starlark(require = named)] map_each: Option<Value<'v>>,
        #[starlark(require = named)] format_each: Option<Formatter>,
        #[starlark(require = named)] format_joined: Option<Formatter>,
        #[starlark(require = named, default = true)] omit_if_empty: bool,
        #[starlark(require = named, default = false)] uniquify: bool,
        #[starlark(require = named)] allow_closure: Option<bool>,
    ) -> starlark::Result<Value<'v>> {
        // See a comment on the error message for more details on why this is needed.
        if map_each.is_some() && allow_closure.is_none() {
            return Err(Error::MapEachRequiresAllowClosure.into());
        }

        let mut args = get_mutable_args(this)?;

        let (flag, values) = arg_name_and_value(
            arg_name_or_values,
            values,
            Error::ExpectedAddJoinedStringFlag,
        )?;

        args.push(ArgValue::Joined {
            flag,
            values,
            join_with: join_with.to_owned(),
            map_each,
            format_each,
            format_joined,
            omit_if_empty,
            uniquify,
        });
        Ok(this)
    }
}

impl<'v> FrozenArgs<'v> {
    /// Expands the stored arguments list into command-line arguments and input
    /// files.
    pub fn expand(&self, eval: &mut Evaluator<'v, '_, '_>) -> starlark::Result<Vec<String>> {
        let mut command = Vec::new();
        crate::expand::expand_into(&mut command, self, eval)?;
        Ok(command)
    }
}
