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

#include "schemaregistry/nats/message.hpp"

#include <functional>
#include <memory>

namespace schemaregistry::nats {

class Connection;

// RAII wrapper around a natsSubscription pointer. Holds a reference to the
// owning Connection so the underlying natsConnection outlives the subscription.
class Subscription {
public:
  // Closure data passed to the C asynchronous subscription callback. Kept alive
  // for the lifetime of the Subscription that owns it.
  struct Closure {
    std::function<void(std::unique_ptr<Message>)> callback;
  };

  Subscription(natsSubscription *sub, Closure *closure,
               std::shared_ptr<Connection> conn)
      : sub_(sub), closure_(closure), conn_(std::move(conn)) {}

  ~Subscription() {
    natsSubscription_Destroy(sub_);
    delete closure_;
  }

  Subscription(const Subscription &) = delete;
  Subscription &operator=(const Subscription &) = delete;
  Subscription(Subscription &&) = delete;
  Subscription &operator=(Subscription &&) = delete;

  [[nodiscard]]
  natsSubscription *get() const noexcept {
    return sub_;
  }

private:
  natsSubscription *sub_;
  Closure *closure_;
  std::shared_ptr<Connection> conn_;
};

} // namespace schemaregistry::nats
