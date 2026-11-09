// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use allocative::Allocative;
use args::ArgsList;
use depset::UnpackFileDepset;
use starlark::{
    collections::SmallMap,
    typing::Ty,
    values::{
        ProvidesStaticType, StarlarkValue, Trace, UnpackValue, Value, dict::DictRef, list::ListRef,
        structs::StructRef, type_repr::StarlarkTypeRepr,
    },
};
use starlark_derive::Freeze;
use types::File;

use crate::{ProviderId, ProviderInstance, errors::Error};

/// Helper to unpack a list of providers to useful metadata.
#[derive(Allocative, Debug, Default, Freeze, ProvidesStaticType, Trace)]
pub struct Providers<'v> {
    /// The output files produced by this target, parsed from the `DefaultInfo`
    /// provider.
    ///
    /// `DefaultInfo` represents the default outputs of a target rule (similar
    /// to Bazel's `DefaultInfo`). In GN, it contains the `files` depset
    /// which lists the direct and transitive outputs of the rule.
    pub outputs_phony: Option<File>,

    /// Additional input files declared by this target, parsed from the
    /// `GnInputsInfo` provider, mapped by path suffix pattern.
    ///
    /// When the target is a mixed C++/starlark target, this adds the specified
    /// inputs as implicit inputs to all ninja actions C++ generates for this
    /// target matching the path suffix pattern.
    ///
    /// Note that the leading '*' is stripped from the pattern before storage
    /// (e.g. "*.o" is stored as ".o").
    pub inputs_phony: Vec<(String, File)>,

    /// Command-line substitution variables propagated by this target, parsed
    /// from the `GnSubstitutionsInfo` provider.
    ///
    /// `GnSubstitutionsInfo` carries key-value substitutions (packaged as a
    /// struct) that are used by toolchains and command-line execution
    /// blocks to expand variables and flags dynamically.
    pub substitutions: Vec<(String, ArgsList<'v>)>,

    /// All provider instances mapped by their ProviderType's ProviderId.
    providers: SmallMap<ProviderId, Value<'v>>,
}

impl<'v> Providers<'v> {
    /// Looks up a provider instance by provider type value.
    pub fn get<'a>(
        &self,
        source: &impl StarlarkValue<'a>,
        key: Value<'_>,
    ) -> starlark::Result<Option<Value<'v>>> {
        let Some(key) = &key
            .downcast_ref::<crate::provider_type::ProviderType>()
            .map(|p| p.id)
        else {
            return starlark::values::ValueError::unsupported_with(source, "[]", key);
        };
        Ok(self.providers.get(key).copied())
    }

    /// Returns whether a provider instance exists for the given provider type value.
    pub fn contains<'a>(
        &self,
        source: &impl StarlarkValue<'a>,
        key: Value<'_>,
    ) -> starlark::Result<bool> {
        let Some(key) = &key
            .downcast_ref::<crate::provider_type::ProviderType>()
            .map(|p| p.id)
        else {
            return starlark::values::ValueError::unsupported_with(source, "in", key);
        };
        Ok(self.providers.contains_key(key))
    }
}

impl<'v> StarlarkTypeRepr for Providers<'v> {
    type Canonical = <&'v ListRef<'v> as StarlarkTypeRepr>::Canonical;

    fn starlark_type_repr() -> Ty {
        Ty::list(Ty::any())
    }
}

