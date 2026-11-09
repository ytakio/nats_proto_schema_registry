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

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
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

// KeyValueEntry is a single entry returned from a KeyValue store.
class KeyValueEntry {
public:
    virtual ~KeyValueEntry() = default;

    [[nodiscard]] virtual std::string_view key() const = 0;
    [[nodiscard]] virtual std::span<const std::byte> value() const = 0;
    [[nodiscard]] virtual std::uint64_t revision() const = 0;
    [[nodiscard]] virtual KeyValueOperation operation() const = 0;
};

// KeyWatcher watches a key in a KeyValue store.
class KeyWatcher {
public:
    virtual ~KeyWatcher() = default;

    // Returns the next entry for this watcher, or std::nullopt if the watcher
    // was stopped or cancellation was requested. A returned shared_ptr may be
    // nullptr to indicate an initial-state marker.
    [[nodiscard]] virtual std::optional<std::shared_ptr<KeyValueEntry>> next(
        std::stop_token stop_token
    ) = 0;

    virtual void stop() = 0;
};

// KeyValue is a minimal abstraction over a NATS KV bucket.
class KeyValue {
public:
    virtual ~KeyValue() = default;

    [[nodiscard]] virtual std::uint64_t put(
        std::string_view key,
        std::span<const std::byte> value,
        std::error_code& ec
    ) = 0;

    [[nodiscard]] virtual std::shared_ptr<KeyValueEntry> get(
        std::string_view key,
        std::error_code& ec
    ) = 0;

    [[nodiscard]] virtual std::unique_ptr<KeyWatcher> watch(
        std::string_view key,
        std::stop_token stop_token,
        std::error_code& ec
    ) = 0;

    [[nodiscard]] virtual std::string_view bucket() const = 0;
};

} // namespace schemaregistry

