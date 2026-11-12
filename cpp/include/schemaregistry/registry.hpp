// Copyright (c) 2026 nats-proto-schema-registry contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include "schemaregistry/error.hpp"
#include "schemaregistry/types.hpp"

#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/message.h>

#include <glaze/glaze.hpp>

#include <memory>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>

namespace schemaregistry {

// Registry stores per-subject metadata and per-proto-file schema blobs in two
// KeyValue backends. It is header-only and does not throw exceptions; all
// errors are reported through std::error_code.
//
// MetadataKV and SchemaKV must provide:
//   - std::error_code put(std::string_view key,
//                         std::string_view value,
//                         std::uint64_t &rev)
//   - std::vector<std::byte> get(std::string_view key,
//                                std::stop_token stop_token,
//                                std::error_code &ec)
//
// Templating allows callers to instantiate the registry with concrete adapter
// types (e.g. the NATS adapter in production or MockKeyValue in tests) without
// virtual dispatch overhead.
template <typename MetadataKV, typename SchemaKV> class Registry {
public:
  using FileDescriptorSet = google::protobuf::FileDescriptorSet;
  using Message = google::protobuf::Message;
  using Descriptor = google::protobuf::Descriptor;
  using FileDescriptor = google::protobuf::FileDescriptor;

  Registry(std::unique_ptr<MetadataKV> metadata_kv,
           std::unique_ptr<SchemaKV> schema_kv)
      : metadata_kv_(std::move(metadata_kv)), schema_kv_(std::move(schema_kv)) {
  }

  // Push inspects the protobuf message type of msg, builds a
  // FileDescriptorSet containing the message's file and its transitive
  // dependencies, and stores it in the registry under the given subject.
  [[nodiscard]] std::error_code push(std::stop_token stop_token,
                                     std::string_view subject,
                                     const Message &msg) {

    if (stop_token.stop_requested()) {
      return make_error_code(registry_errc::cancelled);
    }
    if (subject.empty()) {
      return make_error_code(registry_errc::invalid_argument);
    }

    const Descriptor *descriptor = msg.GetDescriptor();
    if (descriptor == nullptr) {
      return make_error_code(registry_errc::invalid_argument);
    }

    const FileDescriptor *file = descriptor->file();
    if (file == nullptr) {
      return make_error_code(registry_errc::internal_error);
    }

    FileDescriptorSet fds = build_file_descriptor_set(file);

    std::string schema_bytes;
    if (!fds.SerializeToString(&schema_bytes)) {
      return make_error_code(registry_errc::serialization_failed);
    }

    Metadata metadata;
    metadata.schema.path = file->name();
    metadata.schema.full_name = descriptor->full_name();

    std::string metadata_json;
    if (auto err = glz::write_json(metadata, metadata_json); err) {
      return make_error_code(registry_errc::serialization_failed);
    }

    const std::string_view path = metadata.schema.path;

    // Store the schema first so that a concurrent pull never sees metadata
    // referencing a missing schema file.
    std::uint64_t rev = 0;
    auto ec = schema_kv_->put(path, schema_bytes, rev);
    if (ec) {
      return ec;
    }

    ec = metadata_kv_->put(subject, metadata_json, rev);
    if (ec) {
      return ec;
    }
    return {};
  }

  // Pull returns the FileDescriptorSet for the protobuf message registered
  // under the given NATS subject. The metadata is written to meta. It blocks
  // until the subject is registered or cancellation is requested. Returns
  // nullptr and sets ec on failure.
  [[nodiscard]] std::unique_ptr<FileDescriptorSet>
  pull(std::stop_token stop_token, std::string_view subject, Metadata &meta,
       std::error_code &ec) {

    if (stop_token.stop_requested()) {
      ec = make_error_code(registry_errc::cancelled);
      return nullptr;
    }
    if (subject.empty()) {
      ec = make_error_code(registry_errc::invalid_argument);
      return nullptr;
    }

    const auto meta_bytes = metadata_kv_->get(subject, stop_token, ec);
    if (ec) {
      return nullptr;
    }

    Metadata parsed_meta{};
    {
      const auto raw = reinterpret_cast<const char *>(meta_bytes.data());
      const std::string json_str(raw, meta_bytes.size());
      if (auto err = glz::read_json(parsed_meta, json_str); err) {
        ec = make_error_code(registry_errc::parse_failed);
        return nullptr;
      }
    }

    const auto schema_bytes =
        schema_kv_->get(parsed_meta.schema.path, stop_token, ec);
    if (ec) {
      return nullptr;
    }

    auto fds = std::make_unique<FileDescriptorSet>();
    {
      const auto raw = reinterpret_cast<const char *>(schema_bytes.data());
      const std::string fds_bytes(raw, schema_bytes.size());
      if (!fds->ParseFromString(fds_bytes)) {
        ec = make_error_code(registry_errc::parse_failed);
        return nullptr;
      }
    }

    meta = std::move(parsed_meta);
    ec = {};
    return fds;
  }

private:
  // Walks from the file descriptor through all transitive imports, producing
  // a FileDescriptorSet. The resulting files are ordered so that dependencies
  // appear before the files that import them.
  [[nodiscard]]
  static FileDescriptorSet
  build_file_descriptor_set(const FileDescriptor *file) {
    FileDescriptorSet fds;
    std::unordered_set<std::string_view> seen;

    struct collector {
      FileDescriptorSet &fds;
      std::unordered_set<std::string_view> &seen;

      void collect(const FileDescriptor *f) {
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

  std::unique_ptr<MetadataKV> metadata_kv_;
  std::unique_ptr<SchemaKV> schema_kv_;
};

} // namespace schemaregistry
