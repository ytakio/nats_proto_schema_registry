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

#include "mock_kv_store.hpp"
#include "schemaregistry/nats/error.hpp"
#include "schemaregistry/registry.hpp"

#include <google/protobuf/any.pb.h>
#include <google/protobuf/type.pb.h>
#include <google/protobuf/wrappers.pb.h>
#include <gtest/gtest.h>

#include <chrono>
#include <stop_token>
#include <string>
#include <thread>

using namespace schemaregistry;
using namespace schemaregistry::nats;
using namespace schemaregistry::testing;

namespace {

using msec = std::chrono::milliseconds;

class TimeoutSource {
public:
  explicit TimeoutSource(msec timeout)
      : thread_([this, timeout] {
          std::this_thread::sleep_for(timeout);
          source_.request_stop();
        }) {}

  [[nodiscard]] std::stop_token get_token() { return source_.get_token(); }

private:
  std::stop_source source_;
  std::jthread thread_;
};

using TestRegistry = Registry<MockKeyValue, MockKeyValue>;

} // namespace

TEST(RegistryTest, GetReturnsEntry) {
  auto kv = std::make_unique<MockKeyValue>();
  MockKeyValue *raw_kv = kv.get();

  TestRegistry reg(std::move(kv), std::make_unique<MockKeyValue>());

  const std::string key = "orders.created";
  const std::string value = "hello";
  std::uint64_t rev = 0;
  auto ec =
      raw_kv->put(key,
                  std::span{reinterpret_cast<const std::byte *>(value.data()),
                            value.size()},
                  rev);
  ASSERT_FALSE(ec) << ec.message();

  std::error_code get_err;
  auto entry = raw_kv->get(key, get_err);
  ASSERT_TRUE(entry) << get_err.message();
  EXPECT_EQ(entry->key(), key);

  const auto val = entry->value();
  const std::string got(reinterpret_cast<const char *>(val.data()), val.size());
  EXPECT_EQ(got, value);
}

TEST(RegistryTest, GetNotFound) {
  MockKeyValue kv;
  std::error_code ec;
  auto entry = kv.get("missing.key", ec);
  EXPECT_FALSE(entry);
  EXPECT_EQ(ec, make_nats_error(NATS_NOT_FOUND));
}

TEST(RegistryTest, PushAndPull) {
  TestRegistry reg(std::make_unique<MockKeyValue>(),
                   std::make_unique<MockKeyValue>());

  const std::string subject = "orders.created";
  google::protobuf::StringValue msg;
  msg.set_value("hello");

  auto push_err = reg.push({}, subject, msg);
  ASSERT_FALSE(push_err) << push_err.message();

  Metadata meta;
  std::error_code pull_err;
  auto fds = reg.pull({}, subject, meta, pull_err);
  ASSERT_TRUE(fds) << pull_err.message();
  EXPECT_EQ(meta.schema.full_name, "google.protobuf.StringValue");
  EXPECT_EQ(meta.schema.path, "google/protobuf/wrappers.proto");
  EXPECT_GT(fds->file_size(), 0);
}

TEST(RegistryTest, PushAny) {
  TestRegistry reg(std::make_unique<MockKeyValue>(),
                   std::make_unique<MockKeyValue>());

  auto push_err = reg.push({}, "events.any", google::protobuf::Any{});
  ASSERT_FALSE(push_err) << push_err.message();

  Metadata meta;
  std::error_code pull_err;
  auto fds = reg.pull({}, "events.any", meta, pull_err);
  ASSERT_TRUE(fds) << pull_err.message();
  EXPECT_EQ(meta.schema.full_name, "google.protobuf.Any");
  EXPECT_EQ(meta.schema.path, "google/protobuf/any.proto");
}

TEST(RegistryTest, PullNotFound) {
  TestRegistry reg(std::make_unique<MockKeyValue>(),
                   std::make_unique<MockKeyValue>());

  TimeoutSource source(msec(50));
  Metadata meta;
  std::error_code pull_err;
  auto fds = reg.pull(source.get_token(), "unknown.subject", meta, pull_err);
  EXPECT_FALSE(fds);
  EXPECT_EQ(pull_err, make_nats_error(NATS_TIMEOUT));
}

