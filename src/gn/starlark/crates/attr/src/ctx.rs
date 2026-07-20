// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use starlark::{
    collections::SmallMap,
    values::{
        record::{FieldGen, FrozenRecordType, Record},
        typing::TypeInstanceId,
        FrozenHeap, FrozenValue, FrozenValueTyped, Heap, Value,
    },
};
use types::LabelRef;

use crate::{
    schema::{AllowFilesSchema, AttrKind, AttrSchema},
    value::AttrValue,
    Attr, Session,
};

/// Contains ctx.attr, ctx.files, and ctx.file.
///
/// See https://bazel.build/rules/lib/builtins/ctx for more info on what they are.
pub struct CtxAttr<'v> {
    pub attr: Value<'v>,
    pub files: Value<'v>,
    pub file: Value<'v>,
}

/// The rule attr schema.
///
/// This, roughly speaking, corresponds to the following starlark code:
/// {
///   "foo": attr.label_list(...),
///   "bar": attr.string(...),
/// }
pub struct CtxAttrSchema {
    attrs: SmallMap<String, AttrSchema>,
    attr: FrozenValueTyped<'static, FrozenRecordType>,
    files: FrozenValueTyped<'static, FrozenRecordType>,
    file: FrozenValueTyped<'static, FrozenRecordType>,
}

impl CtxAttrSchema {
    /// Creates a new `CtxAttrSchema`.
    pub fn new(attrs: SmallMap<String, AttrSchema>, heap: &FrozenHeap) -> Self {
        let mut attrs_fields = SmallMap::with_capacity(attrs.len());
        let mut file_fields = SmallMap::new();
        let mut files_fields = SmallMap::new();

        let any = || -> FieldGen<FrozenValue> {
            FieldGen::new(starlark::values::typing::TypeCompiled::any(), None)
        };

        for (name, attr) in &attrs {
            attrs_fields.insert(name.clone(), any());

            if matches!(attr.kind, AttrKind::Label | AttrKind::LabelList) {
                files_fields.insert(name.clone(), any());
            }
            if matches!(attr.allow_files, AllowFilesSchema::Single(_)) {
                file_fields.insert(name.clone(), any());
            }
        }

        Self {
            attrs,
            attr: FrozenValueTyped::new(heap.alloc(FrozenRecordType::new(
                "rule_attr",
                attrs_fields,
                TypeInstanceId::r#gen(),
            )))
            .unwrap(),
            files: FrozenValueTyped::new(heap.alloc(FrozenRecordType::new(
                "rule_files",
                files_fields,
                TypeInstanceId::r#gen(),
            )))
            .unwrap(),
            file: FrozenValueTyped::new(heap.alloc(FrozenRecordType::new(
                "rule_file",
                file_fields,
                TypeInstanceId::r#gen(),
            )))
            .unwrap(),
        }
    }

    /// Constructs the record values for `ctx.attr`, `ctx.file`, and `ctx.files`
    /// from the resolved attribute values.
    pub fn create_ctx_fields<'v, S: Session>(
        &self,
        fields: &[Attr],
        session: &S,
        current_toolchain: &LabelRef,
        heap: &Heap<'v>,
    ) -> starlark::Result<CtxAttr<'v>> {
        let mut ctx_attr = Vec::with_capacity(self.attr.len());
        let mut ctx_files = Vec::with_capacity(self.files.len());
        let mut ctx_file = Vec::with_capacity(self.file.len());

        debug_assert!(fields.len() == self.attrs.len());
        for ((name, schema), attr) in self.attrs.iter().zip(fields.iter()) {
            let AttrValue { attr, file, files } = attr
                .to_value(schema, session, current_toolchain, heap)
                .map_err(|e| e.with_context(format!("for attribute `{name}`")))?;

            ctx_attr.push(attr);

            if let Some(files) = files {
                ctx_files.push(files);
            }

            if let Some(file) = file {
                ctx_file.push(file);
            }
        }

        debug_assert!(ctx_file.len() == self.file.len());
        debug_assert!(ctx_files.len() == self.files.len());

        Ok(CtxAttr {
            attr: heap.alloc(Record::new(
                self.attr.to_value_typed(),
                ctx_attr.into_boxed_slice(),
            )),
            files: heap.alloc(Record::new(
                self.files.to_value_typed(),
                ctx_files.into_boxed_slice(),
            )),
            file: heap.alloc(Record::new(
                self.file.to_value_typed(),
                ctx_file.into_boxed_slice(),
            )),
        })
    }
}

