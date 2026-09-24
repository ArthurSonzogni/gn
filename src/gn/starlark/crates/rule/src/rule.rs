// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{cell::OnceCell, fmt, marker::PhantomData};

use allocative::Allocative;
use attr::{traits::EvalContextAttrExt, Attr, AttrSchema, CtxAttrSchema};
use starlark::{
    any::ProvidesStaticType,
    collections::SmallMap,
    eval::{Arguments, Evaluator, ParametersSpec, ParametersSpecParam},
    values::{
        AllocFrozenValue, AllocValue, Freeze, FreezeResult, Freezer, FrozenHeap, Heap, HeapEdge,
        StarlarkValue, Trace, Value, ValueTyped,
    },
};
use starlark_derive::{starlark_value, NoSerialize};
pub use types::OutputType;
use types::{EvaluatorContextExt, Scope};

/// Representation of a Starlark rule object.
///
/// Rules represent target definitions in GN (e.g., `executable`, `source_set`,
/// or custom rules declared via `rule()`).
///
/// The generic parameter `C` represents the execution context. This is required
/// because although we don't store it in the rule object itself, `invoke`
/// requires a concrete execution context.
#[derive(Allocative, NoSerialize, ProvidesStaticType, Trace)]
#[trace(bound = "C: EvalContextAttrExt")]
pub struct Rule<'v, C: EvalContextAttrExt + 'static> {
    pub(crate) schema: CtxAttrSchema<'v>,
    // Filled after `export_as` is called (or at creation for builtins).
    // Contains the name of the rule, and the signature required to call it.
    #[trace(static)]
    pub(crate) once_named: OnceCell<(String, ParametersSpec<Value<'static>>)>,
    pub(crate) builtin: Option<OutputType>,
    pub(crate) implementation: Value<'v>,
    pub(crate) parent: Option<ValueTyped<'v, Rule<'v, C>>>,
    pub(crate) _phantom: PhantomData<C>,
}

// Safety: Rule does not automatically derive Send and Sync because of C.
// But it only contains C inside PhantomData, so Send and Sync are actually
// safe.
unsafe impl<'v, C: EvalContextAttrExt> Send for Rule<'v, C> {}
unsafe impl<'v, C: EvalContextAttrExt> Sync for Rule<'v, C> {}

impl<'v, C: EvalContextAttrExt> fmt::Debug for Rule<'v, C> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt::Display::fmt(self, f)
    }
}

/// Some reserved attributes are currently in use (eg. name).
/// Others are reserved for future use, so we disallow them in case we want to
/// support them in the future.
const RESERVED_ATTRS: &[&str] = &[
    "deps",
    "name",
    "public",
    "public_deps",
    "sources",
    "testonly",
    "visibility",
];

fn merge_attrs(
    parent_attrs: &SmallMap<String, AttrSchema>,
    mut child_attrs: SmallMap<String, AttrSchema>,
) -> Result<SmallMap<String, AttrSchema>, starlark::Error> {
    let mut merged = SmallMap::with_capacity(parent_attrs.len() + child_attrs.len());

    // Order matters. Parent attributes need to be added first. This allows the
    // static_library builtin, for example, to declare attrs = `{"public": ...,
    // "sources": ...}`, then it can simply set `values = [target.public(),
    // target.sources(), starlark parameters]`
    for (name, parent_schema) in parent_attrs {
        if let Some(child_schema) = child_attrs.shift_remove(name) {
            parent_schema.check_override(name, &child_schema)?;
            merged.insert(name.clone(), child_schema);
        } else {
            merged.insert(name.clone(), parent_schema.clone());
        }
    }

    for (name, child_schema) in child_attrs {
        merged.insert(name, child_schema);
    }

    Ok(merged)
}

impl<'v, C: EvalContextAttrExt> Rule<'v, C> {
    /// Creates a new `Rule` with the given attributes, implementation, and
    /// parent.
    pub fn new<'fh>(
        attrs: SmallMap<String, AttrSchema>,
        builtin: Option<OutputType>,
        parent: Option<ValueTyped<'v, Rule<'v, C>>>,
        implementation: Value<'v>,
        frozen_heap: &FrozenHeap<'fh>,
        edge: HeapEdge<'v, 'fh>,
    ) -> Result<Self, starlark::Error> {
        for name in attrs.keys() {
            if RESERVED_ATTRS.contains(&name.as_str()) {
                return Err(crate::Error::ReservedAttribute(name.clone()).into());
            }
        }

        let mut builtin = builtin;
        let parent_schema = match parent {
            Some(parent_rule) => {
                if builtin.is_none() {
                    builtin = parent_rule.builtin;
                }
                Some(&parent_rule.as_ref().schema)
            },
            None => None,
        };

        let attrs = match parent_schema {
            Some(parent_schema) => merge_attrs(parent_schema.attrs(), attrs)?,
            None => attrs,
        };

        let schema = edge.rebrand(CtxAttrSchema::new(attrs, builtin, frozen_heap));
        Ok(Self {
            schema,
            once_named: Default::default(),
            builtin,
            implementation,
            parent,
            _phantom: PhantomData,
        })
    }

    /// Creates a new `Rule` for a built-in rule.
    pub fn new_builtin(builtin: OutputType, frozen_heap: &FrozenHeap<'v>) -> Self {
        let schema = CtxAttrSchema::new(SmallMap::new(), Some(builtin), frozen_heap);
        let name: &str = builtin.into();
        let signature = build_signature(name, &schema, true);
        let once_named = OnceCell::new();
        let _ = once_named.set((name.to_owned(), signature));
        Self {
            schema,
            once_named,
            builtin: Some(builtin),
            implementation: Value::new_none(),
            parent: None,
            _phantom: PhantomData,
        }
    }

    pub fn has_implementation(&self) -> bool {
        !self.implementation.is_none()
    }
}

