// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_PUBLIC_CPP_BINDINGS_ENUM_TRAITS_H_
#define MOJO_PUBLIC_CPP_BINDINGS_ENUM_TRAITS_H_

namespace mojo {

// This must be specialized for any type |T| to be serialized/deserialized as a
// mojom enum |MojomType|. Each specialization needs to implement:
//
//   template <>
//   struct EnumTraits<MojomType, T> {
//     static MojomType ToMojom(T input);
//
//     // Returning false results in deserialization failure and causes the
//     // message pipe receiving it to be disconnected.
//     static bool FromMojom(MojomType input, T* output);
//   };
//
template <typename MojomType, typename T>
struct EnumTraits;

}  // namespace mojo

#endif  // MOJO_PUBLIC_CPP_BINDINGS_ENUM_TRAITS_H_