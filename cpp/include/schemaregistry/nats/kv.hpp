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

#include "schemaregistry/nats/error.hpp"

#if __has_include(<nats/nats.h>)
#include <nats/nats.h>
#else
#include <nats.h>
#endif

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>

namespace schemaregistry::nats {

class JetStream;

// RAII wrapper around a kvEntry pointer. Exposes the key, value, revision and
// operation for a single KeyValue entry.
class KeyValueEntry {
public:
  // Operation type reported by a KeyValue entry watcher.
  enum class Operation {
    Unknown,
    Put,
    Delete,
    Purge,
  };

  explicit KeyValueEntry(kvEntry *entry) : entry_(entry) {}

  ~KeyValueEntry() {
    if (entry_ != nullptr) {
      kvEntry_Destroy(entry_);
    }
  }

  KeyValueEntry(const KeyValueEntry &) = delete;
  KeyValueEntry &operator=(const KeyValueEntry &) = delete;
  KeyValueEntry(KeyValueEntry &&) = delete;
  KeyValueEntry &operator=(KeyValueEntry &&) = delete;

  [[nodiscard]]
  std::string_view key() const {
    return kvEntry_Key(entry_);
  }

  [[nodiscard]]
  std::span<const std::byte> value() const {
    return {static_cast<const std::byte *>(kvEntry_Value(entry_)),
            static_cast<std::size_t>(kvEntry_ValueLen(entry_))};
  }

  [[nodiscard]]
  std::uint64_t revision() const {
    return kvEntry_Revision(entry_);
  }

  [[nodiscard]]
  Operation operation() const {
    switch (kvEntry_Operation(entry_)) {
    case kvOp_Put:
      return Operation::Put;
    case kvOp_Delete:
      return Operation::Delete;
    case kvOp_Purge:
      return Operation::Purge;
    default:
      return Operation::Unknown;
    }
  }

private:
  kvEntry *entry_;
};

// RAII wrapper around a kvWatcher pointer. The watcher yields the current value
// on the first call to next() and then reports subsequent updates until stop()
// is called.
class KeyWatcher {
public:
  using msec = std::chrono::milliseconds;

  explicit KeyWatcher(kvWatcher *watcher) : watcher_(watcher) {}

  ~KeyWatcher() {
    if (watcher_ != nullptr) {
      kvWatcher_Destroy(watcher_);
    }
  }

  KeyWatcher(const KeyWatcher &) = delete;
  KeyWatcher &operator=(const KeyWatcher &) = delete;
  KeyWatcher(KeyWatcher &&) = delete;
  KeyWatcher &operator=(KeyWatcher &&) = delete;

  [[nodiscard]]
  std::unique_ptr<KeyValueEntry> next(std::stop_token stop_token,
                                      std::error_code &ec,
                                      msec timeout = msec{1000}) {
    // Use a stop callback to interrupt the blocking watcher when
    // cancellation is requested.
    std::stop_callback cb{stop_token, [w = watcher_] { kvWatcher_Stop(w); }};

    kvEntry *entry;
    const natsStatus s =
        kvWatcher_Next(&entry, watcher_, static_cast<int64_t>(timeout.count()));

    if (s == NATS_TIMEOUT) {
      // Timeout with no update. Return an empty optional-like result so the
      // caller can retry while still respecting cancellation.
      return {};
    }
    if (s != NATS_OK) {
      ec = make_nats_error(s);
      return nullptr;
    }
    if (entry == nullptr) {
      return {};
    }
    ec = {};
    return std::make_unique<KeyValueEntry>(entry);
  }

  void stop() { kvWatcher_Stop(watcher_); }

private:
  kvWatcher *watcher_;
  std::string key_;
};

// RAII wrapper around a kvStore pointer. Holds a reference to the owning
// JetStream context so the underlying jsCtx (and therefore natsConnection)
// outlives the bucket.
class KeyValue {
public:
  using msec = std::chrono::milliseconds;

  KeyValue(kvStore *store, std::shared_ptr<JetStream> js)
      : store_(store), js_(std::move(js)) {}

  ~KeyValue() {
    if (store_ != nullptr) {
      kvStore_Destroy(store_);
    }
  }

  KeyValue(const KeyValue &) = delete;
  KeyValue &operator=(const KeyValue &) = delete;
  KeyValue(KeyValue &&) = delete;
  KeyValue &operator=(KeyValue &&) = delete;

  [[nodiscard]]
  std::error_code put(std::string_view key, std::span<const std::byte> value,
                      std::uint64_t &rev) {
    const std::string key_str{key};
    const auto s = kvStore_Put(&rev, store_, key_str.c_str(), value.data(),
                               static_cast<int>(value.size()));
    if (s != NATS_OK) {
      return make_nats_error(s);
    }
    return {};
  }

  [[nodiscard]]
  std::error_code put(std::string_view key, std::string_view value,
                      std::uint64_t &rev) {
    return put(key,
               std::span{reinterpret_cast<const std::byte *>(value.data()),
                         value.size()},
               rev);
  }

  [[nodiscard]]
  std::unique_ptr<KeyValueEntry> get(std::string_view key,
                                     std::error_code &ec) {
    kvEntry *kv_entry;
    const std::string key_str{key};
    const auto s = kvStore_Get(&kv_entry, store_, key_str.c_str());
    if (s != NATS_OK) {
      ec = make_nats_error(s);
      return nullptr;
    }
    ec = {};
    return std::make_unique<KeyValueEntry>(kv_entry);
  }

  struct WatchOptions : kvWatchOptions {
    WatchOptions() { kvWatchOptions_Init(this); }

    WatchOptions(const kvWatchOptions &opts) : kvWatchOptions(opts) {}
  };

  [[nodiscard]]
  std::unique_ptr<KeyWatcher> watch(std::string_view key, std::error_code &ec,
                                    const WatchOptions &opts = {}) {
    kvWatcher *w;
    const std::string key_str{key};
    const auto s = kvStore_Watch(&w, store_, key_str.c_str(), &opts);
    if (s != NATS_OK) {
      ec = make_nats_error(s);
      return nullptr;
    }
    ec = {};
    return std::make_unique<KeyWatcher>(w);
  }

  [[nodiscard]]
  std::string_view bucket() const {
    return kvStore_Bucket(store_);
  }

private:
  kvStore *store_;
  std::shared_ptr<JetStream> js_;
};

} // namespace schemaregistry::nats
