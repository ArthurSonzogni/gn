// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use attr::{traits::EvalContextAttrExt, TargetAttrExt, TargetRef};
use starlark::{
    environment::Module,
    eval::Evaluator,
    values::{HeapEdge, OwnedFrozen, Value},
};
use types::{EvaluatorContextExt, Session};

use crate::{Ctx, Rule};

/// Runs the rule implementation function for a given target.
/// This matches the execution phase where rule implementation is run
/// synchronously.
pub fn run<C: EvalContextAttrExt + crate::CtxMethods>(
    target: &<C::Session as Session>::TargetRef,
    create_context: impl FnOnce(&<C::Session as Session>::TargetRef) -> C,
) -> starlark::Result<OwnedFrozen<Value<'static>>>
where
    <C::Session as Session>::TargetRef:
        TargetAttrExt<Rule = Rule<'static, C>, Session = C::Session>,
{
    // Safety: rule is always a rule for custom rule-built targets.
    let rule = target.rule().unwrap();

    Module::with_temp_heap(|module| {
        let rule_context = create_context(target);

        // When the module is frozen, only things transitively required by extra_value
        // are kept.
        module.set_extra_value({
            let mut eval = Evaluator::new(&module);
            eval.set_context(&rule_context);
            let rule = HeapEdge::immortal().rebrand_ref(rule);
            let ctx = eval.heap().alloc(Ctx::<C>::new(
                rule.schema.create_ctx_fields(
                    target.attrs(),
                    rule_context.session(),
                    &rule_context.current_toolchain(),
                    rule.builtin,
                    target.builtin_attrs(rule_context.session(), &eval.heap()),
                    &eval.heap(),
                )?,
                rule,
                eval.heap().alloc(target.label().to_owned()),
                eval.heap().alloc(crate::actions::Actions::<C>::default()),
            ));

            let res = eval.eval_function(rule.implementation, &[ctx], &[])?;
            rule_context
                .require_rule_impl()?
                .borrow()
                .rule_impl_complete()?;
            res
        });

        let frozen = module.freeze()?;
        Ok(frozen.extra_value().unwrap())
    })
}
