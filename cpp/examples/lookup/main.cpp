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

#include "schemaregistry/nats/connection.hpp"
#include "schemaregistry/nats_registry.hpp"

#include <cstdlib>
#include <format>
#include <iostream>
#include <stop_token>
#include <string>

#define LOG_INFO(...) std::cout << std::format(__VA_ARGS__) << std::flush
#define LOG_ERROR(...) std::cerr << std::format(__VA_ARGS__) << std::flush

int main() {
  std::error_code conn_err;
  auto conn = schemaregistry::nats::Connection::connect(
      "nats://localhost:4444", conn_err);
  if (conn_err) {
    LOG_ERROR("connect: {}\n", conn_err.message());
    return EXIT_FAILURE;
  }

  std::error_code js_err;
  auto js = conn->jetstream(js_err);
  if (js_err) {
    LOG_ERROR("jetstream: {}\n", js_err.message());
    return EXIT_FAILURE;
  }

  std::error_code reg_err;
  auto reg = schemaregistry::create_registry(*js, "metadata", "schemas", reg_err);
  if (reg_err) {
    LOG_ERROR("registry: {}\n", reg_err.message());
    return EXIT_FAILURE;
  }

  const std::string subject = "orders.created";
  schemaregistry::Metadata meta;
  std::error_code pull_err;
  auto fds = reg->pull({}, subject, meta, pull_err);
  if (pull_err) {
    LOG_ERROR("pull: {}\n", pull_err.message());
    return EXIT_FAILURE;
  }

  LOG_INFO("subject: {}\n", subject);
  LOG_INFO("schema path: {}\n", meta.schema.path);
  LOG_INFO("schema full name: {}\n", meta.schema.full_name);
  LOG_INFO("files in descriptor set: {}\n", fds->file_size());

  return EXIT_SUCCESS;
}

