// Copyright (c) 2026 nats-proto-schema-registry contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
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
#include <iostream>
#include <stop_token>
#include <string>

int main() {
    natsStatus s = NATS_OK;
    natsConnection* conn = nullptr;
    jsCtx* js = nullptr;

    s = natsConnection_ConnectTo(&conn, "nats://localhost:4444");
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
    auto reg = schemaregistry::create_registry(js, "metadata", "schemas", ec);
    if (ec) {
        std::cerr << "registry: " << ec.message() << std::endl;
        jsCtx_Destroy(js);
        natsConnection_Destroy(conn);
        return EXIT_FAILURE;
    }

    const std::string subject = "orders.created";
    auto result = reg->pull({}, subject, ec);
    if (ec) {
        std::cerr << "pull: " << ec.message() << std::endl;
        jsCtx_Destroy(js);
        natsConnection_Destroy(conn);
        return EXIT_FAILURE;
    }
    if (!result) {
        std::cerr << "pull: no result" << std::endl;
        jsCtx_Destroy(js);
        natsConnection_Destroy(conn);
        return EXIT_FAILURE;
    }

    std::cout << "subject: " << subject << std::endl;
    std::cout << "schema path: " << result->meta.schema.path << std::endl;
    std::cout << "schema full name: " << result->meta.schema.full_name << std::endl;
    std::cout << "files in descriptor set: " << result->fds.file_size() << std::endl;

    jsCtx_Destroy(js);
    natsConnection_Destroy(conn);
    return EXIT_SUCCESS;
}