TEST(RegistryTest, PullWaitsForPush) {
  TestRegistry reg(std::make_unique<MockKeyValue>(),
                   std::make_unique<MockKeyValue>());

  const std::string subject = "orders.created";
  google::protobuf::StringValue msg;
  msg.set_value("hello");

  struct Result {
    std::unique_ptr<google::protobuf::FileDescriptorSet> fds;
    Metadata meta;
    std::error_code error;
  } result;

  std::jthread pull_thread([&reg, &subject, &result](std::stop_token token) {
    Metadata meta;
    std::error_code pull_err;
    auto fds = reg.pull(token, subject, meta, pull_err);
    if (!pull_err) {
      result.fds = std::move(fds);
      result.meta = std::move(meta);
    } else {
      result.error = pull_err;
    }
  });

  std::this_thread::sleep_for(msec(50));
  auto push_err = reg.push({}, subject, msg);
  ASSERT_FALSE(push_err) << push_err.message();
  pull_thread.join();

  ASSERT_FALSE(result.error) << result.error.message();
  EXPECT_EQ(result.meta.schema.full_name, "google.protobuf.StringValue");
  ASSERT_TRUE(result.fds);
  EXPECT_GT(result.fds->file_size(), 0);
}

TEST(RegistryTest, PushValidation) {
  TestRegistry reg(std::make_unique<MockKeyValue>(),
                   std::make_unique<MockKeyValue>());

  google::protobuf::StringValue msg;
  msg.set_value("x");

  auto push_err = reg.push({}, "", msg);
  EXPECT_TRUE(push_err);
  EXPECT_EQ(push_err, make_nats_error(NATS_INVALID_ARG));
}

TEST(RegistryTest, PullCancellation) {
  TestRegistry reg(std::make_unique<MockKeyValue>(),
                   std::make_unique<MockKeyValue>());

  std::stop_source source;
  source.request_stop();
  Metadata meta;
  std::error_code pull_err;
  auto fds = reg.pull(source.get_token(), "cancelled.subject", meta, pull_err);
  EXPECT_FALSE(fds);
  EXPECT_EQ(pull_err, make_nats_error(NATS_TIMEOUT));
}

TEST(RegistryTest, PullIncludesDependencies) {
  TestRegistry reg(std::make_unique<MockKeyValue>(),
                   std::make_unique<MockKeyValue>());

  auto push_err = reg.push({}, "events.any", google::protobuf::Any{});
  ASSERT_FALSE(push_err) << push_err.message();

  Metadata meta;
  std::error_code pull_err;
  auto fds = reg.pull({}, "events.any", meta, pull_err);
  ASSERT_TRUE(fds) << pull_err.message();
  EXPECT_GT(fds->file_size(), 0);

  bool found = false;
  for (const auto &f : fds->file()) {
    if (f.name() == "google/protobuf/any.proto") {
      found = true;
      break;
    }
  }
  EXPECT_TRUE(found);
}

TEST(RegistryTest, PullIncludesTransitiveGoogleImports) {
  TestRegistry reg(std::make_unique<MockKeyValue>(),
                   std::make_unique<MockKeyValue>());

  google::protobuf::Type type;
  auto push_err = reg.push({}, "events.type", type);
  ASSERT_FALSE(push_err) << push_err.message();

  Metadata meta;
  std::error_code pull_err;
  auto fds = reg.pull({}, "events.type", meta, pull_err);
  ASSERT_TRUE(fds) << pull_err.message();
  EXPECT_GT(fds->file_size(), 0);

  const std::vector<std::string> required = {
      "google/protobuf/type.proto",
      "google/protobuf/any.proto",
      "google/protobuf/source_context.proto",
  };
  for (const auto &name : required) {
    bool found = false;
    for (const auto &f : fds->file()) {
      if (f.name() == name) {
        found = true;
        break;
      }
    }
    EXPECT_TRUE(found) << "expected " << name << " in descriptor set";
  }
}
