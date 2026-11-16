// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use rule::run;
use testutils::{Assert, FakeEvalContext};
use types::{LabelRef, PackageRef, Session as _};

#[test]
fn test_provides() {
    let mut assert = Assert::new_rule_assert();
    let module = assert.module(
        "//pkg:rules.scl",
        r#"
ParentInfo = provider(fields = [])
ChildInfo = provider(fields = [])

def _impl(ctx):
    providers = {
        "ParentInfo": ParentInfo(),
        "ChildInfo": ChildInfo(),
    }
    return [providers[name] for name in ctx.attr.providers]

parent = rule(
    implementation = _impl,
    provides = [ParentInfo],
)

child = rule(
    implementation = _impl,
    parent = parent,
    attrs = {
        "providers": attr.string_list(mandatory = True),
    },
    provides = [ChildInfo],
)
"#,
    );
    assert.module_add(module);
    assert.pass(
        r#"
load("//pkg:rules.scl", "child")

child(
    name = "all",
    providers = ["ParentInfo", "ChildInfo"],
)

child(
    name = "missing_parent",
    providers = ["ChildInfo"],
)

child(
    name = "missing_child",
    providers = ["ParentInfo"],
)
"#,
    );

    let context = assert.context();
    for (name, expected_err) in [
        ("all", None),
        (
            "missing_parent",
            Some(
                "Rule 'child' on target '//:missing_parent' did not return declared provider: ParentInfo",
            ),
        ),
        (
            "missing_child",
            Some(
                "Rule 'child' on target '//:missing_child' did not return declared provider: ChildInfo",
            ),
        ),
    ] {
        let target = context.session.get_target(
            LabelRef::new(PackageRef::root(), name),
            context.session.default_toolchain.as_ref(),
        );
        let session = assert.session();
        let res = run(&target, move |t: &testutils::FakeTargetRef| {
            FakeEvalContext::rule_impl(session.clone(), t.clone())
        });
        match expected_err {
            None => {
                res.unwrap();
            },
            Some(expected) => assert_eq!(res.unwrap_err().to_string(), expected),
        }
    }
}

#[test]
fn test_required_providers() {
    let mut assert = Assert::new_rule_assert();
    let module = assert.module(
        "//pkg:req_rules.scl",
        r#"
FooInfo = provider(fields = ["x"])
BarInfo = provider(fields = ["y"])
BazInfo = provider(fields = ["z"])

def _impl(ctx):
    providers = {
        "FooInfo": FooInfo(),
        "BarInfo": BarInfo(),
        "BazInfo": BazInfo(),
    }
    return [providers[name] for name in ctx.attr.providers]

provides = rule(
    implementation = _impl,
    attrs = {"providers": attr.string_list()},
)

def _impl(ctx):
    return []

req_rule = rule(
    implementation = _impl,
    attrs = {
        "dep": attr.label(allow_files = True, providers = [FooInfo, BarInfo]),
        "or_dep": attr.label(providers = [[FooInfo], [BarInfo, BazInfo]]),
        "label_list": attr.label_list(providers = [FooInfo]),
        "label_keyed_dict": attr.label_keyed_string_dict(providers = [FooInfo]),
        "label_list_dict": attr.label_list_dict(providers = [FooInfo]),
        "string_keyed_dict": attr.string_keyed_label_dict(providers = [FooInfo]),
    },
)
"#,
    );
    assert.module_add(module);
    assert.pass(
        r#"
load("//pkg:req_rules.scl", "provides", "req_rule")

provides(name = "none")
provides(name = "foo", providers = ["FooInfo"])
provides(name = "bar", providers = ["BarInfo"])
provides(name = "baz", providers = ["BazInfo"])
provides(name = "foo_bar", providers = ["FooInfo", "BarInfo"])
provides(name = "bar_baz", providers = ["BarInfo", "BazInfo"])

req_rule(
    name = "all_ok",
    dep = ":foo_bar",
    or_dep = ":foo",
    label_list = [":foo"],
    label_keyed_dict = {":foo": "k"},
    label_list_dict = {"k": [":foo"]},
    string_keyed_dict = {"k": ":foo"},
)
req_rule(name = "defaults_ok")
req_rule(name = "file_ok", dep = "foo/file.cc")
req_rule(name = "or_bar_baz_ok", or_dep = ":bar_baz")

req_rule(name = "single_missing", dep = ":foo")
req_rule(name = "single_missing_both", dep = ":none")
req_rule(name = "or_missing", or_dep = ":none")
req_rule(name = "or_partial_missing", or_dep = ":bar")
req_rule(name = "list_missing", label_list = [":none"])
req_rule(name = "keyed_missing", label_keyed_dict = {":none": "k"})
req_rule(name = "list_dict_missing", label_list_dict = {"k": [":none"]})
req_rule(name = "string_keyed_missing", string_keyed_dict = {"k": ":none"})
"#,
    );

    let context = assert.context();
    for (name, expected_err) in [
        ("none", None),
        ("foo", None),
        ("bar", None),
        ("baz", None),
        ("foo_bar", None),
        ("bar_baz", None),
        ("all_ok", None),
        ("defaults_ok", None),
        ("file_ok", None),
        ("or_bar_baz_ok", None),
        (
            "single_missing",
            Some(
                "for attribute `dep`: target `//:foo` does not have mandatory providers: 'BarInfo'",
            ),
        ),
        (
            "single_missing_both",
            Some(
                "for attribute `dep`: target `//:none` does not have mandatory providers: ('FooInfo' and 'BarInfo')",
            ),
        ),
        (
            "or_missing",
            Some(
                "for attribute `or_dep`: target `//:none` does not have mandatory providers: 'FooInfo' or ('BarInfo' and 'BazInfo')",
            ),
        ),
        (
            "or_partial_missing",
            Some(
                "for attribute `or_dep`: target `//:bar` does not have mandatory providers: 'FooInfo' or 'BazInfo'",
            ),
        ),
        (
            "list_missing",
            Some(
                "for attribute `label_list`: target `//:none` does not have mandatory providers: 'FooInfo'",
            ),
        ),
        (
            "keyed_missing",
            Some(
                "for attribute `label_keyed_dict`: target `//:none` does not have mandatory providers: 'FooInfo'",
            ),
        ),
        (
            "list_dict_missing",
            Some(
                "for attribute `label_list_dict`: target `//:none` does not have mandatory providers: 'FooInfo'",
            ),
        ),
        (
            "string_keyed_missing",
            Some(
                "for attribute `string_keyed_dict`: target `//:none` does not have mandatory providers: 'FooInfo'",
            ),
        ),
    ] {
        let target = context.session.get_target(
            LabelRef::new(PackageRef::root(), name),
            context.session.default_toolchain.as_ref(),
        );
        let session = assert.session();
        let res = run(&target, move |t: &testutils::FakeTargetRef| {
            FakeEvalContext::rule_impl(session.clone(), t.clone())
        });
        match expected_err {
            None => {
                target
                    .evaluated
                    .set(res.unwrap())
                    .expect("target evaluated twice");
            },
            Some(expected) => assert_eq!(format!("{:#}", res.unwrap_err()), expected),
        }
    }
}
