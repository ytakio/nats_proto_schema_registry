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
#include "schemaregistry/kv.hpp"
#include "schemaregistry/nats_kv.hpp"
#include "schemaregistry/types.hpp"

#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/message.h>

#include <glaze/glaze.hpp>

#include <memory>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>

namespace schemaregistry {

// NatsRegistry stores per-subject metadata and per-proto-file schema blobs in
// two NATS Key-Value buckets. It is header-only and does not throw exceptions;
// all errors are reported through std::error_code.
class NatsRegistry {
public:
  using FileDescriptorSet = google::protobuf::FileDescriptorSet;
  using Message = google::protobuf::Message;
  using Descriptor = google::protobuf::Descriptor;
  using FileDescriptor = google::protobuf::FileDescriptor;

  NatsRegistry(std::unique_ptr<IKeyValue> metadata_kv,
               std::unique_ptr<IKeyValue> schema_kv)
      : metadata_kv_(std::move(metadata_kv)), schema_kv_(std::move(schema_kv)) {
  }

  // Push inspects the protobuf message type of msg, builds a
  // FileDescriptorSet containing the message's file and its transitive
  // dependencies, and stores it in the registry under the given subject.
  [[nodiscard]] std::error_code push(std::stop_token stop_token,
                                     std::string_view subject,
                                     const Message &msg) {

    if (stop_token.stop_requested()) {
      return make_error_code(errc::cancelled);
    }
    if (subject.empty()) {
      return make_error_code(errc::invalid_argument);
    }

    const Descriptor *descriptor = msg.GetDescriptor();
    if (descriptor == nullptr) {
      return make_error_code(errc::invalid_argument);
    }

    const FileDescriptor *file = descriptor->file();
    if (file == nullptr) {
      return make_error_code(errc::internal);
    }

    FileDescriptorSet fds = build_file_descriptor_set_(file);

    std::string schema_bytes;
    if (!fds.SerializeToString(&schema_bytes)) {
      return make_error_code(errc::internal);
    }

    Metadata metadata;
    metadata.schema.path = file->name();
    metadata.schema.full_name = descriptor->full_name();

    std::string metadata_json;
    if (auto err = glz::write_json(metadata, metadata_json); err) {
      return make_error_code(errc::internal);
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
    return ec;
  }

  // Pull returns the FileDescriptorSet for the protobuf message registered
  // under the given NATS subject. The metadata is written to meta. It watches
  // the registry buckets and blocks until the subject is registered or
  // cancellation is requested. Returns nullptr and sets ec on failure.
  [[nodiscard]] std::unique_ptr<FileDescriptorSet>
  pull(std::stop_token stop_token, std::string_view subject, Metadata &meta,
       std::error_code &ec) {

    if (stop_token.stop_requested()) {
      ec = make_error_code(errc::cancelled);
      return nullptr;
    }
    if (subject.empty()) {
      ec = make_error_code(errc::invalid_argument);
      return nullptr;
    }

    auto meta_entry = watch_key(stop_token, metadata_kv_, subject, ec);
    if (ec) {
      return nullptr;
    }

    Metadata parsed_meta{};
    {
      const auto raw = meta_entry->value();
      const std::string json_str(reinterpret_cast<const char *>(raw.data()),
                                 raw.size());
      if (auto err = glz::read_json(parsed_meta, json_str); err) {
        ec = make_error_code(errc::internal);
        return nullptr;
      }
    }

    auto schema_entry =
        watch_key(stop_token, schema_kv_, parsed_meta.schema.path, ec);
    if (ec) {
      return nullptr;
    }

    auto fds = std::make_unique<FileDescriptorSet>();
    {
      const auto raw = schema_entry->value();
      const std::string fds_bytes(reinterpret_cast<const char *>(raw.data()),
                                  raw.size());
      if (!fds->ParseFromString(fds_bytes)) {
        ec = make_error_code(errc::internal);
        return nullptr;
      }
    }

    meta = std::move(parsed_meta);
    ec = {};
    return fds;
  }

private:
  // Watch a key in a KeyValue store until a real value is available.
  // Deleted or purged entries are ignored so that callers wait for a real
  // value. Returns an error if the watcher fails or cancellation is requested.
  [[nodiscard]] static std::unique_ptr<IKeyValueEntry>
  watch_key(std::stop_token stop_token, const std::unique_ptr<IKeyValue> &kv,
            std::string_view key, std::error_code &ec) {
    if (stop_token.stop_requested()) {
      ec = make_error_code(errc::cancelled);
      return nullptr;
    }

    if (!kv) {
      ec = make_error_code(errc::invalid_argument);
      return nullptr;
    }

    std::unique_ptr<IKeyWatcher> watcher = kv->watch(key, ec);
    if (ec) {
      return nullptr;
    }
    if (!watcher) {
      ec = make_error_code(errc::internal);
      return nullptr;
    }

    for (;;) {
      if (stop_token.stop_requested()) {
        ec = make_error_code(errc::cancelled);
        return nullptr;
      }

      auto entry = watcher->next(stop_token, ec);
      if (ec) {
        return nullptr;
      }
      if (!entry) {
        // Initial-state marker or timeout with no update; keep waiting.
        continue;
      }

      const auto op = entry->operation();
      if (op == IKeyValueEntry::Operation::del ||
          op == IKeyValueEntry::Operation::purge) {
        continue;
      }

      return entry;
    }
  }

  // Walks from the file descriptor through all transitive imports, producing
  // a FileDescriptorSet. The resulting files are ordered so that dependencies
  // appear before the files that import them.
  [[nodiscard]] static FileDescriptorSet
  build_file_descriptor_set_(const FileDescriptor *file) {
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

  std::unique_ptr<IKeyValue> metadata_kv_;
  std::unique_ptr<IKeyValue> schema_kv_;
};

// Create a Registry backed by NATS KV buckets. If the buckets do not exist they
// are created with default settings. The caller retains ownership of js and is
// responsible for closing the underlying NATS connection. On failure ec is set
// and nullptr is returned.
[[nodiscard]] std::unique_ptr<NatsRegistry>
create_registry(jsCtx *js, std::string_view metadata_bucket,
                std::string_view schema_bucket, std::error_code &ec) {
  if (js == nullptr) {
    ec = make_error_code(errc::invalid_argument);
    return nullptr;
  }
  if (metadata_bucket.empty() || schema_bucket.empty()) {
    ec = make_error_code(errc::invalid_argument);
    return nullptr;
  }

  auto get_or_create_kv =
      [&](std::string_view bucket) -> std::unique_ptr<IKeyValue> {
    const std::string bucket_str{bucket};
    kvStore *store = nullptr;
    natsStatus s = js_KeyValue(&store, js, bucket_str.c_str());
    if (s == NATS_NOT_FOUND) {
      kvConfig cfg{};
      kvConfig_Init(&cfg);
      cfg.Bucket = bucket_str.c_str();
      s = js_CreateKeyValue(&store, js, &cfg);
    }
    if (s == NATS_OK) {
      return std::make_unique<NatsKeyValue>(store);
    }
    ec = make_nats_error(s);
    return nullptr;
  };

  auto metadata_kv = get_or_create_kv(metadata_bucket);
  if (ec) {
    return nullptr;
  }

  auto schema_kv = get_or_create_kv(schema_bucket);
  if (ec) {
    return nullptr;
  }

  ec = {};
  return std::make_unique<NatsRegistry>(std::move(metadata_kv),
                                        std::move(schema_kv));
}

} // namespace schemaregistry
