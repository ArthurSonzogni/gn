// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use attr::{TargetAttrExt, traits::EvalContextAttrExt};
use starlark::{
    environment::Module,
    eval::Evaluator,
    values::{HeapEdge, OwnedFrozen, Value},
};
use types::{EvaluatorContextExt as _, Session, TargetRef as _};

use crate::{Ctx, Rule};

/// Runs the rule implementation function for a given target.
/// This matches the execution phase where rule implementation is run
/// synchronously.
pub fn run<C: EvalContextAttrExt + crate::CtxMethods>(
    target: &<C::Session as Session>::TargetRef,
    create_context: impl FnOnce(&<C::Session as Session>::TargetRef) -> C,
) -> starlark::Result<(Vec<types::Phony>, OwnedFrozen<Value<'static>>)>
where
    <C::Session as Session>::TargetRef:
        TargetAttrExt<Rule = Rule<'static, C>, Session = C::Session>,
{
    let rule = target.rule();
    let rule_context = create_context(target);

    Module::with_temp_heap(|module| {
        let res = if rule.is_builtin() {
            // The other branch would also work, but this allows us to skip several
            // allocations (eg. ctx.attr.*) and spinning up a starlark
            // interpreter.
            let eval = Evaluator::new(&module);
            let heap = eval.heap();
            let mut state = rule_context.require_rule_impl()?.borrow_mut();
            rule_context
                .session()
                .run_builtin_rule(&heap, target, &mut state)
        } else {
            // When the module is frozen, only things transitively required by extra_value
            // are kept.
            let mut eval = Evaluator::new(&module);
            eval.set_context(&rule_context);
            let rule = HeapEdge::immortal().rebrand_ref(rule);
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
                eval.heap().alloc(crate::actions::Actions::<C>::default()),
            ));
            eval.eval_function(rule.implementation, &[ctx], &[])?
        };

        let phonies = rule_context
            .require_rule_impl()?
            .borrow_mut()
            .rule_impl_complete()?;
        module.set_extra_value(res);

        let frozen = module.freeze()?;
        Ok((phonies, frozen.extra_value().unwrap()))
    })
}
