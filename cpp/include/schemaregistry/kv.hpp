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

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>

namespace schemaregistry {

enum class KeyValueOperation {
  unknown,
  put,
  del,
  purge,
};

// IKeyValueEntry is a single entry returned from a KeyValue store.
class IKeyValueEntry {
public:
  virtual ~IKeyValueEntry() = default;

  [[nodiscard]] virtual std::string_view key() const = 0;
  [[nodiscard]] virtual std::span<const std::byte> value() const = 0;
  [[nodiscard]] virtual std::uint64_t revision() const = 0;
  [[nodiscard]] virtual KeyValueOperation operation() const = 0;
};

// IKeyWatcher watches a key in a KeyValue store.
class IKeyWatcher {
public:
  virtual ~IKeyWatcher() = default;

  // Returns an entry on a real update, nullptr on a timeout/initial marker,
  // or an error if the watcher fails.
  [[nodiscard]] virtual std::shared_ptr<IKeyValueEntry>
  next(std::stop_token stop_token, std::error_code &ec) = 0;

  virtual void stop() = 0;
};

// IKeyValue is a minimal abstraction over a NATS KV bucket.
class IKeyValue {
public:
  virtual ~IKeyValue() = default;

  // Stores value under key. On success rev is set to the new revision.
  [[nodiscard]] virtual std::error_code put(std::string_view key,
                                            std::span<const std::byte> value,
                                            std::uint64_t &rev) = 0;

  // Convenience overload for callers that already hold a std::string
  // (e.g. values produced by protobuf's SerializeToString).
  // Kept virtual so implementations may override the conversion strategy.
  [[nodiscard]] virtual std::error_code put(std::string_view key,
                                            std::string_view value,
                                            std::uint64_t &rev) {
    return put(key,
               std::span{reinterpret_cast<const std::byte *>(value.data()),
                         value.size()},
               rev);
  }

  // Retrieves the entry for key. Returns nullptr and sets ec on failure.
  [[nodiscard]] virtual std::shared_ptr<IKeyValueEntry>
  get(std::string_view key, std::error_code &ec) = 0;

  // Create a watcher for the given key. The watcher yields the current value
  // on the first call to next() and then reports subsequent updates until
  // stop() is called. The timeout hint is backend-specific; the default is
  // chosen to avoid blocking forever while still allowing long poll behavior.
  // Returns nullptr and sets ec on failure.
  [[nodiscard]] virtual std::unique_ptr<IKeyWatcher>
  watch(std::string_view key, std::error_code &ec,
        std::chrono::milliseconds timeout = std::chrono::milliseconds{1000}) = 0;

  [[nodiscard]] virtual std::string_view bucket() const = 0;
};

} // namespace schemaregistry
