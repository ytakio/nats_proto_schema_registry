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
#include "schemaregistry/nats/jetstream.hpp"
#include "schemaregistry/nats/subscription.hpp"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace schemaregistry::nats {

// RAII wrapper around a natsConnection pointer. Returned as a std::shared_ptr
// so that JetStream and Subscription objects can keep the underlying NATS
// connection alive for their lifetime.
//
// The factory stores a std::weak_ptr to itself in the Connection. When
// jetstream() or subscribe() is called, the weak_ptr is locked to produce a
// std::shared_ptr<Connection> for the dependent object. This avoids inheriting
// from std::enable_shared_from_this while still guaranteeing that the
// natsConnection outlives any JetStream or Subscription created from it.
class Connection {
public:
  ~Connection() {
    if (conn_ != nullptr) {
      natsConnection_Destroy(conn_);
    }
  }

  Connection(const Connection &) = delete;
  Connection &operator=(const Connection &) = delete;
  Connection(Connection &&) = delete;
  Connection &operator=(Connection &&) = delete;

  [[nodiscard]]
  natsConnection *get() const noexcept {
    return conn_;
  }

  [[nodiscard]]
  static std::shared_ptr<Connection> connect(std::string_view url,
                                             std::error_code &ec) {
    natsConnection *conn;
    const std::string url_str{url};
    const auto s = natsConnection_ConnectTo(&conn, url_str.c_str());
    if (s != NATS_OK) {
      ec = make_nats_error(s);
      return nullptr;
    }
    ec = {};
    auto result = std::shared_ptr<Connection>(new Connection(conn));
    result->self_ = result;
    return result;
  }

  [[nodiscard]]
  std::shared_ptr<JetStream> jetstream(std::error_code &ec,
                                       jsOptions *opts = nullptr) {
    jsCtx *js;
    const auto s = natsConnection_JetStream(&js, conn_, opts);
    if (s != NATS_OK) {
      ec = make_nats_error(s);
      return nullptr;
    }
    ec = {};
    auto result = std::shared_ptr<JetStream>(new JetStream(js, self_.lock()));
    result->set_self(result);
    return result;
  }

  [[nodiscard]]
  std::error_code publish(std::string_view subject,
                          std::string_view data) const {
    const std::string subj{subject};
    const int size = static_cast<int>(data.size());
    const auto s =
        natsConnection_Publish(conn_, subj.c_str(), data.data(), size);
    if (s != NATS_OK) {
      return make_nats_error(s);
    }
    return {};
  }

  using MessageCallback = std::function<void(std::unique_ptr<Message>)>;

  [[nodiscard]]
  std::unique_ptr<Subscription> subscribe(std::string_view subject,
                                          MessageCallback callback,
                                          std::error_code &ec) {

    auto *closure = new Subscription::Closure{std::move(callback)};

    natsSubscription *sub;
    const std::string subj{subject};
    const auto s = natsConnection_Subscribe(&sub, conn_, subj.c_str(),
                                            &Connection::msg_cb, closure);

    if (s != NATS_OK) {
      ec = make_nats_error(s);
      delete closure;
      return nullptr;
    }

    ec = {};
    return std::make_unique<Subscription>(sub, closure, self_.lock());
  }

private:
  explicit Connection(natsConnection *conn) : conn_(conn) {}

  static void msg_cb(natsConnection *, natsSubscription *, natsMsg *msg,
                     void *closure) {
    auto *c = static_cast<Subscription::Closure *>(closure);
    c->callback(std::make_unique<Message>(msg));
  }

  std::weak_ptr<Connection> self_;
  natsConnection *conn_;
};

} // namespace schemaregistry::nats
