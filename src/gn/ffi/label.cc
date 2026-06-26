// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gn/label.h"
#include "cxx.h"

extern "C" {

rust::Str GetLabelDir(const Label& label) {
  return label.dir().value();
}

rust::Str GetLabelName(const Label& label) {
  return label.name();
}

}  // extern "C"
