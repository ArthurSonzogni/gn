// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use starlark::{
    environment::{FrozenModule, GlobalsBuilder, Module},
    eval::ParametersSpecParam::{Defaulted, Required},
    values::{FrozenHeapName, OwnedFrozen, Value},
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
pub struct BuiltinProviders {
    /// Default instance of `DefaultInfo`.
    /// Targets that do not return an explicit DefaultInfo provider will have
    /// target[DefaultInfo] return this.
    pub default_defaultinfo: OwnedFrozen<Value<'static>>,

    /// A frozen Starlark module containing the built-in provider definitions.
    /// This module is preloaded as `//builtins:providers.scl` and can be loaded
    /// in Starlark files.
    pub module: FrozenModule,
}

pub(crate) fn register_builtin_providers(builder: &mut GlobalsBuilder) -> BuiltinProviders {
    let empty_depset: OwnedFrozen<Value<'static>> =
        OwnedFrozen::build(FrozenHeapName::user("//builtins:empty_depset"), |heap| {
            heap.alloc(depset::Depset::default())
        });

    let (module, default_defaultinfo) = Module::with_temp_heap(|module: Module| {
        module.frozen_heap(|heap, edge| {
            let default_info = ProviderType::new_builtin(
                DEFAULT_INFO_ID,
                "DefaultInfo",
                &[
                    ("files", Defaulted(Some(empty_depset.clone()))),
                    ("executable", Defaulted(None)),
                ],
            );
            builder.set("DefaultInfo", default_info.clone());
            let default_info_value = heap.alloc_typed(default_info);

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

            let defaultinfo_instance = heap.alloc(ProviderInstance {
                provider_type: default_info_value,
                values: Box::new([
                    Some(empty_depset.as_ref().add_to_frozen_heap(heap)),
                    Some(Value::new_none()),
                ]),
            });
            module.set("default_defaultinfo", edge.rebrand(defaultinfo_instance));
        });

        let frozen = module
            .freeze_named(FrozenHeapName::user("//builtins:providers.scl"))
            .unwrap();
        let default_defaultinfo = frozen.get("default_defaultinfo").unwrap();
        (frozen, default_defaultinfo)
    });
    // DefaultInfo is allocated in the module's heap, so we ensure that the module's
    // heap will never disappear while the builder is active.
    builder.frozen_heap(|fh| fh.add_reference(module.frozen_heap()));

    BuiltinProviders {
        default_defaultinfo,
        module,
    }
}
