// Copyright (c) 2026 nats-proto-schema-registry contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include <glaze/glaze.hpp>

#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>

#include <string>

namespace schemaregistry {

// SchemaRef identifies a protobuf message type and the .proto file that
// defines it. It is stored as JSON in the metadata bucket keyed by the NATS
// subject.
struct SchemaRef {
    // Path is the .proto file path, for example "google/protobuf/any.proto".
    std::string path;

    // FullName is the fully qualified protobuf message name, for example
    // "google.protobuf.Any".
    std::string full_name;
};

// Metadata is the JSON document stored per subject in the metadata bucket.
struct Metadata {
    SchemaRef schema;
};

// Result returned by Registry::pull.
struct pull_result {
    google::protobuf::FileDescriptorSet fds;
    Metadata meta;
};

} // namespace schemaregistry

namespace glz {

template <>
struct meta<schemaregistry::SchemaRef> {
    using T = schemaregistry::SchemaRef;
    static constexpr auto value = object(
        "path", &T::path,
        "full_name", &T::full_name
    );
};

template <>
struct meta<schemaregistry::Metadata> {
    using T = schemaregistry::Metadata;
    static constexpr auto value = object(
        "schema", &T::schema
    );
};

} // namespace glz

