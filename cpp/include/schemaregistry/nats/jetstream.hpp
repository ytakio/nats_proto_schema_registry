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
#include "schemaregistry/nats/kv.hpp"

#if __has_include(<nats/nats.h>)
#include <nats/nats.h>
#else
#include <nats.h>
#endif

#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace schemaregistry::nats {

class Connection;

// RAII wrapper around a jsCtx pointer. Created from Connection::jetstream() and
// shared between the registry and any callers that need it. Holds a reference
// to the owning Connection so the underlying natsConnection outlives the
// JetStream context.
class JetStream {
public:
  JetStream(jsCtx *js, std::shared_ptr<Connection> conn)
      : js_(js), conn_(std::move(conn)) {}

  ~JetStream() {
    if (js_ != nullptr) {
      jsCtx_Destroy(js_);
    }
  }

  JetStream(const JetStream &) = delete;
  JetStream &operator=(const JetStream &) = delete;
  JetStream(JetStream &&) = delete;
  JetStream &operator=(JetStream &&) = delete;

  // Called once by Connection::jetstream() so that KeyValue objects created
  // from this JetStream can keep it alive.
  void set_self(std::weak_ptr<JetStream> self) { self_ = std::move(self); }

  [[nodiscard]]
  jsCtx *get() const noexcept {
    return js_;
  }

  // Open an existing KeyValue bucket. Returns nullptr and sets ec on failure.
  [[nodiscard]]
  std::unique_ptr<KeyValue> key_value(std::string_view bucket,
                                      std::error_code &ec) {
    const std::string bkt{bucket};
    kvStore *store;
    const natsStatus s = js_KeyValue(&store, js_, bkt.c_str());
    if (s != NATS_OK) {
      ec = make_nats_error(s);
      return nullptr;
    }
    ec = {};
    return std::make_unique<KeyValue>(store, self_.lock());
  }

  // Create a new KeyValue bucket. If cfg is nullptr, default settings are
  // used. The bucket name argument always overrides cfg->Bucket. Returns
  // nullptr and sets ec on failure.
  [[nodiscard]]
  std::unique_ptr<KeyValue> create_key_value(std::string_view bucket,
                                             std::error_code &ec,
                                             kvConfig *cfg = nullptr) {
    const std::string bkt{bucket};
    kvConfig default_cfg{};
    if (cfg == nullptr) {
      kvConfig_Init(&default_cfg);
      cfg = &default_cfg;
    }
    cfg->Bucket = bkt.c_str();

    kvStore *store;
    const natsStatus s = js_CreateKeyValue(&store, js_, cfg);
    if (s != NATS_OK) {
      ec = make_nats_error(s);
      return nullptr;
    }
    ec = {};
    return std::make_unique<KeyValue>(store, self_.lock());
  }

private:
  jsCtx *js_;
  std::shared_ptr<Connection> conn_;
  std::weak_ptr<JetStream> self_;
};

} // namespace schemaregistry::nats
