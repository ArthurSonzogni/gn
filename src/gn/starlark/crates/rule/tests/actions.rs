// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::path::Path;

use anyhow::Context as _;
use rule::run;
use tempfile::tempdir;
use testutils::Assert;
use types::{Label, PackageRef, Session};

#[test]
fn test_actions_suite() {
    let _temp_dir = tempdir().unwrap();
    let mut assert = Assert::new_rule_assert();
    assert.modify_globals(|builder| {
        depset::depset_globals!(builder, testutils::FakeEvalContext);
    });
    assert.load_module("//rules:actions.scl");
    assert.pass(
        r#"
load(
    "//rules:actions.scl",
    "already_generated",
    "duplicate_declare_file",
    "generated_file",
    "label",
    "undeclared_output",
    "write",
)

write(name = "write")

label(name = "label")

generated_file(
    name = "empty_filename",
    files = {"": ""}
)

generated_file(
    name = "simple_filename",
    files = {"simple/filename": "content"}
)

generated_file(
    name = "relative_file",
    files = {"../dummy": ""},
)

generated_file(
    name = "backslashes",
    files = {"with\\backslashes": ""},
)

generated_file(
    name = "never_generated",
    files = {"dummy": ""},
)

duplicate_declare_file(name = "duplicate_declare_file")

already_generated(name = "already_generated")

undeclared_output(name = "undeclared_output", src = "foo/file.cc")
"#,
    );

    type OutputFile<'a> = (&'a Path, &'a str);
    type TestCase<'a> = (&'a str, Result<(&'a str, &'a [OutputFile<'a>]), &'a str>);

    let test_cases: &[TestCase<'_>] = &[
        (
            "write",
            Ok((
                "obj/write/declared.txt",
                &[
                    (Path::new("obj/write/declared.txt"), "//:write"),
                    (Path::new("obj/write/undeclared.txt"), "undeclared"),
                ],
            )),
        ),
        (
            "label",
            Ok(("", &[(Path::new("obj/label/label.txt"), "label")])),
        ),
        (
            "simple_filename",
            Ok((
                "obj/simple_filename/simple/filename",
                &[(Path::new("obj/simple_filename/simple/filename"), "content")],
            )),
        ),
        (
            "empty_filename",
            Err("Invalid ctx.actions.declare_file filename: \"\""),
        ),
        (
            "relative_file",
            Err("Invalid ctx.actions.declare_file filename: \"../dummy\""),
        ),
        (
            "backslashes",
            Err("Invalid ctx.actions.declare_file filename: \"with\\\\backslashes\""),
        ),
        ("duplicate_declare_file", Err("was already declared")),
        ("already_generated", Err("has already been generated")),
        ("undeclared_output", Err("was not declared by this target")),
        ("never_generated", Err("was never generated")),
    ];

    let context = assert.context();
    for &(target_name, expected) in test_cases {
        let label = Label::new(PackageRef::root().to_owned(), target_name.to_owned());
        let target = context
            .session
            .get_target(label.as_ref(), context.session.default_toolchain.as_ref());
        let session = assert.session();
        let res = run(&target, move |t: &testutils::FakeTargetRef| {
            testutils::FakeEvalContext::rule_impl(session.clone(), t.clone())
        });

        match expected {
            Ok((defaultinfo_phony, files)) => {
                let rooted = res.expect(target_name);
                let providers = providers::Providers::try_from(rooted).expect(target_name);

                if defaultinfo_phony.is_empty() {
                    assert_eq!(providers.outputs_phony, None);
                } else {
                    assert_eq!(
                        providers.outputs_phony.unwrap().as_path(),
                        Path::new(defaultinfo_phony)
                    );
                }

                for &(file_path, expected_content) in files {
                    let resolved = context
                        .path_resolver
                        .resolve(&types::File::intern(&file_path.to_string_lossy()));
                    let content = std::fs::read_to_string(&resolved)
                        .context(resolved.display().to_string())
                        .unwrap();
                    assert_eq!(content, expected_content);
                }
            },
            Err(expected_error) => {
                let err = res.expect_err(target_name);
                assert!(
                    err.to_string().contains(expected_error),
                    "Target '{target_name}' error '{err}' did not contain expected substring '{expected_error}'"
                );
            },
        }
    }
}
