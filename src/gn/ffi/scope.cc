// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ranges>
#include <vector>

#include "gn/ffi/bridge.h"
#include "gn/ffi/scope.h"
#include "gn/ffi/slice.h"
#include "gn/scope.h"
#include "gn/value.h"

SliceAny NewScope(const Scope& parent_scope,
                  rust::Slice<const rust::Str> keys,
                  std::unique_ptr<Scope>& out_scope) {
  auto new_scope = std::make_unique<Scope>(&parent_scope);
  new_scope->set_source_dir(parent_scope.GetSourceDir());
  // We detach because GN always detaches when result_mode of the parse tree is
  // RETURNS_SCOPE (which is what starlark creates new scopes for).
  new_scope->DetachFromContaining();

  std::vector<Value*> placeholders;
  placeholders.reserve(keys.size());
  for (const auto& key : keys) {
    std::string_view key_sv(key.data(), key.size());
    Value* val = new_scope->SetValue(key_sv, Value(), nullptr);
    placeholders.push_back(val);
  }

  out_scope = std::move(new_scope);
  return IntoSlice(std::move(placeholders));
}

// Unlike regular GN scoping rules, this does not extract from variables defined
// in outer scopes. This is because starlark treats scopes as equivalent to
// "struct" objects, and as the **kwargs to pass to functions. Thus, accessing
// values from outer scopes would be very wierd.
// Consider the following example:
//
// # //:example.scl
// def my_macro(srcs, my_struct):
//    my_struct.bar
//
// # BUILD.gn
// load("//:example.scl", "my_macro")
//
// foo = 1
// my_macro() {
//   srcs = ...
//   my_struct = {
//     bar = 2
//   }
// }
//
// In this example, if we included parent scopes as well:
// * my_macro would complain that it got an unexpected parameter "foo"
// * my_struct.srcs would also be accessible.
SliceAny GetScopeItems(const Scope& scope) {
  auto range =
      scope.GetCurrentScopeValues() | std::views::transform([](auto pair) {
        return KeyValue{rust::Str(pair.first.data(), pair.first.size()),
                        *pair.second};
      });
  return IntoSlice(std::vector<KeyValue>(range.begin(), range.end()));
}

const Value* GetValue(const Scope& scope, rust::Str ident) {
  std::string_view ident_sv(ident.data(), ident.size());
  return scope.GetValue(ident_sv);
}
