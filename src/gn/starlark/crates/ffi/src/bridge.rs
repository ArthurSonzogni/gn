// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use starlark::values::ValueLike as _;
use types::EvaluatorContextExt as _;

/// The consolidated cxx FFI bridge defining all shared C++ classes, structs,
/// methods, and constructors utilized by the high-level Rust wrappers.
///
/// This file does several things:
/// * It generates types usable by rust.
/// * The `cxxbridge --header` command can be ran to re-generate the C++
///   headers.
///   * This allows for C++ code to #include rust types
/// * The `cxxbridge` command generates shims to allow us to use C++ types in
///   rust.
use crate::{session::Session, target::Target};

pub struct OwnedFrozenValue(pub starlark::values::OwnedFrozenValue);

impl OwnedFrozenValue {
    pub fn clone_cxx(&self) -> Box<Self> {
        Box::new(Self(self.0.clone()))
    }

    pub fn to_string_cxx(&self) -> String {
        self.0.value().to_string()
    }

    pub fn eq_cxx(&self, other: &Self) -> bool {
        self.0.value() == other.0.value()
    }

    pub fn invoke(
        &self,
        session: &'static Session,
        args: &cxx::CxxVector<Value>,
        kwargs: &Scope,
        mut out_val: std::pin::Pin<&mut Value>,
        scope: std::pin::Pin<&mut Scope>,
        origin: ParseNodePtr,
        err: std::pin::Pin<&mut Err>,
    ) {
        // Safety: `self` (and thus self.0.owner()) is guaranteed to outlive
        // the temp module and thus this cannot be GC'd during this call.
        let func_val = unsafe { self.0.unchecked_frozen_value() };
        // Safety: The Scope reference is valid and non-null for the duration of the
        // invocation.
        let scope_ptr = unsafe { std::ptr::NonNull::new_unchecked(scope.get_unchecked_mut()) };
        // Safety: The Err reference is valid and non-null for the duration of the
        // invocation.
        let mut err_ptr = unsafe { std::ptr::NonNull::new_unchecked(err.get_unchecked_mut()) };
        // Safety: The Scope pointer is valid and non-null.
        let settings = unsafe { scope_ptr.as_ref() }.settings();
        let eval_context = crate::eval_context::EvalContext::new_macro(session, scope_ptr, err_ptr);
        let res = (|| {
            let val = starlark::environment::Module::with_temp_heap(
                |module| -> starlark::Result<Self> {
                    let heap = module.heap();
                    let mut args: Vec<_> = args.iter().map(|arg| arg.to_rust(&heap)).collect();
                    let mut kwargs: Vec<_> = kwargs
                        .items()
                        .as_slice()
                        .iter()
                        .map(|kw| (kw.key, kw.value.to_rust(&heap)))
                        .collect();
                    if func_val
                        .downcast_ref::<rule::FrozenRule<crate::eval_context::EvalContext>>()
                        .is_some()
                    {
                        // Rules require the parameter name, but GN uses rule(name, **kwargs).
                        if let [name] = args.as_slice() {
                            kwargs.push(("name", *name));
                            args.clear();
                        } else {
                            return Err(crate::errors::Error::RuleRequiresTargetName.into());
                        }
                    }

                    let res = {
                        let mut eval = starlark::eval::Evaluator::new(&module);
                        eval.set_context(&eval_context);
                        eval.eval_function(func_val.to_value(), &args, &kwargs)
                    };

                    module.set_extra_value(res?);
                    let frozen_module = module.freeze().map_err(starlark::Error::new_other)?;
                    Ok(Self(frozen_module.owned_extra_value().unwrap()))
                },
            )?;

            out_val
                .as_mut()
                .assign(val.0.value(), Some(val.0.owner()), settings, origin)?;
            Ok(())
        })();
        // Safety: The Err pointer is valid, non-null, and pinned.
        let err_pin = unsafe { std::pin::Pin::new_unchecked(err_ptr.as_mut()) };
        err_pin.handle(res);
    }
}

#[cxx::bridge]
// Allow let_underscore_drop because the cxx::bridge generated code has non-binding
// lets on C++ types with destructors.
#[allow(let_underscore_drop)]
// CxxBridge requires a module, but we don't want one. So we make a private one
// and re-export all fields.
mod dummy {
    struct Any {
        _private: u8,
    }

    // A &[T] compatible with both opaque and non-opaque types.
    #[derive(Clone, Copy)]
    struct SliceAny {
        len: usize,
        ptr: *mut Any,
    }

