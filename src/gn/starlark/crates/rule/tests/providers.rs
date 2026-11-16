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
