// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{
    fmt::{self, Display, Formatter},
    marker::PhantomData,
};

use allocative::Allocative;
use attr::{traits::EvalContextAttrExt, TargetAttrExt};
use starlark::{
    any::ProvidesStaticType,
    environment::Methods,
    values::{AllocValue, Freeze, FreezeResult, Freezer, Heap, StarlarkValue, Trace, Value},
};
use starlark_derive::{starlark_value, NoSerialize};
use types::{CtxMethods, Session};

/// The `ctx.actions` object which provides APIs to declare output files and
/// register build actions.
#[derive(Allocative, NoSerialize, Trace)]
#[trace(bound = "C: EvalContextAttrExt + CtxMethods")]
pub struct Actions<C: EvalContextAttrExt + CtxMethods> {
    // We don't need to store ctx because it's stored already in the EvalContext.
    _marker: PhantomData<fn() -> C>,
}

// Safety: This is a zero-sized type. All are equivalent.
unsafe impl<'v, C: EvalContextAttrExt + CtxMethods> ProvidesStaticType<'v> for Actions<C> {
    type StaticType = Actions<C>;
}

impl<C: EvalContextAttrExt + CtxMethods> Default for Actions<C> {
    fn default() -> Self {
        Self {
            _marker: PhantomData,
        }
    }
}

impl<C: EvalContextAttrExt + CtxMethods> std::fmt::Debug for Actions<C> {
    fn fmt(&self, f: &mut Formatter<'_>) -> fmt::Result {
        write!(f, "ctx.actions")
    }
}

impl<C: EvalContextAttrExt + CtxMethods> Display for Actions<C> {
    fn fmt(&self, f: &mut Formatter<'_>) -> fmt::Result {
        write!(f, "ctx.actions")
    }
}

#[starlark_value(type = "ctx.actions")]
impl<'v, C: EvalContextAttrExt + CtxMethods> StarlarkValue<'v> for Actions<C>
where
    <C::Session as Session>::TargetRef: TargetAttrExt,
{
    type Canonical = Self;

    fn get_methods() -> Option<&'static Methods> {
        Some(<C as CtxMethods>::actions_methods())
    }
}

impl<'v, C: EvalContextAttrExt + CtxMethods> AllocValue<'v> for Actions<C>
where
    <C::Session as Session>::TargetRef: TargetAttrExt,
{
    fn alloc_value(self, heap: Heap<'v>) -> Value<'v> {
        heap.alloc_complex(self)
    }
}

impl<'v, C: EvalContextAttrExt + CtxMethods> Freeze<'v> for Actions<C>
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
                use types::{EvalContext as _, EvaluatorContextExt as _};
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

            fn write<'v>(
                this: starlark::values::Value<'v>,
                output: &types::File,
                content: &str,
                #[starlark(default = false)] is_executable: bool,
                eval: &mut starlark::eval::Evaluator<'v, '_, '_>,
            ) -> starlark::Result<starlark::values::none::NoneType> {
                let _ = this;
                use types::{EvalContext as _, EvaluatorContextExt as _};
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
