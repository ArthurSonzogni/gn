// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{
    cell::RefCell,
    fmt::{self, Display, Formatter},
    marker::PhantomData,
};

use allocative::Allocative;
use attr::{TargetAttrExt, traits::EvalContextAttrExt};
use starlark::{
    any::ProvidesStaticType,
    environment::Methods,
    values::{AllocValue, Freeze, FreezeResult, Freezer, Heap, StarlarkValue, Trace, Value},
};
use starlark_derive::{NoSerialize, starlark_value};
use types::{CtxMethods, Session};

/// The `ctx.actions` object which provides APIs to declare output files and
/// register build actions.
#[derive(Allocative, NoSerialize, Trace)]
#[trace(bound = "C: EvalContextAttrExt + CtxMethods")]
pub struct Actions<'v, C: EvalContextAttrExt + CtxMethods> {
    actions: RefCell<Vec<crate::action::Action<'v>>>,
    // We don't need to store ctx because it's stored already in the EvalContext.
    _marker: PhantomData<fn() -> C>,
}

impl<'v, C: EvalContextAttrExt + CtxMethods> Actions<'v, C> {
    /// Pushes an action to the list of registered actions.
    pub fn push_action(&self, action: crate::action::Action<'v>) {
        self.actions.borrow_mut().push(action);
    }

    /// Takes all registered actions, leaving the internal list empty.
    pub(crate) fn take_actions(&self) -> Vec<crate::action::Action<'v>> {
        self.actions.take()
    }
}

// Safety: All instances of Actions with the same C have the same static type.
unsafe impl<'v, C: EvalContextAttrExt + CtxMethods> ProvidesStaticType<'v> for Actions<'v, C> {
    type StaticType = Actions<'static, C>;
}

impl<'v, C: EvalContextAttrExt + CtxMethods> Default for Actions<'v, C> {
    fn default() -> Self {
        Self {
            actions: RefCell::new(Vec::new()),
            _marker: PhantomData,
        }
    }
}

impl<'v, C: EvalContextAttrExt + CtxMethods> std::fmt::Debug for Actions<'v, C> {
    fn fmt(&self, f: &mut Formatter<'_>) -> fmt::Result {
        write!(f, "ctx.actions")
    }
}

impl<'v, C: EvalContextAttrExt + CtxMethods> Display for Actions<'v, C> {
    fn fmt(&self, f: &mut Formatter<'_>) -> fmt::Result {
        write!(f, "ctx.actions")
    }
}

#[starlark_value(type = "ctx.actions")]
impl<'v, C: EvalContextAttrExt + CtxMethods> StarlarkValue<'v> for Actions<'v, C>
where
    <C::Session as Session>::TargetRef: TargetAttrExt,
{
    type Canonical = Self;

    fn get_methods() -> Option<&'static Methods> {
        Some(<C as CtxMethods>::actions_methods())
    }
}

