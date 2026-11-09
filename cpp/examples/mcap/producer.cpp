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
#include "sensor.pb.h"

#include <google/protobuf/type.pb.h>

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <stop_token>
#include <string>
#include <thread>

namespace {

std::atomic<bool> g_running{true};

void signal_handler(int) { g_running = false; }

} // namespace

int main(int argc, char **argv) {
  std::string url = "nats://localhost:4444";
  std::string subject = "sensor.readings";
  std::string metadata_bucket = "metadata";
  std::string schema_bucket = "schemas";

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--url" && i + 1 < argc) {
      url = argv[++i];
    } else if (arg == "--subject" && i + 1 < argc) {
      subject = argv[++i];
    } else if (arg == "--metadata-bucket" && i + 1 < argc) {
      metadata_bucket = argv[++i];
    } else if (arg == "--schema-bucket" && i + 1 < argc) {
      schema_bucket = argv[++i];
    }
  }

  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  natsStatus s = NATS_OK;
  natsConnection *conn = nullptr;
  jsCtx *js = nullptr;

  s = natsConnection_ConnectTo(&conn, url.c_str());
  if (s != NATS_OK) {
    std::cerr << "connect: " << natsStatus_GetText(s) << std::endl;
    return EXIT_FAILURE;
  }

  s = natsConnection_JetStream(&js, conn, nullptr);
  if (s != NATS_OK) {
    std::cerr << "jetstream: " << natsStatus_GetText(s) << std::endl;
    natsConnection_Destroy(conn);
    return EXIT_FAILURE;
  }

  std::error_code ec;
  auto reg = schemaregistry::create_registry(js, metadata_bucket, schema_bucket, ec);
  if (ec) {
    std::cerr << "registry: " << ec.message() << std::endl;
    jsCtx_Destroy(js);
    natsConnection_Destroy(conn);
    return EXIT_FAILURE;
  }

  // Register the schema for the subject. This unblocks the recorder which
  // is waiting for the schema via pull().
  sensorpb::SensorReading placeholder;
  ec = reg->push({}, subject, placeholder);
  if (ec) {
    std::cerr << "push: " << ec.message() << std::endl;
    jsCtx_Destroy(js);
    natsConnection_Destroy(conn);
    return EXIT_FAILURE;
  }
  std::cout << "registered schema for subject \"" << subject << "\""
            << std::endl;

  std::uint64_t seq = 0;
  while (g_running) {
    ++seq;
    const auto now = std::chrono::system_clock::now();
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        now.time_since_epoch())
                        .count();

    sensorpb::SensorReading msg;
    msg.set_sequence(seq);
    msg.set_temperature(20.0 + static_cast<double>(seq) * 0.1);
    msg.set_humidity(50.0 + static_cast<double>(seq) * 0.2);
    msg.set_pressure(1013.0 + static_cast<double>(seq) * 0.05);
    msg.set_timestamp_ns(ns);

    google::protobuf::Type *device_type = msg.mutable_device_type();
    device_type->set_name("sensorpb.SensorReading");
    device_type->set_syntax(google::protobuf::SYNTAX_PROTO3);

    std::string data;
    if (!msg.SerializeToString(&data)) {
      std::cerr << "marshal failed" << std::endl;
      break;
    }

    s = natsConnection_Publish(conn, subject.c_str(), data.data(),
                               static_cast<int>(data.size()));
    if (s != NATS_OK) {
      std::cerr << "publish: " << natsStatus_GetText(s) << std::endl;
      break;
    }
    std::cout << "published seq=" << seq << std::endl;

    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  std::cout << "producer shutting down after " << seq << " messages"
            << std::endl;

  jsCtx_Destroy(js);
  natsConnection_Destroy(conn);
  return EXIT_SUCCESS;
}
