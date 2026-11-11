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
#include <span>
#include <string_view>

namespace schemaregistry::nats {

// RAII wrapper around a natsMsg pointer. Ownership is transferred through
// std::unique_ptr returned by Subscription::next_message().
class Message {
public:
  explicit Message(natsMsg *msg) : msg_(msg) {}

  ~Message() {
    if (msg_ != nullptr) {
      natsMsg_Destroy(msg_);
    }
  }

  Message(const Message &) = delete;
  Message &operator=(const Message &) = delete;
  Message(Message &&) = delete;
  Message &operator=(Message &&) = delete;

  [[nodiscard]]
  natsMsg *get() const noexcept {
    return msg_;
  }

  [[nodiscard]]
  std::string_view subject() const noexcept {
    return natsMsg_GetSubject(msg_);
  }

  [[nodiscard]]
  std::string_view reply() const noexcept {
    return natsMsg_GetReply(msg_);
  }

  [[nodiscard]]
  std::span<const std::byte> data() const noexcept {
    return {reinterpret_cast<const std::byte *>(natsMsg_GetData(msg_)),
            static_cast<std::size_t>(natsMsg_GetDataLength(msg_))};
  }

private:
  natsMsg *msg_;
};

} // namespace schemaregistry::nats
