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

#pragma once

#include <string>
#include <system_error>

namespace schemaregistry {

// Registry-level error codes. These are independent of the underlying NATS
// transport so that callers of Registry do not depend on nats/ error types.
enum class registry_errc {
  success = 0,
  invalid_argument,
  cancelled,
  serialization_failed,
  parse_failed,
  internal_error,
};

// Convert a registry-level error code to std::error_code. The implementation
// mirrors nats::make_nats_error: a single function-local error_category is
// used and the public API is just this thin wrapper.
inline std::error_code make_error_code(registry_errc c) {
  static const struct : std::error_category {
    const char *name() const noexcept override { return "schemaregistry"; }

    std::string message(int ev) const override {
      switch (static_cast<registry_errc>(ev)) {
      case registry_errc::success:
        return "success";
      case registry_errc::invalid_argument:
        return "invalid argument";
      case registry_errc::cancelled:
        return "cancelled";
      case registry_errc::serialization_failed:
        return "serialization failed";
      case registry_errc::parse_failed:
        return "parse failed";
      case registry_errc::internal_error:
        return "internal error";
      }
      return "unknown registry error";
    }
  } category;

  if (c == registry_errc::success) {
    return {};
  }
  return {static_cast<int>(c), category};
}

} // namespace schemaregistry
