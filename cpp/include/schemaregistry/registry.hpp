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

#include "schemaregistry/descriptor.hpp"
#include "schemaregistry/error.hpp"
#include "schemaregistry/kv.hpp"
#include "schemaregistry/nats_kv.hpp"
#include "schemaregistry/types.hpp"

#include <google/protobuf/message.h>

#include <glaze/glaze.hpp>

#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>

namespace schemaregistry {

namespace detail {

// Watch a key in a KeyValue store until a real value is available.
// Deleted or purged entries are ignored so that callers wait for a real value.
// Returns an error if the watcher fails or cancellation is requested.
inline std::pair<std::shared_ptr<IKeyValueEntry>, std::error_code>
watch_key(std::stop_token stop_token, IKeyValue &kv, std::string_view key) {
  if (stop_token.stop_requested()) {
    return {nullptr, make_error_code(errc::cancelled)};
  }

  auto [watcher, watch_err] = kv.watch(key, stop_token);
  if (watch_err) {
    return {nullptr, watch_err};
  }
  if (!watcher) {
    return {nullptr, make_error_code(errc::internal)};
  }

  for (;;) {
    if (stop_token.stop_requested()) {
      return {nullptr, make_error_code(errc::cancelled)};
    }

    auto [entry, next_err] = watcher->next(stop_token);
    if (next_err) {
      return {nullptr, next_err};
    }
    if (!entry) {
      // Initial-state marker or timeout with no update; keep waiting.
      continue;
    }

    const auto op = entry->operation();
    if (op == KeyValueOperation::del || op == KeyValueOperation::purge) {
      continue;
    }

    return {entry, {}};
  }
}

} // namespace detail

// NatsRegistry stores per-subject metadata and per-proto-file schema blobs in
// two NATS Key-Value buckets. It is header-only and does not throw exceptions;
// all errors are reported through std::error_code.
class NatsRegistry {
public:
  NatsRegistry(std::unique_ptr<IKeyValue> metadata_kv,
               std::unique_ptr<IKeyValue> schema_kv)
      : metadata_kv_(std::move(metadata_kv)), schema_kv_(std::move(schema_kv)) {
  }

  // Push inspects the protobuf message type of msg, builds a
  // FileDescriptorSet containing the message's file and its transitive
  // dependencies, and stores it in the registry under the given subject.
  [[nodiscard]] std::error_code push(std::stop_token stop_token,
                                     std::string_view subject,
                                     const google::protobuf::Message &msg) {
    if (stop_token.stop_requested()) {
      return make_error_code(errc::cancelled);
    }
    if (subject.empty()) {
      return make_error_code(errc::invalid_argument);
    }

    const google::protobuf::Descriptor *descriptor = msg.GetDescriptor();
    if (descriptor == nullptr) {
      return make_error_code(errc::invalid_argument);
    }

    const google::protobuf::FileDescriptor *file = descriptor->file();
    if (file == nullptr) {
      return make_error_code(errc::internal);
    }

    google::protobuf::FileDescriptorSet fds =
        detail::build_file_descriptor_set(file);

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
    if (auto [rev, err] =
            schema_kv_->put(path, detail::to_byte_span(schema_bytes));
        err) {
      return err;
    }

    if (auto [rev, err] =
            metadata_kv_->put(subject, detail::to_byte_span(metadata_json));
        err) {
      return err;
    }

    return {};
  }

  // Pull returns the FileDescriptorSet and metadata associated with the
  // given NATS subject. It watches the registry buckets and blocks until the
  // subject is registered or cancellation is requested.
  [[nodiscard]] std::pair<pull_result, std::error_code>
  pull(std::stop_token stop_token, std::string_view subject) {
    if (stop_token.stop_requested()) {
      return {{}, make_error_code(errc::cancelled)};
    }
    if (subject.empty()) {
      return {{}, make_error_code(errc::invalid_argument)};
    }

    auto [meta_entry, meta_err] =
        detail::watch_key(stop_token, *metadata_kv_, subject);
    if (meta_err) {
      return {{}, meta_err};
    }

    Metadata meta{};
    {
      const auto raw = meta_entry->value();
      const std::string json_str(reinterpret_cast<const char *>(raw.data()),
                                 raw.size());
      if (auto err = glz::read_json(meta, json_str); err) {
        return {{}, make_error_code(errc::internal)};
      }
    }

    auto [schema_entry, schema_err] =
        detail::watch_key(stop_token, *schema_kv_, meta.schema.path);
    if (schema_err) {
      return {{}, schema_err};
    }

    pull_result result;
    result.meta = std::move(meta);
    {
      const auto raw = schema_entry->value();
      const std::string fds_bytes(reinterpret_cast<const char *>(raw.data()),
                                  raw.size());
      if (!result.fds.ParseFromString(fds_bytes)) {
        return {{}, make_error_code(errc::internal)};
      }
    }

    return {std::move(result), {}};
  }

private:
  std::unique_ptr<IKeyValue> metadata_kv_;
  std::unique_ptr<IKeyValue> schema_kv_;
};

// Create a Registry backed by NATS KV buckets. If the buckets do not exist they
// are created with default settings. The caller retains ownership of js and is
// responsible for closing the underlying NATS connection.
inline std::pair<std::unique_ptr<NatsRegistry>, std::error_code>
create_registry(jsCtx *js, std::string_view metadata_bucket,
                std::string_view schema_bucket) {
  if (js == nullptr) {
    return {nullptr, make_error_code(errc::invalid_argument)};
  }
  if (metadata_bucket.empty() || schema_bucket.empty()) {
    return {nullptr, make_error_code(errc::invalid_argument)};
  }

  auto get_or_create_kv =
      [&](std::string_view bucket) -> std::pair<std::unique_ptr<IKeyValue>, std::error_code> {
    const std::string bucket_str{bucket};
    kvStore *store = nullptr;
    natsStatus s = js_KeyValue(&store, js, bucket_str.c_str());
    if (s == NATS_OK) {
      return {std::make_unique<NatsKeyValue>(store), {}};
    }
    if (s == NATS_NOT_FOUND) {
      kvConfig cfg{};
      static_cast<void>(kvConfig_Init(&cfg));
      cfg.Bucket = bucket_str.c_str();
      s = js_CreateKeyValue(&store, js, &cfg);
      if (s == NATS_OK) {
        return {std::make_unique<NatsKeyValue>(store), {}};
      }
    }
    return {nullptr, make_nats_error(s)};
  };

  auto [metadata_kv, metadata_err] = get_or_create_kv(metadata_bucket);
  if (metadata_err) {
    return {nullptr, metadata_err};
  }

  auto [schema_kv, schema_err] = get_or_create_kv(schema_bucket);
  if (schema_err) {
    return {nullptr, schema_err};
  }

  return {
      std::make_unique<NatsRegistry>(std::move(metadata_kv),
                                     std::move(schema_kv)),
      {}};
}

} // namespace schemaregistry