impl<'v> UnpackValue<'v> for Providers<'v> {
    type Error = starlark::Error;

    fn unpack_value_impl(value: Value<'v>) -> Result<Option<Self>, Self::Error> {
        let Some(list) = ListRef::from_value(value) else {
            return Ok(None);
        };
        let mut value_map = SmallMap::new();
        let mut outputs_phony = None;
        let mut inputs_phony = Vec::new();
        let mut substitutions = Vec::new();

        for item in list.iter() {
            let instance = <&ProviderInstance>::unpack_value_err(item)?;

            if value_map.insert(instance.provider_type.id, item).is_some() {
                return Err(Error::DuplicateProvider(instance.ty_name().to_owned()).into());
            }

            match instance.provider_type.id {
                crate::builtins::DEFAULT_INFO_ID => {
                    let files = instance.values[0].unwrap();
                    match UnpackFileDepset::unpack_value_err(files) {
                        Ok(f) => outputs_phony = f.0,
                        Err(_) => {
                            return Err(
                                Error::DefaultInfoFilesMustBeFileDepset(files.to_repr()).into()
                            );
                        },
                    }
                },
                crate::builtins::INPUTS_INFO_ID => {
                    let files = instance.values[0].unwrap();
                    let Some(dict) = DictRef::from_value(files) else {
                        return Err(Error::GnInputsInfoFilesMustBeDict(files.to_repr()).into());
                    };

                    for (k, v) in dict.iter() {
                        let Some(suffix) = k
                            .unpack_str()
                            .and_then(|path| path.strip_prefix('*'))
                            .filter(|suffix| !suffix.contains('*'))
                        else {
                            return Err(Error::InvalidGnInputsInfoKey(k.to_repr()).into());
                        };

                        match UnpackFileDepset::unpack_value_err(v) {
                            Ok(UnpackFileDepset(Some(f))) => {
                                inputs_phony.push((suffix.to_owned(), f));
                            },
                            Ok(UnpackFileDepset(None)) => {},
                            Err(_) => {
                                return Err(
                                    Error::GnInputsInfoValuesMustBeFileDepset(v.to_repr()).into()
                                );
                            },
                        }
                    }
                },
                crate::builtins::SUBSTITUTIONS_INFO_ID => {
                    // GnSubstitutionsInfo(substitutions = struct)
                    let substitutions_val = instance.values[0].unwrap();
                    let Some(substitutions_struct) = StructRef::from_value(substitutions_val)
                    else {
                        return Err(Error::GnSubstitutionsInfoSubstitutionsMustBeStruct(
                            substitutions_val.to_repr(),
                        )
                        .into());
                    };

                    for (k, v) in substitutions_struct.iter() {
                        substitutions
                            .push((k.as_str().to_owned(), <ArgsList>::unpack_value_err(v)?));
                    }
                    // Sort for deterministic ordering in the ninja file.
                    substitutions.sort_by(|(a, _), (b, _)| a.cmp(b));
                },
                _ => {},
            }
        }

        Ok(Some(Providers {
            outputs_phony,
            inputs_phony,
            substitutions,
            providers: value_map,
        }))
    }
}

#[cfg(test)]
mod tests {
    use starlark::values::UnpackValue as _;
    use types::File;

    use crate::Providers;

    fn new_assert() -> testutils::Assert {
        let mut a = testutils::Assert::default();

        let module = crate::builtins::builtin_providers_module();
        a.modify_globals(move |builder| {
            depset::depset_globals!(builder, testutils::eval_context::FakeEvalContext);
            crate::globals::register_providers(builder);
            // Note: In production code, the module would be preloaded instead
            // of loading into globals.
            builder.frozen_heap(|fh| fh.add_reference(module.frozen_heap()));
            builder.set("GnInputsInfo", module.get("GnInputsInfo").unwrap());
            builder.set(
                "GnSubstitutionsInfo",
                module.get("GnSubstitutionsInfo").unwrap(),
            );
        });
        a
    }