    struct KeyValue<'a> {
        key: &'a str,
        value: &'a Value,
    }

    // cxxbridge marks any function that takes a raw pointer unsafe.
    // By providing a thin wrapper around the pointer, we can remove the unsafe.
    #[derive(Clone, Copy, Default)]
    struct ParseNodePtr {
        ptr: *const ParseNode,
    }

    #[derive(Clone, Copy)]
    enum ValueType {
        None = 0,
        Boolean = 1,
        Integer = 2,
        String = 3,
        List = 4,
        Scope = 5,
        StarlarkValue = 6,
    }
    unsafe extern "C++" {
        // include! simply tells cxxbridge to put the #include in the generated C++
        // source code. It does not do anything on the rust side.
        include!("gn/err.h");
        include!("gn/ffi/err.h");
        include!("gn/ffi/scope.h");
        include!("gn/ffi/source_file.h");
        include!("gn/ffi/target.h");
        include!("gn/ffi/test_with_scope.h");
        include!("gn/ffi/value.h");
        include!("gn/label.h");
        include!("gn/output_file.h");
        include!("gn/scope.h");
        include!("gn/settings.h");
        include!("gn/source_dir.h");
        include!("gn/source_file.h");
        include!("gn/target.h");
        include!("gn/test_with_scope.h");
        include!("gn/value.h");

        pub unsafe fn free_vector_buffer(ptr: *mut Any);

        type Err;
        fn has_error(self: &Err) -> bool;
        // Dead code for production, used in tests only
        #[allow(dead_code)]
        fn NewErr() -> UniquePtr<Err>;
        fn ErrToString(err: &Err) -> String;

        fn PopulateErrWithLocation(
            err: Pin<&mut Err>,
            message: &str,
            help: &str,
            file: &InputFile,
            start_line: i32,
            start_column: i32,
            end_line: i32,
            end_column: i32,
        );
        fn PopulateErrWithMessage(err: Pin<&mut Err>, message: &str, help: &str);
        fn AppendSubErr(
            err: Pin<&mut Err>,
            message: &str,
            file: &InputFile,
            start_line: i32,
            start_column: i32,
            end_line: i32,
            end_column: i32,
        );

        type InputFile;
        fn NewInputFile<'a, 'b>(name: &'a str, code: &'a str) -> &'b InputFile;

        type OutputFile;
        #[cxx_return_type = "std::string_view"]
        fn value(self: &OutputFile) -> &str;

        type SourceDir;
        #[cxx_return_type = "std::string_view"]
        fn SourceWithNoTrailingSlash(self: &SourceDir) -> &str;

        type Label;
        fn dir(self: &Label) -> &SourceDir;
        #[cxx_return_type = "const std::string&"]
        fn name(self: &Label) -> &str;

        type SourceFile;
        #[cxx_name = "IsHeaderType"]
        fn is_header(self: &SourceFile) -> bool;
        fn source_file_to_output_path<'a>(settings: &'a Settings, file: &'a SourceFile) -> &'a str;

        #[rust_name = "CxxTarget"]
        type Target;
        fn label(self: &CxxTarget) -> &Label;
        fn output_type_u8(target: &CxxTarget) -> u8;
        fn rust_target<'a>(self: &'a CxxTarget, session: &'a Session) -> &'static Target;
        fn set_rust_target(self: &CxxTarget, rust_target: &Target);
        #[rust_name = "settings_cxx"]
        fn settings(self: &CxxTarget) -> *const Settings;
        fn create_target(
            scope: Pin<&mut Scope>,
            name: &str,
            output_type: &str,
            err: Pin<&mut Err>,
        ) -> *mut CxxTarget;
        fn register_dependency(
            target: Pin<&mut CxxTarget>,
            package: &str,
            name: &str,
            toolchain_package: &str,
            toolchain_name: &str,
        );

        type Settings;
        fn toolchain_label(self: &Settings) -> &Label;
        fn is_default(self: &Settings) -> bool;

        type Scope;
        // Constructs a new child Scope, populates placeholder Values for the given
        // keys, and returns an owned slice of references to the placeholders.
        // For example, NewScope(&scope, ["foo", "bar"]) would return
        // [scope["foo"], scope["bar"]].
        // The caller is then responsible for filling in the values as needed.
        fn NewScope(
            parent_scope: Pin<&mut Scope>,
            keys: &[&str],
            out_scope: &mut UniquePtr<Scope>,
        ) -> SliceAny;
        fn NewStruct(
            settings: &Settings,
            keys: &[&str],
            out_scope: &mut UniquePtr<Scope>,
        ) -> SliceAny;
        // Returns an OwnedSlice<KeyValue> corresponding to references to each element.
        fn GetScopeItems(scope: &Scope) -> SliceAny;
        fn GetValue(scope: &Scope, ident: &str) -> *const Value;
        fn SetValue<'a>(
            scope: Pin<&'a mut Scope>,
            ident: &str,
            origin: ParseNodePtr,
        ) -> Pin<&'a mut Value>;
        #[rust_name = "settings_cxx"]
        fn settings(self: &Scope) -> *const Settings;
        #[cxx_name = "GetSourceDir"]
        fn package_cxx(self: &Scope) -> &SourceDir;

        type TestWithScope;
        fn NewTestWithScope() -> UniquePtr<TestWithScope>;
        #[rust_name = "scope_cxx"]
        fn scope(self: Pin<&mut TestWithScope>) -> *mut Scope;

        type Value;
        type ParseNode;
        // We allow dead code because this isn't used in production and we
        // can't tag things in the bridge with cfg(test).
        #[allow(dead_code)]
        fn NewValueForTesting() -> UniquePtr<Value>;
        fn ValueSize() -> usize;
        #[cxx_return_type = "Value::Type"]
        #[cxx_name = "type"]
        // We can't call this "type" in rust since it's a keyword.
        fn kind(self: &Value) -> ValueType;
        fn boolean_value(self: &Value) -> &bool;
        fn int_value(self: &Value) -> &i64;
        #[cxx_return_type = "const std::string&"]
        fn string_value(self: &Value) -> &str;
        #[cxx_name = "GetValueList"]
        fn list_value_cxx(val: &Value) -> SliceAny;
        fn scope_value(self: &Value) -> *const Scope;
        fn SetValueNone(val: Pin<&mut Value>, origin: ParseNodePtr);
        fn SetValueBool(val: Pin<&mut Value>, origin: ParseNodePtr, b: bool);
        fn SetValueInt(val: Pin<&mut Value>, origin: ParseNodePtr, i: i64);
        fn SetValueString(val: Pin<&mut Value>, origin: ParseNodePtr, s: &str);
        // Initialises self as a list of `size` elements and returns a pointer to the
        // start.
        fn SetValueList(val: Pin<&mut Value>, origin: ParseNodePtr, size: usize) -> *mut Any;
        fn SetValueScope(val: Pin<&mut Value>, origin: ParseNodePtr, scope: UniquePtr<Scope>);
        fn SetValueStarlark(
            val: Pin<&mut Value>,
            origin: ParseNodePtr,
            starlark_val: Box<OwnedFrozenValue>,
        );
        fn starlark_value(self: &Value) -> &OwnedFrozenValue;
    }

    extern "Rust" {
        #[cxx_name = "RustTarget"]
        type Target;

        type Session;

        #[Self = "Session"]
        #[cxx_name = "new_cxx"]
        fn new(source_root: &str, source_root_rel: &str) -> Box<Session>;

        #[Self = "Session"]
        fn new_for_testing() -> Box<Session>;

        fn register_cxx_target(
            self: &'static Session,
            target: &'static CxxTarget,
        ) -> &'static Target;

        fn load_values(
            self: &'static Session,
            label: &str,
            relative_to: &str,
            keys: &[&str],
            scope: Pin<&mut Scope>,
            settings: &Settings,
            origin: ParseNodePtr,
            err: Pin<&mut Err>,
        );

        type OwnedFrozenValue;
        #[rust_name = "clone_cxx"]
        fn clone(self: &OwnedFrozenValue) -> Box<OwnedFrozenValue>;
        #[rust_name = "to_string_cxx"]
        fn to_string(self: &OwnedFrozenValue) -> String;
        #[rust_name = "eq_cxx"]
        fn eq(self: &OwnedFrozenValue, other: &OwnedFrozenValue) -> bool;
        fn invoke(
            self: &OwnedFrozenValue,
            session: &'static Session,
            args: &CxxVector<Value>,
            kwargs: &Scope,
            out_val: Pin<&mut Value>,
            scope: Pin<&mut Scope>,
            origin: ParseNodePtr,
            err: Pin<&mut Err>,
        );
    }
}

pub use dummy::*;
