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
#include "schemaregistry/nats/jetstream.hpp"
#include "schemaregistry/nats/kv.hpp"
#include "schemaregistry/registry.hpp"

#if __has_include(<nats/nats.h>)
#include <nats/nats.h>
#else
#include <nats.h>
#endif

#include <memory>
#include <string_view>
#include <system_error>

namespace schemaregistry {

// Registry backed by NATS JetStream KeyValue buckets.
using NatsRegistry = Registry<nats::KeyValue, nats::KeyValue>;

// Create a Registry backed by NATS KV buckets. If the buckets do not exist they
// are created with default settings. The JetStream context must outlive the
// returned registry. On failure ec is set and nullptr is returned.
[[nodiscard]] inline std::unique_ptr<NatsRegistry>
create_registry(nats::JetStream &js, std::string_view metadata_bucket,
                std::string_view schema_bucket, std::error_code &ec) {

  if (metadata_bucket.empty() || schema_bucket.empty()) {
    ec = make_error_code(registry_errc::invalid_argument);
    return nullptr;
  }

  auto get_or_create_kv =
      [&](std::string_view bucket) -> std::unique_ptr<nats::KeyValue> {
    std::error_code kv_ec;
    auto kv = js.key_value(bucket, kv_ec);
    if (!kv_ec) {
      return kv;
    }
    if (kv_ec.value() != NATS_NOT_FOUND) {
      ec = kv_ec;
      return nullptr;
    }

    kv = js.create_key_value(bucket, kv_ec);
    if (kv_ec) {
      ec = kv_ec;
      return nullptr;
    }
    return kv;
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
