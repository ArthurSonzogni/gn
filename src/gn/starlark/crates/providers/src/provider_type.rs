// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{
    fmt,
    fmt::{Debug, Display},
    sync::{
        atomic::{AtomicU64, Ordering},
        OnceLock,
    },
};

use allocative::Allocative;
use starlark::{
    any::ProvidesStaticType,
    collections::SmallMap,
    eval::{Arguments, Evaluator, ParametersSpec, ParametersSpecParam},
    starlark_simple_value,
    values::{
        Freeze, FreezeResult, Freezer, FrozenHeapName, HeapEdge, OwnedFrozen, StarlarkValue,
        StringValue, Value, ValueTyped,
    },
};
use starlark_derive::{starlark_value, NoSerialize, Trace};

use crate::{Error, ProviderInstance};

/// A unique identifier for a provider type within a GN build session.
#[derive(Copy, Clone, Debug, Hash, PartialEq, Eq, PartialOrd, Ord, Allocative)]
pub struct ProviderId(pub(crate) u64);

impl ProviderId {
    /// Generates a new unique `ProviderId`.
    ///
    /// A process-wide counter is fine here because all we care about is that these type IDs are
    /// distinct Provider IDs are also not persisted anywhere so we don't have nondeterminism
    /// concerns.
    pub fn next() -> Self {
        // Built-in provider IDs are u8s, so starting at 256 is safe
        static NEXT_ID: AtomicU64 = AtomicU64::new(256);
        Self(NEXT_ID.fetch_add(1, Ordering::Relaxed))
    }

    /// Constant constructor for built-in providers.
    pub const fn builtin(id: u8) -> Self {
        Self(id as u64)
    }
}

#[derive(Allocative, Clone, Debug, Trace)]
// Contains all the information we cannot know about a provider type until we
// actually know the name of it.
pub(crate) struct ProviderTypeData {
    pub(crate) name: OwnedFrozen<StringValue<'static>>,
    pub(crate) parameter_spec: ParametersSpec<Value<'static>>,
    pub(crate) custom_defaults: SmallMap<usize, OwnedFrozen<Value<'static>>>,
}

/// Represents the provider type constructor.
#[derive(Allocative, Clone, Debug, NoSerialize, ProvidesStaticType, Trace)]
pub struct ProviderType {
    /// The unique type identifier.
    pub(crate) id: ProviderId,
    /// The configured provider fields. This is set when starlark calls
    /// `export_as` when you assign the provider to a variable.
    /// If this is not set, you cannot construct the provider.
    pub(crate) data: OnceLock<ProviderTypeData>,
    /// A mapping from field name to index.
    /// This is akin to python's `__slots__`.
    pub(crate) fields: SmallMap<String, usize>,
}

starlark_simple_value!(ProviderType);

impl Display for ProviderType {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "<provider>")
    }
}

#[starlark_value(type = "provider")]
impl<'v> StarlarkValue<'v> for ProviderType {
    type Canonical = Self;

    fn invoke(
        &self,
        me: Value<'v>,
        args: &Arguments<'v, '_>,
        eval: &mut Evaluator<'v, '_, '_>,
    ) -> starlark::Result<Value<'v>> {
        if !me.is_frozen() {
            return Err(Error::ProviderNotFrozen.into());
        }

        let provider_type = ValueTyped::<Self>::new_err(me).unwrap();
        let data = self.data.get().expect("Frozen providers must be exported");
        let parameter_spec = HeapEdge::immortal().rebrand_ref(&data.parameter_spec);
        parameter_spec.parser(args, eval, |param_parser, eval| {
            let values: Box<[Option<Value<'v>>]> = (0..self.fields.len())
                .map(|idx| {
                    let val: Option<Value<'v>> = param_parser.next_opt()?;
                    Ok(match val {
                        Some(v) => Some(v),
                        None => data
                            .custom_defaults
                            .get(&idx)
                            .map(|default_val| default_val.as_ref().add_to_heap(eval.heap())),
                    })
                })
                .collect::<starlark::Result<_>>()?;
            Ok(eval.heap().alloc_complex(ProviderInstance {
                provider_type,
                values,
            }))
        })
    }

    fn export_as(&self, name: &str, _eval: &mut Evaluator<'v, '_, '_>) -> starlark::Result<()> {
        if self.data.get().is_some() {
            return Ok(());
        }

        if !name.ends_with("Info") {
            return Err(Error::InvalidProviderName(name.to_owned()).into());
        }

        self.data.get_or_init(|| ProviderTypeData {
            // Standard practice is to just `eval.heap().alloc_str(name). However, that produces a
            // Value tied to the brand of the evaluator's heap. This is incompatible
            // with `get_type_value_dyn`, as it cannot call rebrand because it takes no heap.
            name: OwnedFrozen::build(FrozenHeapName::user("//providers:name"), |heap| {
                heap.alloc_str(name)
            }),
            parameter_spec: ParametersSpec::new_named_only(
                name,
                self.fields
                    .keys()
                    .map(|f| (f.as_str(), ParametersSpecParam::Optional)),
            ),
            custom_defaults: SmallMap::new(),
        });
        Ok(())
    }
}

