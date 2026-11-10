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

#include "schemaregistry/error.hpp"
#include "schemaregistry/kv.hpp"

#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace schemaregistry::testing {

class MockKeyValueEntry : public IKeyValueEntry {
public:
    MockKeyValueEntry(
        std::string key,
        std::vector<std::byte> value,
        KeyValueOperation op = KeyValueOperation::put
    )
        : key_(std::move(key))
        , value_(std::move(value))
        , operation_(op) {}

    [[nodiscard]] std::string_view key() const override { return key_; }
    [[nodiscard]] std::span<const std::byte> value() const override { return value_; }
    [[nodiscard]] std::uint64_t revision() const override { return 1; }
    [[nodiscard]] KeyValueOperation operation() const override { return operation_; }

private:
    std::string key_;
    std::vector<std::byte> value_;
    KeyValueOperation operation_;
};

class MockKeyWatcher : public IKeyWatcher {
public:
    MockKeyWatcher(
        std::string key,
        std::map<std::string, std::vector<std::byte>>& data,
        std::mutex& parent_mutex,
        std::map<std::string, std::vector<MockKeyWatcher*>>& watchers
    )
        : key_(std::move(key))
        , parent_mutex_(parent_mutex)
        , watchers_(watchers) {
        std::shared_ptr<MockKeyValueEntry> initial;
        {
            std::lock_guard<std::mutex> lock(parent_mutex_);
            watchers_[key_].push_back(this);
            auto it = data.find(key_);
            if (it != data.end()) {
                initial = std::make_shared<MockKeyValueEntry>(key_, it->second);
            }
        }

        std::lock_guard<std::mutex> lock(mutex_);
        if (initial) {
            queue_.push(std::move(initial));
        }
        queue_.push(nullptr);
    }

    ~MockKeyWatcher() override { stop(); }

    MockKeyWatcher(MockKeyWatcher&&) = delete;
    MockKeyWatcher& operator=(MockKeyWatcher&&) = delete;
    MockKeyWatcher(const MockKeyWatcher&) = delete;
    MockKeyWatcher& operator=(const MockKeyWatcher&) = delete;

    [[nodiscard]] std::pair<std::shared_ptr<IKeyValueEntry>, std::error_code>
    next(
        std::stop_token stop_token
    ) override {
        std::unique_lock<std::mutex> lock(mutex_);
        auto predicate = [this] { return cancelled_ || !queue_.empty(); };

        if (stop_token.stop_possible()) {
            std::stop_callback cb(stop_token, [this] { request_stop(); });
            cv_.wait(lock, predicate);
        } else {
            cv_.wait(lock, predicate);
        }

        if (cancelled_) {
            return {nullptr, make_error_code(errc::cancelled)};
        }

        auto entry = std::move(queue_.front());
        queue_.pop();
        return {entry, {}};
    }

    void stop() override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            cancelled_ = true;
        }
        cv_.notify_all();

        std::lock_guard<std::mutex> lock(parent_mutex_);
        auto it = watchers_.find(key_);
        if (it != watchers_.end()) {
            auto& vec = it->second;
            vec.erase(std::remove(vec.begin(), vec.end(), this), vec.end());
        }
    }

    void notify(std::shared_ptr<IKeyValueEntry> entry) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (cancelled_) {
                return;
            }
            queue_.push(std::move(entry));
        }
        cv_.notify_one();
    }

private:
    void request_stop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            cancelled_ = true;
        }
        cv_.notify_all();
    }

    std::string key_;
    std::mutex& parent_mutex_;
    std::map<std::string, std::vector<MockKeyWatcher*>>& watchers_;

    std::mutex mutex_;
    std::condition_variable cv_;
    bool cancelled_ = false;
    std::queue<std::shared_ptr<IKeyValueEntry>> queue_;
};

class MockKeyValue : public IKeyValue {
public:
    MockKeyValue() = default;

    [[nodiscard]] std::pair<std::uint64_t, std::error_code>
    put(
        std::string_view key,
        std::span<const std::byte> value
    ) override {
        std::shared_ptr<IKeyValueEntry> entry;
        std::vector<MockKeyWatcher*> watchers_copy;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            std::vector<std::byte> copy(value.begin(), value.end());
            data_[std::string(key)] = std::move(copy);
            entry = std::make_shared<MockKeyValueEntry>(
                std::string(key),
                data_[std::string(key)]
            );

            auto it = watchers_.find(std::string(key));
            if (it != watchers_.end()) {
                watchers_copy = it->second;
            }
        }

        for (auto* watcher : watchers_copy) {
            watcher->notify(entry);
        }
        return {std::uint64_t{1}, {}};
    }

    [[nodiscard]] std::pair<std::shared_ptr<IKeyValueEntry>, std::error_code>
    get(
        std::string_view key
    ) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = data_.find(std::string(key));
        if (it == data_.end()) {
            return {nullptr, make_error_code(errc::not_found)};
        }
        return {std::make_shared<MockKeyValueEntry>(std::string(key), it->second), {}};
    }

    [[nodiscard]] std::pair<std::unique_ptr<IKeyWatcher>, std::error_code>
    watch(
        std::string_view key,
        std::stop_token /*stop_token*/
    ) override {
        return {std::make_unique<MockKeyWatcher>(
            std::string(key),
            data_,
            mutex_,
            watchers_
        ), {}};
    }

    [[nodiscard]] std::string_view bucket() const override { return "mock"; }

private:
    mutable std::mutex mutex_;
    std::map<std::string, std::vector<std::byte>> data_;
    std::map<std::string, std::vector<MockKeyWatcher*>> watchers_;
};

} // namespace schemaregistry::testing

