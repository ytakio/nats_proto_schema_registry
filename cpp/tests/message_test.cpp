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

#include "schemaregistry/nats/message.hpp"

#include <gtest/gtest.h>

#include <string>
#include <system_error>

namespace schemaregistry::nats {

class MessageHeaderTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        // nats.c must be initialized before any message/header operations.
        ASSERT_EQ(nats_Open(0), NATS_OK);
    }

    static void TearDownTestSuite() { nats_Close(); }

    std::unique_ptr<Msg> make_message(const char* subject = "test.subject") {
        std::error_code ec;
        auto msg = Msg::create(subject, std::nullopt, std::nullopt, ec);
        EXPECT_FALSE(ec) << ec.message();
        EXPECT_NE(msg, nullptr);
        return msg;
    }
};

TEST_F(MessageHeaderTest, SetAndGetReturnsValue) {
    auto msg = make_message();

    auto ec = msg->header_set("Content-Type", std::string{"application/json"});
    ASSERT_FALSE(ec) << ec.message();

    std::error_code get_ec;
    auto value = msg->header_get("Content-Type", get_ec);
    ASSERT_FALSE(get_ec) << get_ec.message();
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, "application/json");
}

TEST_F(MessageHeaderTest, SetNulloptAccepted) {
    auto msg = make_message();

    auto ec = msg->header_set("X-Empty", std::nullopt);
    ASSERT_FALSE(ec) << ec.message();

    // nats.c accepts a NULL value pointer for header_set; the resulting
    // get may report either an empty value or a missing key depending on
    // the underlying implementation, so we only assert that the set
    // operation itself does not fail.
    SUCCEED();
}

TEST_F(MessageHeaderTest, AddAppendsValues) {
    auto msg = make_message();

    auto ec = msg->header_add("Accept", std::string{"text/plain"});
    ASSERT_FALSE(ec) << ec.message();
    ec = msg->header_add("Accept", std::string{"text/html"});
    ASSERT_FALSE(ec) << ec.message();

    std::error_code get_ec;
    auto value = msg->header_get("Accept", get_ec);
    ASSERT_FALSE(get_ec) << get_ec.message();
    ASSERT_TRUE(value.has_value());
    // natsMsgHeader_Get returns the first value for a multi-value header.
    EXPECT_EQ(*value, "text/plain");
}

TEST_F(MessageHeaderTest, GetMissingKeyReturnsError) {
    auto msg = make_message();

    std::error_code get_ec;
    auto value = msg->header_get("Missing-Header", get_ec);
    EXPECT_FALSE(value.has_value());
    EXPECT_TRUE(get_ec);
    EXPECT_EQ(get_ec.value(), NATS_NOT_FOUND);
}

TEST_F(MessageHeaderTest, DeleteExistingKey) {
    auto msg = make_message();

    auto ec = msg->header_set("X-Temp", std::string{"value"});
    ASSERT_FALSE(ec) << ec.message();

    ec = msg->header_delete("X-Temp");
    ASSERT_FALSE(ec) << ec.message();

    std::error_code get_ec;
    auto value = msg->header_get("X-Temp", get_ec);
    EXPECT_FALSE(value.has_value());
    EXPECT_TRUE(get_ec);
}

TEST_F(MessageHeaderTest, DeleteMissingKeyReturnsNotFound) {
    auto msg = make_message();

    auto ec = msg->header_delete("Missing-Header");
    EXPECT_TRUE(ec);
    EXPECT_EQ(ec.value(), NATS_NOT_FOUND);
}

}  // namespace schemaregistry::nats