impl<'v, C: EvalContextAttrExt + CtxMethods> AllocValue<'v> for Actions<'v, C>
where
    <C::Session as Session>::TargetRef: TargetAttrExt,
{
    fn alloc_value(self, heap: Heap<'v>) -> Value<'v> {
        heap.alloc_complex(self)
    }
}

impl<'v, C: EvalContextAttrExt + CtxMethods> Freeze<'v> for Actions<'v, C>
where
    <C::Session as Session>::TargetRef: TargetAttrExt,
{
    type Frozen<'fv> = starlark::values::none::NoneType;

    fn freeze<'fv>(self, _packer: &Freezer<'v, 'fv>) -> FreezeResult<Self::Frozen<'fv>> {
        Err(crate::errors::Error::ObjectUnfreezable("ctx.actions").into())
    }
}

#[macro_export]
macro_rules! impl_actions_methods {
    ($ctx_type:ty) => {
        #[starlark_derive::starlark_module]
        pub fn actions_methods(builder: &mut starlark::environment::MethodsBuilder) {
            fn declare_file<'v>(
                this: starlark::values::Value<'v>,
                filename: &str,
                eval: &mut starlark::eval::Evaluator<'v, '_, '_>,
            ) -> starlark::Result<starlark::values::Value<'v>> {
                let _ = this;
                use $crate::types::{EvalContext as _, EvaluatorContextExt as _};
                let file = eval
                    .context::<$ctx_type>()
                    .require_rule_impl()?
                    .borrow_mut()
                    .declare_file(filename)?;
                Ok(eval.heap().alloc(file))
            }

            fn args<'v>(
                this: starlark::values::Value<'v>,
                eval: &mut starlark::eval::Evaluator<'v, '_, '_>,
            ) -> starlark::Result<starlark::values::Value<'v>> {
                let _ = this;
                Ok(eval.heap().alloc($crate::args::Args::default()))
            }

            fn run<'v>(
                this: starlark::values::Value<'v>,
                #[starlark(require = named)] outputs: starlark::values::list::UnpackList<
                    $crate::types::File,
                >,
                #[starlark(require = named)] inputs: Option<
                    $crate::either::Either<
                        starlark::values::list::UnpackList<$crate::types::File>,
                        $crate::depset::UnpackFileDepset,
                    >,
                >,
                #[starlark(require = named)] executable: $crate::either::Either<
                    $crate::types::File,
                    starlark::values::StringValue<'v>,
                >,
                #[starlark(require = named)] tools: Option<
                    $crate::either::Either<
                        starlark::values::list::UnpackList<
                            $crate::either::Either<
                                $crate::types::File,
                                $crate::depset::UnpackFileDepset,
                            >,
                        >,
                        $crate::depset::UnpackFileDepset,
                    >,
                >,
                #[starlark(require = named, default = Default::default())]
                // Despite the bazel documentation saying list[str] | list[Args], the
                // implementation takes list[str | Args].
                arguments: $crate::args::ArgsList<'v>,
                #[starlark(require = named, default = starlark::values::none::NoneOr::None)]
                mnemonic: starlark::values::none::NoneOr<starlark::values::StringValue<'v>>,
                eval: &mut starlark::eval::Evaluator<'v, '_, '_>,
            ) -> starlark::Result<starlark::values::none::NoneType> {
                use $crate::either::Either;
                use $crate::types::{EvalContext as _, EvaluatorContextExt as _, File};

                if outputs.items.is_empty() {
                    return Err($crate::Error::OutputsCannotBeEmpty.into());
                }

                // Collect executable, inputs, and tools into inputs.
                let mut inputs: Vec<File> = executable
                    .left()
                    .into_iter()
                    .chain(inputs.into_iter().flat_map(|either| match either {
                        Either::Left(list) => Either::Left(list.items.into_iter()),
                        Either::Right(depset) => Either::Right(depset.0.into_iter()),
                    }))
                    .chain(tools.into_iter().flat_map(|either| match either {
                        Either::Left(list) => {
                            Either::Left(list.items.into_iter().filter_map(|item| match item {
                                Either::Left(file) => Some(file),
                                Either::Right(depset) => depset.0,
                            }))
                        },
                        Either::Right(depset) => Either::Right(depset.0.into_iter()),
                    }))
                    .collect();

                $crate::types::uniquify_small_vec(&mut inputs);

                // Bazel only allows [a-zA-Z0-9]+ as mnemonics. We filter this further to [A-Z][A-Z0-9]* for consistency with GN builtin actions.
                let mnemonic = mnemonic.into_option();
                if let Some(s) = mnemonic {
                    let bytes = s.as_bytes();
                    let is_valid = matches!(bytes.first(), Some(b'A'..=b'Z'))
                        && bytes[1..].iter().all(|b| matches!(b, b'A'..=b'Z' | b'0'..=b'9'));
                    if !is_valid {
                        return Err($crate::Error::InvalidMnemonic(s.as_str().to_owned()).into());
                    }
                }

                let ctx = eval.context::<$ctx_type>();
                {
                    let mut state = ctx.require_rule_impl()?.borrow_mut();
                    for file in &outputs.items {
                        state.generates_file(file)?;
                    }
                }

                this.downcast_ref::<$crate::actions::Actions<'v, $ctx_type>>()
                    .unwrap()
                    .push_action($crate::action::Action {
                        outputs: outputs.items,
                        inputs,
                        executable,
                        arguments,
                        mnemonic,
                    });

                Ok(starlark::values::none::NoneType)
            }

            fn write<'v>(
                this: starlark::values::Value<'v>,
                output: &$crate::types::File,
                content: &str,
                #[starlark(default = false)] is_executable: bool,
                eval: &mut starlark::eval::Evaluator<'v, '_, '_>,
            ) -> starlark::Result<starlark::values::none::NoneType> {
                let _ = this;
                use $crate::types::{EvalContext as _, EvaluatorContextExt as _};
                let ctx = eval.context::<$ctx_type>();
                ctx.require_rule_impl()?
                    .borrow_mut()
                    .generates_file(output)?;

                let path = ctx.path_resolver().resolve(output);
                if let Some(parent) = path.parent() {
                    std::fs::create_dir_all(parent).map_err(starlark::Error::new_other)?;
                }

                std::fs::write(&path, content).map_err(starlark::Error::new_other)?;

                // On Windows, executability is determined by file extension,
                // so no permission bits need to be modified.
                #[cfg(unix)]
                {
                    use std::os::unix::fs::PermissionsExt;
                    // On unix, std::fs::write preserves existing permissions.
                    // So in case the user changed the is_executable flag, we need to set the
                    // permissions explicitly every single time.
                    std::fs::set_permissions(
                        &path,
                        std::fs::Permissions::from_mode(if is_executable { 0o755 } else { 0o644 }),
                    )
                    .map_err(starlark::Error::new_other)?;
                }

                Ok(starlark::values::none::NoneType)
            }
        }
    };
}
