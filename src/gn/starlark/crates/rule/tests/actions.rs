// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::path::Path;

use anyhow::Context as _;
use either::Either;
use rule::run;
use tempfile::tempdir;
use testutils::Assert;
use types::{File, Label, PackageRef, Session as _};

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
                let evaluated = res.expect(target_name);

                evaluated.by_ref(|evaluated| {
                    if defaultinfo_phony.is_empty() {
                        assert_eq!(evaluated.providers.outputs_phony, None);
                    } else {
                        assert_eq!(
                            evaluated.providers.outputs_phony.unwrap().as_path(),
                            Path::new(defaultinfo_phony)
                        );
                    }
                });

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

#[test]
fn test_actions_run() {
    let _temp_dir = tempdir().unwrap();
    let mut assert = Assert::new_rule_assert();
    assert.modify_globals(|builder| {
        depset::depset_globals!(builder, testutils::FakeEvalContext);
    });
    assert.load_module("//rules:actions.scl");
    assert.pass(
        r#"
load("//rules:actions.scl", "run_action")

run_action(name = "run_action", src = "foo/file.cc")
"#,
    );

    let context = assert.context();
    let session = assert.session();
    let evaluated = run(
        &context.session.get_target(
            Label::new(PackageRef::root().to_owned(), "run_action".to_owned()).as_ref(),
            context.session.default_toolchain.as_ref(),
        ),
        move |t: &testutils::FakeTargetRef| {
            testutils::FakeEvalContext::rule_impl(session.clone(), t.clone())
        },
    )
    .unwrap();

    evaluated.by_ref_with_reconstructor(|evaluated, reconstructor| {
        assert_eq!(
            evaluated.providers.outputs_phony.unwrap().as_path(),
            Path::new("obj/run_action/run_action.o")
        );

        assert_eq!(evaluated.actions.len(), 2);

        let action = &evaluated.actions[0];
        assert_eq!(
            action.outputs,
            &[File::intern("obj/run_action/run_action.o")]
        );
        assert_eq!(
            action.inputs,
            &[
                File::intern("../../foo/file.cc"),
                File::intern("obj/run_action/run_action_tool"),
            ]
        );
        assert_eq!(
            action.executable.as_ref().map_right(|s| s.as_str()),
            Either::Right("my_compiler")
        );
        assert_eq!(
            action.mnemonic.as_ref().map(|s| s.as_str()),
            Some("MYCOMPILE")
        );
        action
            .arguments
            .assert_eq(&reconstructor, &["--flag", "-c", "../../foo/file.cc"]);

        let action = &evaluated.actions[1];
        assert_eq!(
            action.outputs,
            &[File::intern("obj/run_action/run_action_exec.o")]
        );
        assert_eq!(
            action.inputs,
            &[
                File::intern("../../foo/file.cc"),
                File::intern("obj/run_action/run_action_tool"),
            ]
        );
        assert_eq!(
            action.executable,
            Either::Left(File::intern("../../foo/file.cc"))
        );
        assert_eq!(action.mnemonic, None);
        action.arguments.assert_eq(&reconstructor, &["--test"]);
    });
}

#[test]
fn test_actions_run_errors() {
    let _temp_dir = tempdir().unwrap();
    let mut assert = Assert::new_rule_assert();
    assert.modify_globals(|builder| {
        depset::depset_globals!(builder, testutils::FakeEvalContext);
    });
    assert.load_module("//rules:actions.scl");
    assert.pass(
        r#"
load("//rules:actions.scl", "invalid_action")

invalid_action(name = "empty_outputs")
invalid_action(name = "undeclared_output", src = "foo/file.cc")
invalid_action(name = "duplicate_output")
invalid_action(name = "invalid_mnemonic")
"#,
    );

    let context = assert.context();

    let error_cases: &[(&str, &str)] = &[
        ("empty_outputs", "outputs cannot be empty"),
        ("undeclared_output", "was not declared by this target"),
        ("duplicate_output", "has already been generated"),
        (
            "invalid_mnemonic",
            "mnemonic must be an uppercase letter followed by uppercase letters or digits",
        ),
    ];

    for &(target_name, expected_err) in error_cases {
        let session = assert.session();
        let res = run(
            &context.session.get_target(
                Label::new(PackageRef::root().to_owned(), target_name.to_owned()).as_ref(),
                context.session.default_toolchain.as_ref(),
            ),
            move |t: &testutils::FakeTargetRef| {
                testutils::FakeEvalContext::rule_impl(session.clone(), t.clone())
            },
        );
        let err = res.expect_err(target_name);
        assert!(
            err.to_string().contains(expected_err),
            "Target '{target_name}' error '{err}' did not contain expected substring '{expected_err}'"
        );
    }
}
