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

#pragma once

#if __has_include(<nats/nats.h>)
#include <nats/nats.h>
#else
#include <nats.h>
#endif

#include <string>
#include <system_error>

namespace schemaregistry {

// Error codes reused from NATS.c where possible.
//
// The numeric values intentionally match the natsStatus constants so that
// converting a natsStatus to std::error_code is a simple cast.
enum class errc : int {
    ok = NATS_OK,
    cancelled = NATS_TIMEOUT,
    invalid_argument = NATS_INVALID_ARG,
    not_found = NATS_NOT_FOUND,
    internal = NATS_ERR,
};

namespace detail {

class nats_error_category : public std::error_category {
public:
    [[nodiscard]] const char* name() const noexcept override { return "nats"; }

    [[nodiscard]] std::string message(int ev) const override {
        return natsStatus_GetText(static_cast<natsStatus>(ev));
    }

    [[nodiscard]] bool equivalent(
        const std::error_code& code,
        int condition
    ) const noexcept override {
        if (code.category() == *this) {
            return code.value() == condition;
        }
        return false;
    }
};

inline const std::error_category& nats_category() {
    static const nats_error_category instance;
    return instance;
}

} // namespace detail

inline std::error_code make_error_code(errc e) {
    return {static_cast<int>(e), detail::nats_category()};
}

inline std::error_code make_nats_error(natsStatus s) {
    if (s == NATS_OK) {
        return {};
    }
    return {static_cast<int>(s), detail::nats_category()};
}

} // namespace schemaregistry

namespace std {

template <>
struct is_error_code_enum<schemaregistry::errc> : true_type {};

} // namespace std

