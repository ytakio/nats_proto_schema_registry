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

#include "schemaregistry/registry.hpp"

#include <google/protobuf/wrappers.pb.h>

#include <cstdlib>
#include <format>
#include <iostream>
#include <stop_token>
#include <string>

#define LOG_INFO(...) std::cout << std::format(__VA_ARGS__) << std::flush
#define LOG_ERROR(...) std::cerr << std::format(__VA_ARGS__) << std::flush

int main() {
  natsStatus s = NATS_OK;
  natsConnection *conn = nullptr;
  jsCtx *js = nullptr;

  s = natsConnection_ConnectTo(&conn, "nats://localhost:4444");
  if (s != NATS_OK) {
    LOG_ERROR("connect: {}\n", natsStatus_GetText(s));
    return EXIT_FAILURE;
  }

  s = natsConnection_JetStream(&js, conn, nullptr);
  if (s != NATS_OK) {
    LOG_ERROR("jetstream: {}\n", natsStatus_GetText(s));
    natsConnection_Destroy(conn);
    return EXIT_FAILURE;
  }

  std::error_code reg_err;
  auto reg = schemaregistry::create_registry(js, "metadata", "schemas", reg_err);
  if (reg_err) {
    LOG_ERROR("registry: {}\n", reg_err.message());
    jsCtx_Destroy(js);
    natsConnection_Destroy(conn);
    return EXIT_FAILURE;
  }

  const std::string subject = "orders.created";
  google::protobuf::StringValue msg;
  msg.set_value("order-123");

  auto push_err = reg->push({}, subject, msg);
  if (push_err) {
    LOG_ERROR("push: {}\n", push_err.message());
    jsCtx_Destroy(js);
    natsConnection_Destroy(conn);
    return EXIT_FAILURE;
  }

  LOG_INFO("pushed schema for subject \"{}\"\n", subject);

  jsCtx_Destroy(js);
  natsConnection_Destroy(conn);
  return EXIT_SUCCESS;
}
