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
#include <csignal>
#include <cstdlib>
#include <format>
#include <fstream>
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
  std::string output = "sensor.mcap";

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
    } else if (arg == "--output" && i + 1 < argc) {
      output = argv[++i];
    }
  }

  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  natsStatus s = NATS_OK;
  natsConnection *conn = nullptr;
  jsCtx *js = nullptr;

  s = natsConnection_ConnectTo(&conn, url.c_str());
  if (s != NATS_OK) {
    std::cerr << std::format("connect: {}\n", natsStatus_GetText(s));
    return EXIT_FAILURE;
  }

  s = natsConnection_JetStream(&js, conn, nullptr);
  if (s != NATS_OK) {
    std::cerr << std::format("jetstream: {}\n", natsStatus_GetText(s));
    natsConnection_Destroy(conn);
    return EXIT_FAILURE;
  }

  auto [reg, reg_err] = schemaregistry::create_registry(js, metadata_bucket, schema_bucket);
  if (reg_err) {
    std::cerr << std::format("registry: {}\n", reg_err.message());
    jsCtx_Destroy(js);
    natsConnection_Destroy(conn);
    return EXIT_FAILURE;
  }

  // Subscribe before waiting for the schema so messages published while we
  // are waiting are buffered by the NATS client.
  natsSubscription *sub = nullptr;
  s = natsConnection_Subscribe(&sub, conn, subject.c_str(), nullptr, nullptr);
  if (s != NATS_OK) {
    std::cerr << std::format("subscribe: {}\n", natsStatus_GetText(s));
    jsCtx_Destroy(js);
    natsConnection_Destroy(conn);
    return EXIT_FAILURE;
  }
  std::cout << std::format(
      "recorder subscribed to \"{}\", waiting for schema...\n", subject);

  // Pull blocks until the producer registers the schema.
  auto [result, pull_err] = reg->pull({}, subject);
  if (pull_err) {
    std::cerr << std::format("pull schema: {}\n", pull_err.message());
    natsSubscription_Destroy(sub);
    jsCtx_Destroy(js);
    natsConnection_Destroy(conn);
    return EXIT_FAILURE;
  }

  std::cout << std::format("resolved schema {} ({})\n",
                           result.meta.schema.full_name,
                           result.meta.schema.path);

  // Open the MCAP file and register the schema/channel.
  std::ofstream out(output, std::ios::binary);
  if (!out) {
    std::cerr << std::format("create output file: {}\n", output);
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
  if (!result.fds.SerializeToString(&schema_data)) {
    std::cerr << std::format("marshal schema\n");
    writer.close();
    natsSubscription_Destroy(sub);
    jsCtx_Destroy(js);
    natsConnection_Destroy(conn);
    return EXIT_FAILURE;
  }

  mcap::Schema schema;
  schema.id = 1;
  schema.name = result.meta.schema.full_name;
  schema.encoding = "protobuf";
  schema.data =
      mcap::ByteArray(reinterpret_cast<const std::byte *>(schema_data.data()),
                      reinterpret_cast<const std::byte *>(
                          schema_data.data() + schema_data.size()));
  writer.addSchema(schema);

  mcap::Channel channel;
  channel.id = 0;
  channel.schemaId = schema.id;
  channel.topic = subject;
  channel.messageEncoding = "protobuf";
  writer.addChannel(channel);

  std::uint32_t received = 0;
  while (g_running) {
    natsMsg *msg = nullptr;
    s = natsSubscription_NextMsg(&msg, sub, 1000);
    if (s == NATS_TIMEOUT) {
      continue;
    }
    if (s != NATS_OK) {
      std::cerr << std::format("next msg: {}\n", natsStatus_GetText(s));
      break;
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
      std::cerr << std::format("mcap write: {}\n", status.message);
      natsMsg_Destroy(msg);
      break;
    }
    natsMsg_Destroy(msg);

    std::cout << std::format("recorded seq={}\n", received);
  }

  writer.close();
  natsSubscription_Destroy(sub);
  std::cout <<
      std::format("shutdown signal received, wrote {} messages\n", received);

  jsCtx_Destroy(js);
  natsConnection_Destroy(conn);
  return EXIT_SUCCESS;
}
