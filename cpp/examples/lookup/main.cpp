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

#include <cstdlib>
#include <format>
#include <iostream>
#include <stop_token>
#include <string>

int main() {
    natsStatus s = NATS_OK;
    natsConnection* conn = nullptr;
    jsCtx* js = nullptr;

    s = natsConnection_ConnectTo(&conn, "nats://localhost:4444");
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

    auto [reg, reg_err] = schemaregistry::create_registry(js, "metadata", "schemas");
    if (reg_err) {
        std::cerr << std::format("registry: {}\n", reg_err.message());
        jsCtx_Destroy(js);
        natsConnection_Destroy(conn);
        return EXIT_FAILURE;
    }

    const std::string subject = "orders.created";
    auto [result, pull_err] = reg->pull({}, subject);
    if (pull_err) {
        std::cerr << std::format("pull: {}\n", pull_err.message());
        jsCtx_Destroy(js);
        natsConnection_Destroy(conn);
        return EXIT_FAILURE;
    }

    std::cout << std::format("subject: {}\n", subject);
    std::cout << std::format("schema path: {}\n", result.meta.schema.path);
    std::cout << std::format("schema full name: {}\n", result.meta.schema.full_name);
    std::cout << std::format("files in descriptor set: {}\n", result.fds.file_size());

    jsCtx_Destroy(js);
    natsConnection_Destroy(conn);
    return EXIT_SUCCESS;
}

