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

#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>

#include <string_view>
#include <unordered_set>

namespace schemaregistry::detail {

// build_file_descriptor_set walks from the file descriptor through all
// transitive imports, producing a FileDescriptorSet. The resulting files are
// ordered so that dependencies appear before the files that import them.
inline google::protobuf::FileDescriptorSet build_file_descriptor_set(
    const google::protobuf::FileDescriptor* file
) {
    google::protobuf::FileDescriptorSet fds;
    std::unordered_set<std::string_view> seen;

    struct collector {
        google::protobuf::FileDescriptorSet& fds;
        std::unordered_set<std::string_view>& seen;

        void collect(const google::protobuf::FileDescriptor* f) {
            if (f == nullptr) {
                return;
            }
            const std::string_view path = f->name();
            if (!seen.insert(path).second) {
                return;
            }

            // Collect dependencies first so the resulting FileDescriptorSet
            // is in a valid topological order.
            for (int i = 0; i < f->dependency_count(); ++i) {
                collect(f->dependency(i));
            }

            f->CopyTo(fds.add_file());
        }
    };

    collector{fds, seen}.collect(file);
    return fds;
}

} // namespace schemaregistry::detail

