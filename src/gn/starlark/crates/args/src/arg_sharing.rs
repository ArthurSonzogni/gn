// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::hash::{Hash, Hasher};

use starlark::{
    collections::SmallMap,
    eval::Evaluator,
    values::{HeapEdge, ValueTyped},
};

use crate::{ArgVisitor, ArgsLike, FrozenArgs, expand::expand_args};

/// Wrapper comparing and hashing `FrozenArgs` values by heap identity rather
/// than value equality.
#[derive(Clone, Copy, Debug)]
struct FrozenArgsId<'v>(ValueTyped<'v, FrozenArgs<'v>>);

impl<'v> PartialEq for FrozenArgsId<'v> {
    fn eq(&self, other: &Self) -> bool {
        self.0.to_value().identity() == other.0.to_value().identity()
    }
}

impl<'v> Eq for FrozenArgsId<'v> {}

impl<'v> Hash for FrozenArgsId<'v> {
    fn hash<H: Hasher>(&self, state: &mut H) {
        self.0.to_value().identity().hash(state);
    }
}

/// Map from each non-empty `FrozenArgs` slice `(id, size)` to an optional
/// shared Ninja variable ID (`None` if used once and inlined).
#[derive(Debug, PartialEq, Eq)]
pub struct ArgSharingPlans<'v> {
    plans: SmallMap<(FrozenArgsId<'v>, usize), Option<u32>>,
}

impl<'v> ArgSharingPlans<'v> {
    /// Plans how to expand each `FrozenArgs` slice `(id, size)`.
    ///
    /// Slices seen once are inlined (`None`); identical `(id, size)` slices
    /// reused multiple times are assigned a variable `id` (`Some(id)`) so
    /// callers define them once and reference `$args_<id>` elsewhere.
    pub fn new<'a>(args: impl Iterator<Item = &'a ArgsLike<'v>>) -> Self
    where
        'v: 'a,
    {
        let mut plans: SmallMap<(FrozenArgsId<'v>, usize), Option<u32>> = SmallMap::new();
        let mut next_id = 0u32;

        for arg in args {
            let ArgsLike::FrozenArgs(v, size) = *arg else {
                continue;
            };
            if size == 0 {
                continue;
            }
            plans
                .entry((FrozenArgsId(v), size))
                // We've seen this exact slice before. Promote it to a named
                // variable so it isn't expanded multiple times.
                .and_modify(|value| {
                    if value.is_none() {
                        *value = Some(next_id);
                        next_id += 1;
                    }
                })
                .or_insert(None);
        }

        Self { plans }
    }

    /// Writes Ninja variable definitions for all shared slices.
    pub fn write_shared_args<'m>(
        &self,
        eval: &mut Evaluator<'m, '_, '_>,
        edge: HeapEdge<'m, 'v>,
        visitor: &mut impl ArgVisitor,
    ) -> starlark::Result<()> {
        for ((args_id, size), id) in &self.plans {
            let Some(id) = id else {
                continue;
            };
            visitor.write(("args_", *id, " = "));
            expand_args(&edge.rebrand(args_id.0).arguments[..*size], eval, visitor)?;
            visitor.done();
            visitor.write(b'\n');
        }
        Ok(())
    }

