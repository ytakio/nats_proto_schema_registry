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
#include "sensor.pb.h"

#include <google/protobuf/type.pb.h>

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <format>
#include <iostream>
#include <stop_token>
#include <string>
#include <thread>

#define LOG_INFO(...) std::cout << std::format(__VA_ARGS__) << std::flush
#define LOG_ERROR(...) std::cerr << std::format(__VA_ARGS__) << std::flush

namespace {

std::atomic<bool> g_running{true};

void signal_handler(int) { g_running = false; }

void print_usage(std::string_view program) {
  std::cerr << "usage: " << program << " [options]\n"
            << "options:\n"
            << "  --url <url>               NATS URL (default: nats://localhost:4444)\n"
            << "  --subject <subject>       NATS subject (default: sensor.readings)\n"
            << "  --metadata-bucket <name>  metadata KV bucket (default: metadata)\n"
            << "  --schema-bucket <name>    schema KV bucket (default: schemas)\n"
            << "  -h, --help                show this help message\n";
}

struct Options {
  std::string url = "nats://localhost:4444";
  std::string subject = "sensor.readings";
  std::string metadata_bucket = "metadata";
  std::string schema_bucket = "schemas";
};

[[nodiscard]] bool parse_args(int argc, char **argv, Options &opts) {
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--url" && i + 1 < argc) {
      opts.url = argv[++i];
    } else if (arg == "--subject" && i + 1 < argc) {
      opts.subject = argv[++i];
    } else if (arg == "--metadata-bucket" && i + 1 < argc) {
      opts.metadata_bucket = argv[++i];
    } else if (arg == "--schema-bucket" && i + 1 < argc) {
      opts.schema_bucket = argv[++i];
    } else if (arg == "-h" || arg == "--help") {
      print_usage(argv[0]);
      std::exit(EXIT_SUCCESS);
    } else {
      std::cerr << "unknown argument: " << arg << std::endl;
      print_usage(argv[0]);
      return false;
    }
  }
  return true;
}

} // namespace

int main(int argc, char **argv) {
  Options opts;
  if (!parse_args(argc, argv, opts)) {
    return EXIT_FAILURE;
  }

  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  std::error_code conn_err;
  auto conn = schemaregistry::nats::Connection::connect(opts.url, conn_err);
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
  auto reg = schemaregistry::create_registry(*js, opts.metadata_bucket,
                                             opts.schema_bucket, reg_err);
  if (reg_err) {
    LOG_ERROR("registry: {}\n", reg_err.message());
    return EXIT_FAILURE;
  }

  // Register the schema for the subject. This unblocks the recorder which
  // is waiting for the schema via pull().
  sensorpb::SensorReading placeholder;
  auto push_err = reg->push({}, opts.subject, placeholder);
  if (push_err) {
    LOG_ERROR("push: {}\n", push_err.message());
    return EXIT_FAILURE;
  }
  LOG_INFO("registered schema for subject \"{}\"\n", opts.subject);

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
      LOG_ERROR("marshal failed\n");
      break;
    }

    auto pub_err = conn->publish(opts.subject, data);
    if (pub_err) {
      LOG_ERROR("publish: {}\n", pub_err.message());
      break;
    }
    LOG_INFO("published seq={}\n", seq);

    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  LOG_INFO("producer shutting down after {} messages\n", seq);

  return EXIT_SUCCESS;
}
