// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// derive(Trace) on ArgsList causes this lint failure, but it's nonsensical.
// Unfortunately we can't disable it for just that specific part of the file because it's a separate
// AST node.
#![allow(clippy::semicolon_if_nothing_returned)]

use std::convert::Infallible;

use allocative::Allocative;
use either::Either;
use starlark::{
    any::ProvidesStaticType,
    eval::Evaluator,
    typing::Ty,
    values::{
        Freeze, FreezeResult, Freezer, StringValue, Trace, UnpackValue, Value, ValueTyped,
        list::UnpackList, type_repr::StarlarkTypeRepr,
    },
};

use crate::{
    ArgVisitor,
    args::{Args, FrozenArgs},
    expand::expand_args,
};

/// Starlark value for Args | str.
/// It is possible to write the following code. When doing so, bazel
/// does *not* add foo to the command-line.
/// ```
/// args = ctx.actions.args()
/// ctx.actions.run(..., arguments = [args])
/// args.add("--foo")
/// ```
/// To match this behaviour, if an args is provided, we take a copy of args rather than a reference.
/// As an optimization, however, since args is append-only, (reference, size) is equivalent to a
/// copy.
#[derive(Allocative, Clone, Debug, PartialEq, Eq, ProvidesStaticType, Trace)]
pub enum ArgsLike<'v> {
    /// A string.
    String(StringValue<'v>),
    /// A slice of an args object (`args[..upto]`).
    Args(ValueTyped<'v, Args<'v>>, usize),
    /// A slice of a frozen args object (`args[..upto]`).
    FrozenArgs(ValueTyped<'v, FrozenArgs<'v>>, usize),
}

// Safety: ArgsLike with 'static lifetime only references frozen/immutable data.
unsafe impl Sync for ArgsLike<'static> {}

impl<'v> Freeze<'v> for ArgsLike<'v> {
    type Frozen<'fv> = ArgsLike<'fv>;

    fn freeze<'fv>(self, freezer: &Freezer<'v, 'fv>) -> FreezeResult<Self::Frozen<'fv>> {
        Ok(match self {
            Self::String(s) => ArgsLike::String(s.freeze(freezer)?),
            Self::Args(args, upto) => ArgsLike::FrozenArgs(args.freeze(freezer)?, upto),
            Self::FrozenArgs(args, upto) => ArgsLike::FrozenArgs(args.freeze(freezer)?, upto),
        })
    }
}

impl<'v> StarlarkTypeRepr for ArgsLike<'v> {
    type Canonical = Either<Args<'v>, String>;

    fn starlark_type_repr() -> Ty {
        Self::Canonical::starlark_type_repr()
    }
}

impl<'v> UnpackValue<'v> for ArgsLike<'v> {
    type Error = Infallible;

    fn unpack_value_impl(value: Value<'v>) -> Result<Option<Self>, Self::Error> {
        if let Some(s) = StringValue::new(value) {
            Ok(Some(Self::String(s)))
        } else if let Some(args) = ValueTyped::<Args<'v>>::new(value) {
            Ok(Some(Self::Args(args, args.arguments.borrow().len())))
        } else if let Some(args) = ValueTyped::<FrozenArgs<'v>>::new(value) {
            Ok(Some(Self::FrozenArgs(args, args.arguments.len())))
        } else {
            Ok(None)
        }
    }
}

/// Starlark value for list[Args | str].
#[derive(Allocative, Debug, Default, Freeze, ProvidesStaticType, StarlarkTypeRepr, Trace)]
pub struct ArgsList<'v>(Vec<ArgsLike<'v>>);

impl<'v> UnpackValue<'v> for ArgsList<'v> {
    type Error = Infallible;

    fn unpack_value_impl(value: Value<'v>) -> Result<Option<Self>, Self::Error> {
        Ok(UnpackList::<ArgsLike<'v>>::unpack_value_opt(value).map(|l| Self(l.items)))
    }
}

impl<'v> ArgsList<'v> {
    /// Expands the arguments list into the given visitor.
    pub fn expand(
        &self,
        eval: &mut Evaluator<'v, '_, '_>,
        visitor: &mut impl ArgVisitor,
    ) -> starlark::Result<()> {
        for arg in &self.0 {
            match arg {
                ArgsLike::String(s) => visitor.add_arg(s.as_str()),
                ArgsLike::Args(..) => {
                    unreachable!("expand should never be called on a non-frozen ArgsList")
                },
                ArgsLike::FrozenArgs(args, upto) => {
                    expand_args(&args.arguments[..*upto], eval, visitor)?;
                },
            }
        }
        Ok(())
    }

    /// Asserts that expanding this argument list produces the expected strings.
    #[track_caller]
    pub fn assert_eq(
        &self,
        reconstructor: &starlark::values::OwnedFrozenReconstructor<'v>,
        values: &[&str],
    ) {
        let expanded = starlark::environment::Module::with_temp_heap(|module| {
            let mut eval = Evaluator::new(&module);
            let edge = reconstructor.edge(eval.heap());
            let args = edge.rebrand_ref(self);
            let mut visitor = crate::TestArgVisitor::default();
            args.expand(&mut eval, &mut visitor).map(|_| visitor.args)
        });
        assert_eq!(expanded.unwrap(), values);
    }
}