    #[test]
    fn test_providers_unpacking() {
        let mut a = new_assert();

        let val = a.pass("[]");
        val.by_ref(|v| {
            let providers = Providers::unpack_value_err(*v).unwrap();
            assert_eq!(providers.outputs_phony, None);
            assert_eq!(providers.inputs_phony, Vec::<(String, File)>::new());
            assert!(providers.providers.is_empty());
            assert!(providers.substitutions.is_empty());
        });

        let custom_info_ty = a.pass("CustomInfo = provider(fields = ['foo']); CustomInfo");
        a.modify_globals(move |builder| {
            builder.set("CustomInfo", custom_info_ty.clone());
        });

        let val = a.pass(
            r#"[
    DefaultInfo(files = depset([make_file("a")])),
    GnInputsInfo(files = {
        "*.o": depset([make_file("b")]),
        "*.empty": depset(),
    }),
    GnSubstitutionsInfo(substitutions = struct(key = ["val"])),
    CustomInfo(foo = 1),
]"#,
        );
        val.by_ref(|v| {
            let providers = Providers::unpack_value_err(*v).unwrap();

            assert_eq!(providers.outputs_phony, Some(File::intern("a")));
            assert_eq!(
                providers.inputs_phony,
                vec![(".o".to_owned(), File::intern("b"))]
            );

            assert_eq!(providers.providers.len(), 4);
            assert!(
                providers
                    .providers
                    .contains_key(&crate::builtins::DEFAULT_INFO_ID)
            );
            assert!(
                providers
                    .providers
                    .contains_key(&crate::builtins::INPUTS_INFO_ID)
            );
            assert!(
                providers
                    .providers
                    .contains_key(&crate::builtins::SUBSTITUTIONS_INFO_ID)
            );

            let keys: Vec<&str> = providers
                .substitutions
                .iter()
                .map(|(k, _)| k.as_str())
                .collect();
            assert_eq!(keys, vec!["key"]);
        });
    }

    #[track_caller]
    fn assert_unpack_fails(a: &mut testutils::Assert, expr: &str, expected_err: &str) {
        let val = a.pass(expr);
        let err = val.by_ref(|v| Providers::unpack_value_err(*v).unwrap_err().to_string());
        assert_eq!(err, expected_err);
    }

    #[test]
    fn test_providers_unpack_fails() {
        let mut a = new_assert();

        assert_unpack_fails(
            &mut a,
            "[DefaultInfo(files = depset()), DefaultInfo(files = depset())]",
            "Duplicate provider: DefaultInfo",
        );

        assert_unpack_fails(
            &mut a,
            r#"[DefaultInfo(files = "not-a-depset")]"#,
            r#"DefaultInfo.files must be a depset of files, got "not-a-depset""#,
        );

        assert_unpack_fails(
            &mut a,
            r#"[DefaultInfo(files = depset(["not-a-file"]))]"#,
            r#"DefaultInfo.files must be a depset of files, got depset(["not-a-file"])"#,
        );

        assert_unpack_fails(
            &mut a,
            r#"[GnInputsInfo(files = "not-a-dict")]"#,
            r#"GnInputsInfo.files must be a dict, got "not-a-dict""#,
        );

        assert_unpack_fails(
            &mut a,
            r#"[GnInputsInfo(files = {True: depset()})]"#,
            "GnInputsInfo.files keys must be a pattern starting '*' and containing no other '*' (e.g. '*.o'), got True",
        );

        assert_unpack_fails(
            &mut a,
            r#"[GnInputsInfo(files = {"foo": depset()})]"#,
            r#"GnInputsInfo.files keys must be a pattern starting '*' and containing no other '*' (e.g. '*.o'), got "foo""#,
        );

        assert_unpack_fails(
            &mut a,
            r#"[GnInputsInfo(files = {"*/*.o": depset()})]"#,
            r#"GnInputsInfo.files keys must be a pattern starting '*' and containing no other '*' (e.g. '*.o'), got "*/*.o""#,
        );

        assert_unpack_fails(
            &mut a,
            r#"[GnInputsInfo(files = {"*": "not-a-depset"})]"#,
            r#"GnInputsInfo.files values must be a depset of files, got "not-a-depset""#,
        );

        assert_unpack_fails(
            &mut a,
            r#"[GnSubstitutionsInfo(substitutions = {"key": ["val"]})]"#,
            r#"GnSubstitutionsInfo.substitutions must be a struct, got {"key": ["val"]}"#,
        );

        assert_unpack_fails(
            &mut a,
            r#"[GnSubstitutionsInfo(substitutions = struct(key = "not-a-list"))]"#,
            r#"Expected `list[Args | str]`, but got `string (repr: "not-a-list")`"#,
        );

        assert_unpack_fails(
            &mut a,
            r#"[GnSubstitutionsInfo(substitutions = struct(key = [123]))]"#,
            r#"Expected `list[Args | str]`, but got `list (repr: [123])`"#,
        );
    }
}
