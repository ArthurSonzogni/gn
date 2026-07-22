// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{fmt, marker::PhantomData};

use allocative::Allocative;
use attr::{traits::EvalContextAttrExt, Attr, CtxAttrSchema};
use starlark::{
    any::ProvidesStaticType,
    collections::SmallMap,
    eval::{Arguments, Evaluator, ParametersSpec},
    values::{FrozenHeap, FrozenValue, StarlarkValue, Value},
};
use starlark_derive::{starlark_value, NoSerialize};
use types::{EvaluatorContextExt, Scope, TargetRef};

use crate::rule::{build_signature, OutputType};

/// A frozen representation of a Starlark rule object.
///
/// Once a rule has been exported from a loaded Starlark module (e.g., from
/// `.bzl` files), it is frozen into a `FrozenRule` and is ready to be invoked
/// in target build files.
#[derive(NoSerialize, Allocative)]
pub struct FrozenRule<C: EvalContextAttrExt> {
    pub(crate) schema: CtxAttrSchema,
    pub(crate) builtin: Option<OutputType>,
    pub(crate) implementation: FrozenValue,
    pub(crate) name: String,
    pub(crate) signature: ParametersSpec<FrozenValue>,
    pub(crate) parent: Option<&'static FrozenRule<C>>,
    pub(crate) _phantom: PhantomData<C>,
}

// Safety: FrozenRule does not automatically derive Send and Sync because of C.
// But it only contains C inside PhantomData, so Send and Sync are actually
// safe.
unsafe impl<C: EvalContextAttrExt> Send for FrozenRule<C> {}
unsafe impl<C: EvalContextAttrExt> Sync for FrozenRule<C> {}

unsafe impl<'v, C: EvalContextAttrExt> ProvidesStaticType<'v> for FrozenRule<C> {
    type StaticType = FrozenRule<C>;
}

unsafe impl<'v, C: EvalContextAttrExt> starlark::values::Trace<'v> for FrozenRule<C> {
    fn trace(&mut self, _tracer: &starlark::values::Tracer<'v>) {}
}

impl<C: EvalContextAttrExt> fmt::Debug for FrozenRule<C> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("FrozenRule")
            .field("schema", &self.schema)
            .field("builtin", &self.builtin)
            .field("parent", &self.parent)
            .finish()
    }
}

impl<C: EvalContextAttrExt> FrozenRule<C> {
    /// Creates a new `FrozenRule` for a built-in rule.
    pub fn new_builtin(builtin: OutputType, frozen_heap: &FrozenHeap) -> Self {
        let schema = CtxAttrSchema::new(SmallMap::new(), Some(builtin), frozen_heap);
        let name: &str = builtin.into();
        let signature = build_signature(name, &schema, true);
        Self {
            schema,
            builtin: Some(builtin),
            implementation: FrozenValue::new_none(),
            name: name.to_owned(),
            signature,
            parent: None,
            _phantom: PhantomData,
        }
    }
}

#[starlark_value(type = "rule")]
impl<'v, C: EvalContextAttrExt> StarlarkValue<'v> for FrozenRule<C>
where
    Self: ProvidesStaticType<'v>,
{
    type Canonical = Self;

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
        let me = me.unpack_frozen().unwrap();
        let signature = &self.signature;

        signature.parser(args, eval, |param_parser, eval| {
            let target_name: &str = param_parser.next()?;

            let context = eval.context::<C>();
            let scope = context.require_macro()?;
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

            let target = if let Some(builtin) = self.builtin {
                // Collect all the arguments we don't recognise and pass them to the native
                // implementation.
                let kwargs: SmallMap<String, Value<'v>> = param_parser.next()?;
                let child_scope = scope.copy_with(kwargs.iter().map(|(k, v)| (k.as_str(), *v)));
                context.create_target(Some(builtin), target_name, &child_scope, me, attrs)?
            } else {
                context.create_target(None, target_name, scope, me, attrs)?
            };
            target.register_dependencies(context.session(), context.current_toolchain());

            Ok(Value::new_none())
        })
    }
}

impl<C: EvalContextAttrExt> starlark::values::AllocFrozenValue for FrozenRule<C> {
    #[inline]
    fn alloc_frozen_value(
        self,
        heap: &starlark::values::FrozenHeap,
    ) -> starlark::values::FrozenValue {
        heap.alloc_simple(self)
    }
}

impl<C: EvalContextAttrExt> fmt::Display for FrozenRule<C> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "<rule: {}>", &self.name)
    }
}

#[cfg(test)]
mod tests {
    use std::{
        collections::{HashMap, HashSet},
        sync::Mutex,
    };

    use attr::{Attr, LabelOrFile};
    use starlark::environment::FrozenModule;
    use testutils::FakeTarget;
    use types::{Label, OutputType, PackageRef, Session};

    use crate::globals::tests::new_assert;

