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

#if __has_include(<nats/nats.h>)
#include <nats/nats.h>
#else
#include <nats.h>
#endif

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

#include "schemaregistry/nats/error.hpp"

namespace schemaregistry::nats {

// RAII wrapper around a natsMsg pointer. Ownership is transferred through
// std::unique_ptr returned by Subscription::next_message().
class Msg {
public:
    explicit Msg(natsMsg* msg) : msg_(msg) {}

    ~Msg() {
        if (msg_ != nullptr) {
            natsMsg_Destroy(msg_);
        }
    }

    Msg(const Msg&) = delete;
    Msg& operator=(const Msg&) = delete;
    Msg(Msg&&) = delete;
    Msg& operator=(Msg&&) = delete;

    [[nodiscard]]
    natsMsg* get() const noexcept {
        return msg_;
    }

    // The subject is always present for a message delivered by NATS. The
    // returned view aliases storage owned by the wrapped natsMsg and therefore
    // must not outlive this Message.
    [[nodiscard]]
    std::string_view subject() const noexcept {
        return natsMsg_GetSubject(msg_);
    }

    // The reply subject is optional in the NATS protocol and nats.c models its
    // absence as NULL, so the accessor keeps that distinction visible by
    // returning std::optional: a caller has to acknowledge that no reply may be
    // present instead of reading an empty view as if it were a valid subject.
    // nats.c never yields an empty one (natsMsg_Create() rejects it and the
    // parser stores NULL), so std::nullopt is the only way absence can appear.
    // The value aliases storage owned by the wrapped natsMsg and must not
    // outlive it.
    [[nodiscard]]
    std::optional<std::string_view> reply() const noexcept {
        const char* reply = natsMsg_GetReply(msg_);
        if (reply == nullptr) {
            return std::nullopt;
        }
        return reply;
    }

    // The payload may be empty but is always present for a valid NATS
    // message. The returned span aliases storage owned by the wrapped natsMsg
    // and must not outlive this Message.
    [[nodiscard]]
    std::span<const std::byte> data() const noexcept {
        const char* data = natsMsg_GetData(msg_);
        if (data == nullptr) {
            return {};
        }
        return {reinterpret_cast<const std::byte*>(data),
                static_cast<std::size_t>(natsMsg_GetDataLength(msg_))};
    }

    // Sets a header value, replacing any existing values for the key. The
    // value is optional and is passed as NULL to nats.c when absent, which the
    // underlying API accepts. The key is taken as const std::string& because it
    // is always required and must be NUL-terminated for nats.c. An empty key is
    // rejected by the underlying API and returned as an error. The value is
    // taken as std::optional<std::string> so that NUL-termination is guaranteed
    // without requiring an extra copy before forwarding to nats.c.
    [[nodiscard]]
    std::error_code header_set(const std::string& key,
                               std::optional<std::string> value) {
        const char* value_ptr = value ? value->c_str() : nullptr;
        return make_nats_error(natsMsgHeader_Set(msg_, key.c_str(), value_ptr));
    }

    // Adds a header value, appending to any existing values for the key. The
    // value is optional and is passed as NULL to nats.c when absent, which the
    // underlying API accepts. The key is taken as const std::string& because it
    // is always required and must be NUL-terminated for nats.c. An empty key is
    // rejected by the underlying API and returned as an error.
    [[nodiscard]]
    std::error_code header_add(const std::string& key,
                               std::optional<std::string> value) {
        const char* value_ptr = value ? value->c_str() : nullptr;
        return make_nats_error(natsMsgHeader_Add(msg_, key.c_str(), value_ptr));
    }

    // Deletes the value(s) associated with a header key. A missing key is
    // returned as NATS_NOT_FOUND. The key is taken as const std::string&
    // because it is always required and must be NUL-terminated for nats.c.
    [[nodiscard]]
    std::error_code header_delete(const std::string& key) {
        return make_nats_error(natsMsgHeader_Delete(msg_, key.c_str()));
    }

    // Returns the first value associated with a header key. The returned view
    // aliases storage owned by the wrapped natsMsg and must not outlive this
    // Message. A missing key, or any other nats.c failure, is reported through
    // ec; callers should inspect the returned optional to distinguish a missing
    // header from one whose value is empty/null.
    [[nodiscard]]
    std::optional<std::string_view> header_get(const std::string& key,
                                               std::error_code& ec) const {
        const char* value = nullptr;
        const auto s = natsMsgHeader_Get(msg_, key.c_str(), &value);
        ec = {};
        if (s != NATS_OK) {
            ec = make_nats_error(s);
            return std::nullopt;
        }
        if (value == nullptr) {
            return {};
        }
        return value;
    }

    // Builds a message from its parts. The reply subject and payload are both
    // optional, so their absence is passed to natsMsg_Create() as NULL, which
    // that call accepts, while an empty reply subject stays empty and is
    // rejected, matching NATS semantics. On failure the corresponding nats.c
    // status is reported through ec and nullptr is returned, as for the other
    // factories in this namespace.
    //
    // A std::unique_ptr is returned rather than a Message by value: Message is
    // neither copyable nor movable, so no factory could return it by value, and
    // every other place in this namespace already hands messages out as
    // std::unique_ptr<Message>.
    [[nodiscard]]
    static std::unique_ptr<Msg> create(
        const std::string& subject, std::optional<std::string> reply,
        std::optional<std::span<const std::byte>> data, std::error_code& ec) {
        // The reply subject is optional and taken as std::optional<std::string>
        // so that NUL-termination is guaranteed without requiring an extra copy
        // before forwarding to nats.c. The subject is already NUL-terminated
        // because it is taken as const std::string&.
        const char* reply_ptr = reply ? reply->c_str() : nullptr;

        const char* data_ptr = nullptr;
        int data_len = 0;
        if (data) {
            data_ptr = reinterpret_cast<const char*>(data->data());
            data_len = static_cast<int>(data->size());
        }

        natsMsg* msg = nullptr;
        const auto s = natsMsg_Create(&msg, subject.c_str(), reply_ptr,
                                      data_ptr, data_len);
        ec = {};
        if (s != NATS_OK) {
            ec = make_nats_error(s);
            return nullptr;
        }
        return std::make_unique<Msg>(msg);
    }

private:
    natsMsg* msg_;
};

}  // namespace schemaregistry::nats
