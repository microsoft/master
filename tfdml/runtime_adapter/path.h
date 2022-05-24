/* Copyright 2015 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#pragma once

#include "absl/strings/string_view.h"

namespace tfdml {
std::string JoinPathImpl(std::initializer_list<absl::string_view> paths);

// Join multiple paths together.
// JoinPath unconditionally joins all paths together. For example:
//
//  Arguments                  | JoinPath
//  ---------------------------+---------------------
//  '/foo', 'bar'              | /foo/bar
//  '/foo/', 'bar'             | /foo/bar
//  '/foo', '/bar'             | /foo/bar
//  '/foo', '/bar', '/baz'     | /foo/bar/baz
//
// All paths will be treated as relative paths, regardless of whether or not
// they start with a leading '/'.  That is, all paths will be concatenated
// together, with the appropriate path separator inserted in between.
// Arguments must be convertible to absl::string_view.
//
// Usage:
// string path = file::JoinPath("/var/log", dirname, filename);
// string path = file::JoinPath(FLAGS_test_srcdir, filename);
template <typename... T>
inline std::string JoinPath(const T&... args) {
  return JoinPathImpl({args...});
}
} // namespace tfdml