#[cfg(test)]
mod tests {
    use starlark::{environment::GlobalsBuilder, eval::Evaluator, values::list::UnpackList};
    use starlark_derive::starlark_module;
    use testutils::{FakeEvalContext, FakeTarget, FakeTargetRef};
    use types::{EvaluatorContextExt as _, File, Label, PackageRef};

    use super::*;
    use crate::globals::{AttrModule, AttrSpecArgs};

    fn make_attr_schema<'v>(
        kind: crate::AttrKind,
        args: AttrSpecArgs<'v>,
        eval: &mut Evaluator<'v, '_, '_>,
    ) -> starlark::Result<starlark::values::Value<'v>> {
        crate::schema::AttrSchema::create(
            kind,
            args,
            types::PackageRef::root(),
            &types::PathResolver::new_for_testing(),
            &eval.heap(),
        )
    }

    #[starlark_module]
    fn register_ctx_test_globals(builder: &mut GlobalsBuilder) {
        fn to_attrs<'v>(
            schema: SmallMap<String, &'v AttrSchema>,
            values: UnpackList<Value<'v>>,
            eval: &mut Evaluator<'v, '_, '_>,
        ) -> starlark::Result<Value<'v>> {
            let context: &FakeEvalContext = eval.context();
            let fields: Vec<Attr> = schema
                .values()
                .zip(values.items)
                .map(|(attr_schema, val)| {
                    Attr::create(
                        attr_schema,
                        Some(val),
                        context.package.as_ref(),
                        &context.path_resolver,
                    )
                })
                .collect::<Result<_, _>>()?;

            let ctx = CtxAttrSchema::new(
                schema.into_iter().map(|(k, v)| (k, (*v).clone())).collect(),
                eval.frozen_heap(),
            )
            .create_ctx_fields(
                &fields,
                &context.session,
                &context.current_toolchain.as_ref(),
                &eval.heap(),
            )?;

            Ok(eval.heap().alloc((ctx.attr, ctx.files, ctx.file)))
        }
    }

    #[test]
    fn test_create_ctx_fields() {
        let mut a = testutils::Assert::default();

        let target_label = Label::new(PackageRef::root().to_owned(), "bar".to_owned());
        let file1 = File::intern("out.cc");
        let target_bar = FakeTargetRef::new(FakeTarget {
            outputs: vec![file1.clone()],
            ..Default::default()
        });
        a.context().session.insert_target(target_label, target_bar);

        a.modify_globals(|builder| {
            builder.set("attr", AttrModule { make_attr_schema });
            register_ctx_test_globals(builder);
        });

        a.pass(
            r#"
attrs, files, file = to_attrs(
    schema = {
        "srcs": attr.label_list(),
        "hdr": attr.label(allow_single_file = True),
        "bool_attr": attr.bool(),
    },
    values = [
        [":bar"],
        ":bar",
        True,
    ],
)

def assert_eq(a, b):
    if a != b:
        fail("expected %s, got %s" % (b, a))

assert_eq(attrs.bool_attr, True)
assert_eq(files.srcs, [make_file("out.cc")])
assert_eq(files.hdr, [make_file("out.cc")])
assert_eq(file.hdr, make_file("out.cc"))

if hasattr(files, "bool_attr"):
    fail("files should not have bool_attr")

if hasattr(file, "bool_attr"):
    fail("file should not have bool_attr")
"#,
        );
    }
}
