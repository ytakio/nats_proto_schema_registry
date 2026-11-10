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

#if __has_include(<nats/nats.h>)
#include <nats/nats.h>
#else
#include <nats.h>
#endif

#include <chrono>
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

namespace detail {

inline std::span<const std::byte> to_byte_span(std::string_view value) {
  return {reinterpret_cast<const std::byte *>(value.data()), value.size()};
}

} // namespace detail

class NatsKeyValueEntry : public IKeyValueEntry {
public:
  explicit NatsKeyValueEntry(kvEntry *entry) : entry_(entry) {}

  ~NatsKeyValueEntry() override { kvEntry_Destroy(entry_); }

  NatsKeyValueEntry(const NatsKeyValueEntry &) = delete;
  NatsKeyValueEntry &operator=(const NatsKeyValueEntry &) = delete;
  NatsKeyValueEntry(NatsKeyValueEntry &&) = delete;
  NatsKeyValueEntry &operator=(NatsKeyValueEntry &&) = delete;

  [[nodiscard]] std::string_view key() const override {
    return kvEntry_Key(entry_);
  }

  [[nodiscard]] std::span<const std::byte> value() const override {
    return {static_cast<const std::byte *>(kvEntry_Value(entry_)),
            static_cast<std::size_t>(kvEntry_ValueLen(entry_))};
  }

  [[nodiscard]] std::uint64_t revision() const override {
    return kvEntry_Revision(entry_);
  }

  [[nodiscard]] KeyValueOperation operation() const override {
    switch (kvEntry_Operation(entry_)) {
    case kvOp_Put:
      return KeyValueOperation::put;
    case kvOp_Delete:
      return KeyValueOperation::del;
    case kvOp_Purge:
      return KeyValueOperation::purge;
    default:
      return KeyValueOperation::unknown;
    }
  }

private:
  kvEntry *entry_;
};

class NatsKeyWatcher : public IKeyWatcher {
public:
  NatsKeyWatcher(kvWatcher *watcher, std::string key)
      : watcher_(watcher), key_(std::move(key)) {}

  ~NatsKeyWatcher() override { kvWatcher_Destroy(watcher_); }

  NatsKeyWatcher(const NatsKeyWatcher &) = delete;
  NatsKeyWatcher &operator=(const NatsKeyWatcher &) = delete;
  NatsKeyWatcher(NatsKeyWatcher &&) = delete;
  NatsKeyWatcher &operator=(NatsKeyWatcher &&) = delete;

  [[nodiscard]] std::pair<std::shared_ptr<IKeyValueEntry>, std::error_code>
  next(std::stop_token stop_token) override {
    // Use a stop callback to interrupt the blocking watcher when
    // cancellation is requested. A small functor avoids the heap
    // allocation that std::function would incur.
    struct stopper {
      kvWatcher *watcher;
      void operator()() const noexcept { kvWatcher_Stop(watcher); }
    };
    std::optional<std::stop_callback<stopper>> cb;
    if (stop_token.stop_possible()) {
      cb.emplace(stop_token, stopper{watcher_});
    }

    kvEntry *entry = nullptr;
    const natsStatus s = kvWatcher_Next(&entry, watcher_,
                                        static_cast<int64_t>(timeout_.count()));

    if (s == NATS_TIMEOUT) {
      // Timeout with no update. Return nullptr entry so the caller can
      // retry while still respecting cancellation.
      return {std::shared_ptr<IKeyValueEntry>{}, {}};
    }
    if (s != NATS_OK) {
      return {nullptr, make_nats_error(s)};
    }
    if (entry == nullptr) {
      return {std::shared_ptr<IKeyValueEntry>{}, {}};
    }
    return {std::make_shared<NatsKeyValueEntry>(entry), {}};
  }

  void stop() override { kvWatcher_Stop(watcher_); }

private:
  kvWatcher *watcher_;
  std::string key_;
  std::chrono::milliseconds timeout_{1000};
};

class NatsKeyValue : public IKeyValue {
public:
  explicit NatsKeyValue(kvStore *store) : store_(store) {}

  ~NatsKeyValue() override { kvStore_Destroy(store_); }

  NatsKeyValue(const NatsKeyValue &) = delete;
  NatsKeyValue &operator=(const NatsKeyValue &) = delete;
  NatsKeyValue(NatsKeyValue &&) = delete;
  NatsKeyValue &operator=(NatsKeyValue &&) = delete;

  [[nodiscard]] std::pair<std::uint64_t, std::error_code>
  put(std::string_view key, std::span<const std::byte> value) override {
    uint64_t rev = 0;
    const std::string key_str{key};
    const natsStatus s =
        kvStore_Put(&rev, store_, key_str.c_str(), value.data(),
                    static_cast<int>(value.size()));
    if (s != NATS_OK) {
      return {0, make_nats_error(s)};
    }
    return {std::uint64_t{rev}, {}};
  }

  [[nodiscard]] std::pair<std::shared_ptr<IKeyValueEntry>, std::error_code>
  get(std::string_view key) override {
    kvEntry *entry = nullptr;
    const std::string key_str(key);
    const natsStatus s = kvStore_Get(&entry, store_, key_str.c_str());
    if (s != NATS_OK) {
      return {nullptr, make_nats_error(s)};
    }
    if (entry == nullptr) {
      return {nullptr, make_error_code(errc::not_found)};
    }
    return {std::make_shared<NatsKeyValueEntry>(entry), {}};
  }

  [[nodiscard]] std::pair<std::unique_ptr<IKeyWatcher>, std::error_code>
  watch(std::string_view key, std::stop_token /*stop_token*/
        ) override {
    kvWatchOptions opts{};
    kvWatchOptions_Init(&opts);
    opts.IgnoreDeletes = true;

    const std::string key_str(key);
    kvWatcher *watcher = nullptr;
    const natsStatus s =
        kvStore_Watch(&watcher, store_, key_str.c_str(), &opts);
    if (s != NATS_OK) {
      return {nullptr, make_nats_error(s)};
    }
    return {std::make_unique<NatsKeyWatcher>(watcher, key_str), {}};
  }

  [[nodiscard]] std::string_view bucket() const override {
    return kvStore_Bucket(store_);
  }

private:
  kvStore *store_;
};

} // namespace schemaregistry