    /// Writes a single argument entry, referencing a shared variable or expanding inline.
    pub fn write<'m>(
        &self,
        arg: &ArgsLike<'v>,
        eval: &mut Evaluator<'m, '_, '_>,
        edge: HeapEdge<'m, 'v>,
        visitor: &mut impl ArgVisitor,
    ) -> starlark::Result<()> {
        match *arg {
            ArgsLike::String(s) => visitor.add_arg(s.as_str()),
            ArgsLike::Args(..) => {
                unreachable!("ArgSharingPlans::write should never see non-frozen Args")
            },
            ArgsLike::FrozenArgs(v, size) => {
                if size == 0 {
                    return Ok(());
                }
                if let Some(id) = self.plans.get(&(FrozenArgsId(v), size)).unwrap() {
                    visitor.write(("$args_", *id, " "));
                } else {
                    expand_args(&edge.rebrand(v).arguments[..size], eval, visitor)?;
                }
            },
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use starlark::{
        environment::{GlobalsBuilder, Module},
        syntax::{AstModule, Dialect},
        values::{UnpackValue as _, Value, list::ListRef, none::NoneType},
    };
    use starlark_derive::starlark_module;

    use super::*;
    use crate::Args;

    #[starlark_module]
    fn register_arg_sharing_test_globals(builder: &mut GlobalsBuilder) {
        fn args<'v>(eval: &mut Evaluator<'v, '_, '_>) -> starlark::Result<Value<'v>> {
            Ok(eval.heap().alloc(Args::default()))
        }

        fn capture<'v>(
            arg: ValueTyped<'v, Args<'v>>,
            eval: &mut Evaluator<'v, '_, '_>,
        ) -> starlark::Result<NoneType> {
            let mut captured = eval
                .module()
                .extra_value()
                .map(|v| ListRef::from_value(v).unwrap().content().to_vec())
                .unwrap_or_default();
            let size = i32::try_from(arg.arguments.borrow().len()).unwrap();
            captured.push(eval.heap().alloc((arg, size)));
            eval.module().set_extra_value(eval.heap().alloc(captured));
            Ok(NoneType)
        }
    }

    fn eval_captured(code: &str) -> Vec<String> {
        let mut globals = GlobalsBuilder::standard();
        register_arg_sharing_test_globals(&mut globals);
        let globals = globals.build();

        let frozen = Module::with_temp_heap(|module| {
            {
                let mut eval = Evaluator::new(&module);
                eval.eval_module(
                    AstModule::parse("//test.scl", code.to_owned(), &Dialect::Standard).unwrap(),
                    &globals,
                )
                .unwrap();
            }
            module.freeze().unwrap().extra_value().unwrap()
        });

        frozen.by_ref_with_reconstructor(|val, reconstructor| {
            let args: Vec<_> = ListRef::from_value(*val)
                .unwrap()
                .iter()
                .map(|item| {
                    let (args, size) =
                        <(ValueTyped<'_, FrozenArgs<'_>>, i32)>::unpack_value_err(item).unwrap();
                    ArgsLike::FrozenArgs(args, usize::try_from(size).unwrap())
                })
                .collect();

            let plans = ArgSharingPlans::new(args.iter());
            Module::with_temp_heap(|module| {
                let mut eval = Evaluator::new(&module);
                let edge = reconstructor.edge(module.heap());
                let mut visitor = String::new();
                plans
                    .write_shared_args(&mut eval, edge, &mut visitor)
                    .unwrap();
                for arg in &args {
                    plans.write(arg, &mut eval, edge, &mut visitor).unwrap();
                }
                visitor.done();
                visitor.split('\n').map(|s| s.to_owned()).collect()
            })
        })
    }

    #[test]
    fn test_plan_arg_sharing_ignores_empty_and_inline_singletons() {
        let shared = eval_captured(
            r#"
empty = args()
later = args()
solo = args().add("--solo")
capture(empty)
capture(later)
capture(solo)
later.add("--added")
capture(empty)
capture(later)
"#,
        );
        assert_eq!(shared, vec!["--solo --added"]);
    }

    #[test]
    fn test_plan_arg_sharing_reuses_same_size_and_separates_extensions() {
        let shared = eval_captured(
            r#"
reused = args().add("reused")
capture(reused)
capture(reused)
lazy = args().add("lazy_a")
capture(lazy)
lazy.add("lazy_b")
capture(lazy)
capture(lazy)
lazy.add("lazy_c")
capture(lazy)
"#,
        );
        assert_eq!(
            shared,
            vec![
                "args_0 = reused",
                "args_1 = lazy_a lazy_b",
                "$args_0 $args_0 lazy_a $args_1 $args_1 lazy_a lazy_b lazy_c",
            ]
        );
    }
}