pub(crate) fn build_signature(
    name: &str,
    schema: &CtxAttrSchema<'_>,
    is_builtin: bool,
) -> ParametersSpec<Value<'static>> {
    let named_only: Vec<_> = std::iter::once(("name", ParametersSpecParam::Required))
        .chain(
            schema
                .attrs()
                .iter()
                .map(|(k, attr)| (k.as_str(), attr.as_param_spec())),
        )
        .collect();

    ParametersSpec::new_parts(name, vec![], vec![], false, named_only, is_builtin)
}

#[starlark_value(type = "rule")]
impl<'v, C: EvalContextAttrExt> StarlarkValue<'v> for Rule<'v, C>
where
    Self: ProvidesStaticType<'v>,
{
    type Canonical = Self;

    fn export_as(
        &self,
        variable_name: &str,
        _eval: &mut Evaluator<'v, '_, '_>,
    ) -> starlark::Result<()> {
        let signature = build_signature(variable_name, &self.schema, self.builtin.is_some());
        let _ = self.once_named.set((variable_name.to_owned(), signature));
        Ok(())
    }

    /// Invoking a rule generates a target.
    /// Note: This is only used when calling my_rule(...) from *starlark*.
    /// When calling from GN directly, we use custom logic to handle scoping
    /// correctly.
    fn invoke(
        &self,
        me: Value<'v>,
        args: &Arguments<'v, '_>,
        eval: &mut Evaluator<'v, '_, '_>,
    ) -> starlark::Result<Value<'v>> {
        let (_name, signature) = self.once_named.get().ok_or(crate::Error::RuleMustBeNamed)?;

        // Rules can only be invoked in macro contexts.
        eval.context::<C>().require_macro()?;

        // Safety: Because rules cannot be created and invoked in the same module (creation requires
        // a .bzl module and invocation requires a macro module), they must be frozen.
        debug_assert!(me.is_frozen(), "Rule must be frozen before invocation");
        // Safety: `me` is receiver of type `Rule`, stored in a frozen heap.
        // Because:
        // * The rule must be frozen in a .bzl module.
        // * .bzl modules are stored in the loader.
        // * The loader doesn't allow overwriting modules
        // * BuildSettings stores Session, which stores Loader, so the loader lives forever.
        // The module containing the rule must thus live forever.
        let me: Value<'static> = unsafe { std::mem::transmute(me) };
        let signature = HeapEdge::immortal().rebrand_ref(signature);

        signature.parser(args, eval, |param_parser, eval| {
            let target_name: &str = param_parser.next()?;

            let context = eval.context::<C>();
            let mut scope = context.require_macro()?;
            let package = context.current_package();
            let path_resolver = context.path_resolver();

            let attrs = self
                .schema
                .attrs()
                .iter()
                .map(|(_name, schema)| {
                    let value_opt: Option<Value<'v>> = param_parser.next_opt()?;
                    Attr::create(schema, value_opt, package, path_resolver)
                })
                .collect::<Result<Vec<_>, _>>()?;

            let mut cxx_target = if let Some(builtin) = self.builtin {
                // Collect all the arguments we don't recognise and pass them to the native
                // implementation.
                let kwargs: SmallMap<String, Value<'v>> = param_parser.next()?;
                let mut child_scope = scope
                    .as_mut()
                    .copy_with(kwargs.iter().map(|(k, v)| (k.as_str(), *v)))?;
                context.create_target(
                    Some(builtin),
                    target_name,
                    C::Scope::as_pin_mut(&mut child_scope),
                )?
            } else {
                let mut child_scope = scope.as_mut().copy_with(std::iter::empty())?;
                context.create_target(None, target_name, C::Scope::as_pin_mut(&mut child_scope))?
            };
            let mut deps = starlark::collections::SmallSet::new();
            for attr in &attrs {
                attr.add_dependencies(context.current_toolchain(), &mut deps);
            }
            for (label, toolchain) in deps {
                types::TargetMut::register_dependency(cxx_target.as_mut(), label, toolchain);
            }
            context.register_target(cxx_target.into_ref().get_ref(), me, attrs)?;

            Ok(Value::new_none())
        })
    }
}

impl<'v, C: EvalContextAttrExt> Freeze<'v> for Rule<'v, C> {
    type Frozen<'fv> = Rule<'fv, C>;

    fn freeze<'fv>(self, freezer: &Freezer<'v, 'fv>) -> FreezeResult<Self::Frozen<'fv>> {
        if self.once_named.get().is_none() {
            return Err(crate::Error::RuleMustBeNamed.into());
        }
        Ok(Rule {
            schema: self.schema.freeze(freezer)?,
            once_named: self.once_named,
            builtin: self.builtin,
            implementation: self.implementation.freeze(freezer)?,
            parent: self.parent.freeze(freezer)?,
            _phantom: PhantomData,
        })
    }
}

impl<'v, C: EvalContextAttrExt> AllocValue<'v> for Rule<'v, C> {
    #[inline]
    fn alloc_value(self, heap: Heap<'v>) -> Value<'v> {
        heap.alloc_complex(self)
    }
}

impl<'fv, C: EvalContextAttrExt> AllocFrozenValue<'fv> for Rule<'fv, C> {
    #[inline]
    fn alloc_frozen_value(self, heap: FrozenHeap<'fv>) -> Value<'fv> {
        heap.alloc_simple_typed(self).to_value()
    }
}

impl<'v, C: EvalContextAttrExt> fmt::Display for Rule<'v, C> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        if let Some((name, _)) = self.once_named.get() {
            write!(f, "<rule: {name}>")
        } else {
            write!(f, "<anonymous rule>")
        }
    }
}
