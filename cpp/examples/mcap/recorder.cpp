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
#include "schemaregistry/nats/message.hpp"
#include "schemaregistry/nats_registry.hpp"
#include "sensor.pb.h"

#include <mcap/writer.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <format>
#include <fstream>
#include <iostream>
#include <mutex>
#include <queue>
#include <string>

#define LOG_INFO(...) std::cout << std::format(__VA_ARGS__) << std::flush
#define LOG_ERROR(...) std::cerr << std::format(__VA_ARGS__) << std::flush

namespace {

std::atomic<bool> g_running{true};

void signal_handler(int) { g_running = false; }

void print_usage(std::string_view program) {
  std::cerr
      << "usage: " << program << " [options]\n"
      << "options:\n"
      << "  --url <url>               NATS URL (default: "
         "nats://localhost:4444)\n"
      << "  --subject <subject>       NATS subject (default: sensor.readings)\n"
      << "  --metadata-bucket <name>  metadata KV bucket (default: metadata)\n"
      << "  --schema-bucket <name>    schema KV bucket (default: schemas)\n"
      << "  --output <path>           output MCAP file (default: sensor.mcap)\n"
      << "  -h, --help                show this help message\n";
}

struct Options {
  std::string url = "nats://localhost:4444";
  std::string subject = "sensor.readings";
  std::string metadata_bucket = "metadata";
  std::string schema_bucket = "schemas";
  std::string output = "sensor.mcap";
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
    } else if (arg == "--output" && i + 1 < argc) {
      opts.output = argv[++i];
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

// Thread-safe queue that takes ownership of messages delivered by the
// asynchronous subscription callback. The main loop consumes from this queue
// and writes messages to the MCAP file.
class MsgQueue {
public:
  void push(schemaregistry::nats::Message *msg) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      queue_.push(msg);
    }
    cv_.notify_one();
  }

  // Returns nullptr immediately if the queue is empty. The caller takes
  // ownership of the returned Message.
  [[nodiscard]] std::unique_ptr<schemaregistry::nats::Message> try_pop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) {
      return nullptr;
    }
    auto msg = std::unique_ptr<schemaregistry::nats::Message>(queue_.front());
    queue_.pop();
    return msg;
  }

  // Wait up to timeout_ms for a message. Returns nullptr on timeout.
  // The caller takes ownership of the returned Message.
  [[nodiscard]] std::unique_ptr<schemaregistry::nats::Message>
  pop(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    bool has_msg =
        cv_.wait_for(lock, timeout, [this] { return !queue_.empty(); });
    if (!has_msg) {
      return nullptr;
    }
    auto msg = std::unique_ptr<schemaregistry::nats::Message>(queue_.front());
    queue_.pop();
    return msg;
  }

private:
  std::queue<schemaregistry::nats::Message *> queue_;
  std::mutex mutex_;
  std::condition_variable cv_;
};

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

  MsgQueue msg_queue;

  std::error_code sub_err;
  auto sub = conn->subscribe(
      opts.subject,
      [&msg_queue](std::unique_ptr<schemaregistry::nats::Message> msg) {
        msg_queue.push(msg.release());
      },
      sub_err);
  if (sub_err) {
    LOG_ERROR("subscribe: {}\n", sub_err.message());
    return EXIT_FAILURE;
  }
  LOG_INFO("recorder subscribed to \"{}\", waiting for schema...\n",
           opts.subject);

  // Pull blocks until the producer registers the schema.
  schemaregistry::Metadata meta;
  std::error_code pull_err;
  auto fds = reg->pull({}, opts.subject, meta, pull_err);
  if (pull_err) {
    LOG_ERROR("pull schema: {}\n", pull_err.message());
    return EXIT_FAILURE;
  }

  LOG_INFO("resolved schema {} ({})\n", meta.schema.full_name,
           meta.schema.path);

  // Open the MCAP file and register the schema/channel.
  std::ofstream out(opts.output, std::ios::binary);
  if (!out) {
    LOG_ERROR("create output file: {}\n", opts.output);
    return EXIT_FAILURE;
  }

  mcap::McapWriter writer;
  mcap::McapWriterOptions options("protobuf");
  options.compression = mcap::Compression::Zstd;
  writer.open(out, options);

  std::string schema_data;
  if (!fds->SerializeToString(&schema_data)) {
    LOG_ERROR("marshal schema\n");
    writer.close();
    return EXIT_FAILURE;
  }

  mcap::Schema schema;
  schema.id = 1;
  schema.name = meta.schema.full_name;
  schema.encoding = "protobuf";
  schema.data =
      mcap::ByteArray(reinterpret_cast<const std::byte *>(schema_data.data()),
                      reinterpret_cast<const std::byte *>(schema_data.data() +
                                                          schema_data.size()));
  writer.addSchema(schema);

  mcap::Channel channel;
  channel.id = 0;
  channel.schemaId = schema.id;
  channel.topic = opts.subject;
  channel.messageEncoding = "protobuf";
  writer.addChannel(channel);

  // Drain any messages that arrived while pull was blocked so they are
  // recorded with monotonically increasing sequence numbers.
  std::uint32_t received = 0;
  while (g_running) {
    auto msg = msg_queue.pop(std::chrono::milliseconds(100));
    if (msg == nullptr) {
      continue;
    }

    ++received;
    const auto now = std::chrono::system_clock::now();
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        now.time_since_epoch())
                        .count();

    mcap::Message mcap_msg;
    mcap_msg.channelId = channel.id;
    mcap_msg.sequence = received;
    mcap_msg.logTime = static_cast<mcap::Timestamp>(ns);
    mcap_msg.publishTime = mcap_msg.logTime;

    const auto payload = msg->data();
    if (!payload.empty()) {
      mcap_msg.data = payload.data();
      mcap_msg.dataSize = static_cast<std::uint64_t>(payload.size());
    }

    auto status = writer.write(mcap_msg);
    if (!status.ok()) {
      LOG_ERROR("mcap write: {}\n", status.message);
      break;
    }

    LOG_INFO("recorded seq={}\n", received);
  }

  writer.close();
  LOG_INFO("shutdown signal received, wrote {} messages\n", received);

  return EXIT_SUCCESS;
}