impl ProviderType {
    /// Creates a new provider type with the provided fields.
    /// This provider is not yet configured, and is unusable until `export_as`
    /// is called.
    pub fn new(fields: Vec<String>) -> starlark::Result<Self> {
        let mut field_map = SmallMap::with_capacity(fields.len());
        for (idx, field) in fields.into_iter().enumerate() {
            if field_map.insert(field.clone(), idx).is_some() {
                return Err(Error::DuplicateFieldName(field).into());
            }
        }
        Ok(Self {
            id: ProviderId::next(),
            data: OnceLock::new(),
            fields: field_map,
        })
    }

    /// Creates a new builtin provider type with a custom stable ProviderId.
    /// Unlike regular providers, these providers may have either Defaulted or
    /// Required parameters.
    pub fn new_builtin(
        id: ProviderId,
        name: &'static str,
        fields: &[(
            &'static str,
            ParametersSpecParam<Option<OwnedFrozen<Value<'static>>>>,
        )],
    ) -> Self {
        let field_map = fields
            .iter()
            .enumerate()
            .map(|(idx, (field_name, _))| (field_name.to_string(), idx))
            .collect();
        let mut custom_defaults = SmallMap::new();
        let param_specs: Vec<(&'static str, ParametersSpecParam<Value<'static>>)> = fields
            .iter()
            .enumerate()
            .map(|(idx, &(field_name, ref param))| {
                let spec = match param {
                    ParametersSpecParam::Required => ParametersSpecParam::Required,
                    ParametersSpecParam::Optional => ParametersSpecParam::Optional,
                    ParametersSpecParam::Defaulted(default_val) => {
                        if let Some(owned) = default_val {
                            custom_defaults.insert(idx, owned.clone());
                        }
                        ParametersSpecParam::Optional
                    },
                };
                (field_name, spec)
            })
            .collect();
        let data = OnceLock::new();
        // The provider name is allocated in a dedicated frozen heap once during builtin
        // registration, keeping it permanently frozen and thread-safe.
        data.set(ProviderTypeData {
            name: OwnedFrozen::build(FrozenHeapName::user("//providers:name"), |heap| {
                heap.alloc_str(name)
            }),
            parameter_spec: ParametersSpec::new_named_only(name, param_specs),
            custom_defaults,
        })
        .expect("newly created OnceLock is empty");
        Self {
            id,
            data,
            fields: field_map,
        }
    }
}

impl<'v> Freeze<'v> for ProviderType {
    type Frozen<'fv> = Self;

    fn freeze<'fv>(self, _freezer: &Freezer<'v, 'fv>) -> FreezeResult<Self::Frozen<'fv>> {
        if self.data.get().is_none() {
            return Err(Error::ProviderNotExported.into());
        }
        Ok(self)
    }
}

#[cfg(test)]
mod tests {
    use crate::globals::register_providers;

    fn new_assert() -> testutils::Assert {
        let mut a = testutils::Assert::default();
        a.modify_globals(|builder| {
            register_providers(builder);
        });
        a
    }

    #[test]
    fn test_provider_type() {
        let mut a = new_assert();
        a.fail(
            "provider()",
            "Missing named-only parameter `fields` for call to `provider`",
        );

        a.fail(
            "provider(fields=['a'])(a=1)",
            "Cannot construct values of non-frozen provider type",
        );

        a.fail(
            r#"
MyInfo = provider(fields=['a'])
MyInfo(a=1, b=2)
"#,
            "Cannot construct values of non-frozen provider type",
        );
        a.fail(
            "provider(fields = 1)",
            "Provider fields must be an iterable",
        );
        a.fail("provider(fields = ['a', 'a'])", "Duplicate field name: a");
        a.fail(
            r#"
p = provider(fields=['a'])
"#,
            "Provider name must end with 'Info' (got 'p')",
        );
    }

    #[test]
    fn test_provider_aliasing() {
        let mut a = new_assert();

        let p_info = a.pass("MyInfo = provider(fields = ['a']); foo = MyInfo; foo");
        a.modify_globals(move |builder| {
            builder.set("foo", p_info.clone());
        });

        let alias = a.pass("alias = foo; alias");
        a.modify_globals(move |builder| {
            builder.set("alias", alias.clone());
        });

        // The constructor should retain its canonical name "MyInfo"
        a.eq("str(alias(a = 1))", "MyInfo(a = 1)".to_string());
    }

    #[test]
    fn test_unexported_provider_fails_to_call() {
        let mut a = new_assert();
        a.fail(
            "x = [provider(fields=['a'])]; x",
            "The result of provider() must be assigned to a variable",
        );
    }
}
