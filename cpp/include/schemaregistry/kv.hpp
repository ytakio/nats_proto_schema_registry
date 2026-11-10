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

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

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
    [[nodiscard]] virtual std::pair<std::shared_ptr<IKeyValueEntry>, std::error_code>
    next(std::stop_token stop_token) = 0;

    virtual void stop() = 0;
};

// IKeyValue is a minimal abstraction over a NATS KV bucket.
class IKeyValue {
public:
    virtual ~IKeyValue() = default;

    [[nodiscard]] virtual std::pair<std::uint64_t, std::error_code>
    put(std::string_view key, std::span<const std::byte> value) = 0;

    [[nodiscard]] virtual std::pair<std::shared_ptr<IKeyValueEntry>, std::error_code>
    get(std::string_view key) = 0;

    [[nodiscard]] virtual std::pair<std::unique_ptr<IKeyWatcher>, std::error_code>
    watch(std::string_view key, std::stop_token stop_token) = 0;

    [[nodiscard]] virtual std::string_view bucket() const = 0;
};

} // namespace schemaregistry
