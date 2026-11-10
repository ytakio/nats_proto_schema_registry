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

// Thread-safe queue that takes ownership of natsMsg pointers delivered by the
// asynchronous subscription callback. The main loop consumes from this queue
// and writes messages to the MCAP file.
class MsgQueue {
public:
  void push(natsMsg *msg) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      queue_.push(msg);
    }
    cv_.notify_one();
  }

  // Returns nullptr immediately if the queue is empty or if shutdown is
  // requested. The caller takes ownership of the returned natsMsg.
  [[nodiscard]] natsMsg *try_pop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) {
      return nullptr;
    }
    natsMsg *msg = queue_.front();
    queue_.pop();
    return msg;
  }

  // Wait up to timeout_ms for a message. Returns nullptr on timeout or when
  // shutdown is requested. The caller takes ownership of the returned natsMsg.
  [[nodiscard]] natsMsg *pop(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    bool has_msg =
        cv_.wait_for(lock, timeout, [this] { return !queue_.empty(); });
    if (!has_msg) {
      return nullptr;
    }
    natsMsg *msg = queue_.front();
    queue_.pop();
    return msg;
  }

private:
  std::queue<natsMsg *> queue_;
  std::mutex mutex_;
  std::condition_variable cv_;
};

// NATS asynchronous message callback. The library invokes this on its own
// dispatcher thread, so ownership of the message is handed off to the queue
// for consumption by the main loop.
static void on_msg(natsConnection * /*nc*/, natsSubscription * /*sub*/,
                   natsMsg *msg, void *closure) {
  auto *queue = static_cast<MsgQueue *>(closure);
  queue->push(msg);
}

} // namespace

int main(int argc, char **argv) {
  Options opts;
  if (!parse_args(argc, argv, opts)) {
    return EXIT_FAILURE;
  }

  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  natsStatus s = NATS_OK;
  natsConnection *conn = nullptr;
  jsCtx *js = nullptr;

  s = natsConnection_ConnectTo(&conn, opts.url.c_str());
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
  auto reg = schemaregistry::create_registry(js, opts.metadata_bucket,
                                             opts.schema_bucket, reg_err);
  if (reg_err) {
    LOG_ERROR("registry: {}\n", reg_err.message());
    jsCtx_Destroy(js);
    natsConnection_Destroy(conn);
    return EXIT_FAILURE;
  }

  MsgQueue msg_queue;

  // Subscribe before waiting for the schema so messages published while we
  // are waiting are buffered by the NATS client and delivered to on_msg.
  natsSubscription *sub = nullptr;
  s = natsConnection_Subscribe(&sub, conn, opts.subject.c_str(), on_msg,
                               &msg_queue);
  if (s != NATS_OK) {
    LOG_ERROR("subscribe: {}\n", natsStatus_GetText(s));
    jsCtx_Destroy(js);
    natsConnection_Destroy(conn);
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
    natsSubscription_Destroy(sub);
    jsCtx_Destroy(js);
    natsConnection_Destroy(conn);
    return EXIT_FAILURE;
  }

  LOG_INFO("resolved schema {} ({})\n", meta.schema.full_name,
           meta.schema.path);

  // Open the MCAP file and register the schema/channel.
  std::ofstream out(opts.output, std::ios::binary);
  if (!out) {
    LOG_ERROR("create output file: {}\n", opts.output);
    natsSubscription_Destroy(sub);
    jsCtx_Destroy(js);
    natsConnection_Destroy(conn);
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
    natsSubscription_Destroy(sub);
    jsCtx_Destroy(js);
    natsConnection_Destroy(conn);
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
    natsMsg *msg = msg_queue.pop(std::chrono::milliseconds(100));
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

    const char *payload = natsMsg_GetData(msg);
    const int payload_len = natsMsg_GetDataLength(msg);
    if (payload && payload_len > 0) {
      mcap_msg.data = reinterpret_cast<const std::byte *>(payload);
      mcap_msg.dataSize = static_cast<std::uint64_t>(payload_len);
    }

    auto status = writer.write(mcap_msg);
    if (!status.ok()) {
      LOG_ERROR("mcap write: {}\n", status.message);
      natsMsg_Destroy(msg);
      break;
    }
    natsMsg_Destroy(msg);

    LOG_INFO("recorded seq={}\n", received);
  }

  writer.close();
  natsSubscription_Destroy(sub);
  LOG_INFO("shutdown signal received, wrote {} messages\n", received);

  jsCtx_Destroy(js);
  natsConnection_Destroy(conn);
  return EXIT_SUCCESS;
}
