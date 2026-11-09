# C++ Schema Registry

C++20 header-only implementation of the NATS protobuf schema registry.

## Features

- Register a protobuf message type for a NATS subject.
- Automatically build a `FileDescriptorSet` from a generated protobuf message,
  including transitive imports.
- Store schema metadata per NATS subject in a NATS KV bucket.
- Store `FileDescriptorSet` blobs per `.proto` path in a NATS KV bucket.
- Look up the schema for a subject at runtime.

## Requirements

- C++20 compiler
- CMake 3.25+
- Protobuf compiler (for examples)

## Installation

This is a header-only library. Add `cpp/include` to your include path and link
against the dependencies listed below.

### Dependencies

Core library (always required):

- protobuf
- nats.c
- glaze

Optional (resolved only by the target that needs them):

- googletest (tests only)
- mcap, lz4, zstd (mcap example only)

The CMake build fetches dependencies automatically with `FetchContent` when
system packages are not found.

## Using the library in your own project

Link against the exported `nats_proto_schema_registry::schemaregistry` target.

### Option 1: add the source tree directly with `add_subdirectory`

Add the `cpp/` directory so that transitive dependencies are resolved for you:

```cmake
cmake_minimum_required(VERSION 3.25)
project(my_app LANGUAGES CXX)

add_subdirectory(/path/to/nats_proto_schema_registry/cpp)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE nats_proto_schema_registry::schemaregistry)
```

### Option 2: fetch the source with `FetchContent`

```cmake
cmake_minimum_required(VERSION 3.25)
project(my_app LANGUAGES CXX)

include(FetchContent)
FetchContent_Declare(
    nats_proto_schema_registry
    GIT_REPOSITORY https://github.com/nats-io/nats_proto_schema_registry.git
    GIT_TAG main
)
FetchContent_MakeAvailable(nats_proto_schema_registry)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE nats_proto_schema_registry::schemaregistry)
```

## Usage

Connect to NATS, create a `Registry`, and push or pull schemas.

### Register a schema

```cpp
#include "schemaregistry/registry.hpp"
#include <google/protobuf/wrappers.pb.h>

natsConnection* conn = nullptr;
natsConnection_ConnectTo(&conn, "nats://localhost:4444");

jsCtx* js = nullptr;
natsConnection_JetStream(&js, conn, nullptr);

auto reg = schemaregistry::create_registry(js, "metadata", "schemas");
google::protobuf::StringValue msg;
msg.set_value("order-123");
reg->push({}, "orders.created", msg);
```

### Look up a schema

```cpp
auto [fds, meta] = reg->pull({}, "orders.created");
std::cout << meta.schema.path << std::endl;
// google/protobuf/wrappers.proto
std::cout << meta.schema.full_name << std::endl;
// google.protobuf.StringValue
std::cout << fds.file_size() << std::endl; // count of files
                                           // in descriptor set
```

## API

The main entry point is the `Registry` interface in `registry.hpp`:

- `push(stop_token, subject, msg)` - register a generated protobuf message.
- `pull(stop_token, subject)` - retrieve the `FileDescriptorSet` and
  `Metadata`. `pull` watches the metadata and schema buckets, so it blocks until
  the requested subject is registered or cancellation is requested.

Cancellation is expressed with `std::stop_token`. For timeouts, create a
`std::stop_source` and request stop after the desired duration.

## Bucket configuration

`create_registry` creates the metadata and schema buckets with default settings
if they do not exist. If you need fine-grained control over bucket configuration
(TTL, history, storage type, etc.), create the buckets yourself with
`js_CreateKeyValue` before calling `create_registry`. `create_registry` uses
existing buckets when they are already present.

## Development

```bash
cd cpp
make build       # cmake configure + build
make test        # build and run tests
make test-race   # build and run tests with ThreadSanitizer
make check       # lint + race tests (lint is a no-op for C++)
make proto       # regenerate the protobuf code used by the mcap example
make up          # start NATS with JetStream (docker compose)
make run-mcap-example
```

## Storage layout

- Metadata bucket (configured by caller):
  - Key: NATS subject (e.g. `orders.created`)
  - Value: JSON `{"schema":{"path":"...","full_name":"..."}}`
- Schema bucket (configured by caller):
  - Key: `.proto` file path (e.g. `google/protobuf/wrappers.proto`)
  - Value: serialized `google.protobuf.FileDescriptorSet`

## Examples

The examples are standalone CMake projects that consume the library directly
from the source tree with `add_subdirectory`. They can also be built as part of
the main project by configuring with `-DSCHEMAREGISTRY_BUILD_EXAMPLES=ON`.

```bash
cd cpp/examples/register
cmake -B build
cmake --build build
```

The same works for `cpp/examples/lookup` and `cpp/examples/mcap`.

- [examples/register](examples/register) - register a schema for a subject.
- [examples/lookup](examples/lookup) - look a schema up, waiting for it.
- [examples/mcap](examples/mcap) - producer and receiver that records
  messages to an MCAP file, resolving the schema through the lookup.