    #[test]
    fn test_pure_rule_inheritance() {
        let mut assert = new_assert();
        let native = assert.load_module("//rules:native.scl");
        let pure = assert.load_module("//rules:pure.scl");

        let rule = |module: &FrozenModule, name: &str| {
            module.get(name).unwrap().value().unpack_frozen().unwrap()
        };

        assert.pass(
            r#"
load("//rules:pure.scl", "child_rule", "parent_rule")
load("//rules:native.scl", "custom_shared_library", "static_library")

custom_shared_library(
    name = "shared_library",
    mandatory = "mandatory_val",
    optional = "optional_val",
    unknown = "unknown",
)

static_library(
   name = "static_library",
   optional = "optional_val",
   unknown = "unknown"
)

parent_rule(
    name = "parent_defaulted",
    parent_only = "p",
)

child_rule(
    name = "child_defaulted",
    parent_only = "p",
    child_only = "c",
)

child_rule(
    name = "child_override",
    parent_only = "parent_val",
    child_only = "child_val",
    override = "//:custom_val",
)
"#,
        );

        let heap = starlark::values::FrozenHeap::new();
        let mut unknown_attrs = HashMap::new();
        unknown_attrs.insert(
            "unknown".to_owned(),
            starlark::values::Value::new_frozen(heap.alloc("unknown")),
        );

        let context = assert.context();
        let load = |name: &str| {
            let label = Label::new(PackageRef::root().to_owned(), name.to_owned());
            context
                .session
                .get_target(label.as_ref(), context.session.default_toolchain.as_ref())
        };

        assert_eq!(
            *load("shared_library"),
            FakeTarget {
                label: Label::new(PackageRef::root().to_owned(), "shared_library".to_owned()),
                toolchain: context.session.default_toolchain.clone(),
                outputs: vec![],
                attrs: vec![
                    Attr::String("optional_val".to_owned()),
                    Attr::String("mandatory_val".to_owned()),
                ],
                output_type: Some(OutputType::SharedLibrary),
                rule: rule(&native, "custom_shared_library"),
                cxx_attrs: unknown_attrs.clone(),
                dependencies: Mutex::new(HashSet::new()),
            }
        );

        assert_eq!(
            *load("static_library"),
            FakeTarget {
                label: Label::new(PackageRef::root().to_owned(), "static_library".to_owned()),
                toolchain: context.session.default_toolchain.clone(),
                outputs: vec![],
                attrs: vec![Attr::String("optional_val".to_owned())],
                output_type: Some(OutputType::StaticLibrary),
                rule: rule(&native, "static_library"),
                cxx_attrs: unknown_attrs.clone(),
                dependencies: Mutex::new(HashSet::new()),
            }
        );

        let toolchain = Label::new(
            PackageRef::root().to_owned(),
            "default_toolchain".to_owned(),
        );

        assert_eq!(
            *load("parent_defaulted"),
            FakeTarget {
                label: Label::new(PackageRef::root().to_owned(), "parent_defaulted".to_owned()),
                toolchain: toolchain.clone(),
                outputs: vec![],
                attrs: vec![
                    Attr::String("p".to_owned()),
                    Attr::Label(Some(LabelOrFile::Label(Label::new(
                        PackageRef::root().to_owned(),
                        "parent".to_owned()
                    )))),
                ],
                output_type: None,
                rule: rule(&pure, "parent_rule"),
                cxx_attrs: HashMap::new(),
                dependencies: Mutex::new(HashSet::from([(
                    Label::new(PackageRef::root().to_owned(), "parent".to_owned()),
                    toolchain.clone(),
                )])),
            }
        );

        assert_eq!(
            *load("child_defaulted"),
            FakeTarget {
                label: Label::new(PackageRef::root().to_owned(), "child_defaulted".to_owned()),
                toolchain: toolchain.clone(),
                outputs: vec![],
                attrs: vec![
                    Attr::String("p".to_owned()),
                    Attr::Label(Some(LabelOrFile::Label(Label::new(
                        PackageRef::root().to_owned(),
                        "child".to_owned()
                    )))),
                    Attr::String("c".to_owned()),
                ],
                output_type: None,
                rule: rule(&pure, "child_rule"),
                cxx_attrs: HashMap::new(),
                dependencies: Mutex::new(HashSet::from([(
                    Label::new(PackageRef::root().to_owned(), "child".to_owned()),
                    toolchain.clone(),
                )])),
            }
        );

        assert_eq!(
            *load("child_override"),
            FakeTarget {
                label: Label::new(PackageRef::root().to_owned(), "child_override".to_owned()),
                toolchain: toolchain.clone(),
                outputs: vec![],
                attrs: vec![
                    Attr::String("parent_val".to_owned()),
                    Attr::Label(Some(LabelOrFile::Label(Label::new(
                        PackageRef::root().to_owned(),
                        "custom_val".to_owned()
                    )))),
                    Attr::String("child_val".to_owned()),
                ],
                output_type: None,
                rule: rule(&pure, "child_rule"),
                cxx_attrs: HashMap::new(),
                dependencies: Mutex::new(HashSet::from([(
                    Label::new(PackageRef::root().to_owned(), "custom_val".to_owned()),
                    toolchain.clone(),
                )])),
            }
        );
    }
}
