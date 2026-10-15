// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt;

use allocative::Allocative;
use attr::{TargetAttrExt, traits::EvalContextAttrExt};
use providers::Providers;
use starlark::{
    any::ProvidesStaticType,
    environment::Module,
    eval::Evaluator,
    values::{Freeze, HeapEdge, OwnedFrozen, StarlarkValue, UnpackValue as _, ValueTyped},
};
use starlark_derive::{NoSerialize, Trace, starlark_value};
use types::{EvaluatorContextExt as _, Phony, Session, TargetRef as _};

use crate::{Ctx, Rule};

/// The evaluated results of running a rule implementation on a target.
#[derive(Allocative, Debug, Freeze, NoSerialize, ProvidesStaticType, Trace)]
pub struct EvaluatedTarget<'v> {
    /// The phony outputs declared by this target.
    pub phonies: Vec<Phony>,
    /// The actions registered by ctx.actions.run.
    pub actions: Vec<crate::action::Action<'v>>,
    /// The providers returned by the target's rule implementation.
    pub providers: Providers<'v>,
}

impl<'v> fmt::Display for EvaluatedTarget<'v> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "EvaluatedTarget")
    }
}

#[starlark_value(type = "EvaluatedTarget")]
impl<'v> StarlarkValue<'v> for EvaluatedTarget<'v> {
    type Canonical = Self;
}

/// Runs the rule implementation function for a given target.
/// This matches the execution phase where rule implementation is run
/// synchronously.
pub fn run<C: EvalContextAttrExt + crate::CtxMethods>(
    target: &<C::Session as Session>::TargetRef,
    create_context: impl FnOnce(&<C::Session as Session>::TargetRef) -> C,
) -> starlark::Result<OwnedFrozen<ValueTyped<'static, EvaluatedTarget<'static>>>>
where
    <C::Session as Session>::TargetRef:
        TargetAttrExt<Rule = Rule<'static, C>, Session = C::Session>,
{
    let rule = target.rule();
    let rule_context = create_context(target);

    Module::with_temp_heap(|module| {
        let (res, actions) = if rule.is_builtin() {
            // The other branch would also work, but this allows us to skip several
            // allocations (eg. ctx.attr.*) and spinning up a starlark
            // interpreter.
            let eval = Evaluator::new(&module);
            let heap = eval.heap();
            let mut state = rule_context.require_rule_impl()?.borrow_mut();
            (
                rule_context
                    .session()
                    .run_builtin_rule(&heap, target, &mut state),
                Vec::new(),
            )
        } else {
            // When the module is frozen, only things transitively required by extra_value
            // are kept.
            let mut eval = Evaluator::new(&module);
            eval.set_context(&rule_context);
            let rule = HeapEdge::immortal().rebrand_ref(rule);
            let actions = eval
                .heap()
                .alloc_typed(crate::actions::Actions::<C>::default());
            let ctx = eval.heap().alloc(Ctx::<C>::new(
                rule.schema.create_ctx_fields(
                    target.attrs(),
                    rule_context.session(),
                    &rule_context.current_toolchain(),
                    rule.output_type,
                    target.builtin_attrs(rule_context.session(), &eval.heap()),
                    &eval.heap(),
                )?,
                rule,
                eval.heap().alloc(target.label().to_owned()),
                actions.to_value(),
            ));
            (
                eval.eval_function(rule.implementation, &[ctx], &[])?,
                actions.take_actions(),
            )
        };

        module.set_extra_value(
            module.heap().alloc_complex(EvaluatedTarget {
                phonies: rule_context
                    .require_rule_impl()?
                    .borrow_mut()
                    .rule_impl_complete()?,
                actions,
                providers: providers::Providers::unpack_value_err(res)?,
            }),
        );

        module.freeze()?.extra_value().unwrap().downcast_starlark()
    })
}
