// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use starlark::{
    environment::{FrozenModule, Globals, GlobalsBuilder, Module},
    eval::ParametersSpecParam::{Defaulted, Required},
    values::{FrozenHeapName, Heap, OwnedFrozen, ProvidesStaticType, Value, ValueTyped},
};

use crate::{ProviderId, ProviderInstance, ProviderType};

// Stable IDs for built-in providers.
pub(crate) const DEFAULT_INFO_ID: ProviderId = ProviderId::builtin(1);
pub(crate) const INPUTS_INFO_ID: ProviderId = ProviderId::builtin(2);
pub(crate) const SUBSTITUTIONS_INFO_ID: ProviderId = ProviderId::builtin(3);

/// Holds the global built-in provider types and default instances created at
/// initialization time.
///
/// All builtin providers except DefaultInfo (which is available globally) are
/// available through load("//builtins:providers.scl", "$NAME")
///
/// Builtin providers are no different to any other providers, except that
/// rather than just being metadata passed between targets, GN does something
/// special with them.
/// * DefaultInfo(files=depset(...)) (https://bazel.build/rules/lib/providers/DefaultInfo)
///   * The "outputs" of a rule. Building the alias for the target builds all files in DefaultInfo.
///   * Unlike all other providers, available globally without calling
/// * GnInputsInfo(files=depset(...))
///   * When the target is a mixed C++/starlark target, this adds the specified inputs as implicit
///     inputs to all ninja actions C++ generates for this target.
/// * GnSubstitutionsInfo:
///
///   ```rust
///   GnSubstitutionsInfo(substitutions=struct(
///      foo = [ctx.actions.args().add("--foo", ctx.file.foo)]
///   ))
///   ```
///   * Adds "foo = --foo path/to/foo" to the ninja file
///   * Adding command = "... {{foo}}" to your GN tool will allow you to use this in GN.
pub struct BuiltinProviders(OwnedFrozen<BuiltinProvidersInternal<'static>>);

#[derive(ProvidesStaticType)]
struct BuiltinProvidersInternal<'v> {
    /// Default instance of `DefaultInfo`.
    default_defaultinfo: Value<'v>,
    default_info: ValueTyped<'v, ProviderType>,
}

impl BuiltinProviders {
    /// Creates a new `BuiltinProviders` from the session globals.
    pub fn new(globals: &Globals) -> Self {
        Self(OwnedFrozen::build(
            FrozenHeapName::user("//builtins:default_defaultinfo"),
            |heap| {
                let default_info_val = globals
                    .get_ref("DefaultInfo")
                    .expect("DefaultInfo provider not registered in globals")
                    .add_to_frozen_heap(heap);
                let default_info = ValueTyped::<ProviderType>::new(default_info_val).unwrap();

                let default_defaultinfo = heap.alloc(ProviderInstance {
                    provider_type: default_info,
                    values: Box::new([
                        Some(heap.alloc(depset::Depset::default())),
                        Some(Value::new_none()),
                    ]),
                });

                BuiltinProvidersInternal {
                    default_defaultinfo,
                    default_info,
                }
            },
        ))
    }

    /// Returns the default `DefaultInfo` instance.
    pub fn default_defaultinfo<'v>(&self, heap: &Heap<'v>) -> Value<'v> {
        self.0.by_ref_with_reconstructor(|inner, reconstructor| {
            reconstructor.edge(*heap).rebrand(inner.default_defaultinfo)
        })
    }

    /// Creates a new `DefaultInfo` instance with the given files and optional
    /// executable.
    pub fn new_defaultinfo<'v>(
        &self,
        files: Value<'v>,
        executable: Option<Value<'v>>,
        heap: &Heap<'v>,
    ) -> Value<'v> {
        let provider_type = self.0.by_ref_with_reconstructor(|inner, reconstructor| {
            reconstructor.edge(*heap).rebrand(inner.default_info)
        });
        heap.alloc_complex(ProviderInstance {
            provider_type,
            values: Box::new([
                Some(files),
                Some(executable.unwrap_or_else(Value::new_none)),
            ]),
        })
    }
}

pub(crate) fn register_builtin_providers_globals(builder: &mut GlobalsBuilder) {
    builder.set(
        "DefaultInfo",
        ProviderType::new_builtin(
            DEFAULT_INFO_ID,
            "DefaultInfo",
            &[
                (
                    "files",
                    Defaulted(Some(OwnedFrozen::build(
                        FrozenHeapName::user("//builtins:empty_depset"),
                        |heap| heap.alloc(depset::Depset::default()),
                    ))),
                ),
                ("executable", Defaulted(None)),
            ],
        ),
    );
}

/// Registers and returns the built-in provider types as a frozen module.
/// Preloaded as `//builtins:providers.scl`.
pub fn builtin_providers_module() -> FrozenModule {
    Module::with_temp_heap(|module: Module| {
        module.frozen_heap(|heap, edge| {
            let inputs =
                ProviderType::new_builtin(INPUTS_INFO_ID, "GnInputsInfo", &[("files", Required)]);

            let substitutions = ProviderType::new_builtin(
                SUBSTITUTIONS_INFO_ID,
                "GnSubstitutionsInfo",
                &[("substitutions", Required)],
            );

            let inputs_value = heap.alloc(inputs);
            let substitutions_value = heap.alloc(substitutions);

            module.set("GnInputsInfo", edge.rebrand(inputs_value.to_value()));
            module.set(
                "GnSubstitutionsInfo",
                edge.rebrand(substitutions_value.to_value()),
            );
        });

        module
            .freeze_named(FrozenHeapName::user("//builtins:providers.scl"))
            .unwrap()
    })
}
