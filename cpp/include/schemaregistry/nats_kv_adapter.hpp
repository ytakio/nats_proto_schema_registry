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

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "schemaregistry/nats/error.hpp"
#include "schemaregistry/nats/kv.hpp"

namespace schemaregistry {

using nats::make_nats_error;

class NatsKeyValueAdapter {
public:
    explicit NatsKeyValueAdapter(std::unique_ptr<nats::KeyValue> kv)
        : kv_(std::move(kv)) {}

    [[nodiscard]]
    std::error_code put(const std::string& key,
                        std::span<const std::byte> value, std::uint64_t& rev) {
        return kv_->put(key, value, rev);
    }

    [[nodiscard]]
    std::error_code put(const std::string& key, std::string_view value,
                        std::uint64_t& rev) {
        return kv_->put(key, value, rev);
    }

    [[nodiscard]]
    std::vector<std::byte> get(const std::string& key,
                               std::stop_token stop_token,
                               std::error_code& ec) {
        if (stop_token.stop_requested()) {
            ec = make_nats_error(NATS_TIMEOUT);
            return {};
        }

        // A KV watch with the default options delivers the last value for the
        // key as its first event, or a null marker if the key does not exist
        // yet. Starting with watch avoids the extra round-trip of get()+watch()
        // and keeps the wait-for-value path uniform.
        nats::KeyValue::WatchOptions opts;
        opts.IgnoreDeletes = true;
        auto watcher = kv_->watch(key, ec, opts);
        if (ec) {
            return {};
        }

        for (;;) {
            if (stop_token.stop_requested()) {
                ec = make_nats_error(NATS_TIMEOUT);
                return {};
            }

            auto entry = watcher->next(stop_token, ec);
            if (ec) {
                return {};
            }
            if (!entry) {
                continue;
            }

            const auto op = entry->operation();
            if (op == nats::KeyValueEntry::Operation::Delete ||
                op == nats::KeyValueEntry::Operation::Purge) {
                continue;
            }

            const auto val = entry->value();
            return {val.begin(), val.end()};
        }
    }

    [[nodiscard]]
    std::string_view bucket() const {
        return kv_->bucket();
    }

private:
    std::unique_ptr<nats::KeyValue> kv_;
};

}  // namespace schemaregistry
