# NATS Protobuf Schema Registry

A lightweight schema registry for arbitrary protobuf messages, backed by NATS
Key-Value stores.

## Overview

This repository provides utilities to register and look up protobuf schemas for
NATS subjects. It stores:

- **Metadata** per NATS subject: which `.proto` file and message type are used.
- **Schema blobs** per `.proto` file: a serialized
  `google.protobuf.FileDescriptorSet`, including transitive imports.

This lets publishers register the schema for a subject, and consumers retrieve
it at runtime to decode or reflect on messages without prior knowledge of the
message type.

## Storage Layout

### Metadata bucket (caller-configured)

- **Key**: NATS subject (e.g. `orders.created`)
- **Value**: JSON metadata

```json
{
  "schema": {
    "path": "google/protobuf/wrappers.proto",
    "full_name": "google.protobuf.StringValue"
  }
}
```

### Schema bucket (caller-configured)

- **Key**: `.proto` file path (e.g. `google/protobuf/wrappers.proto`)
- **Value**: serialized `google.protobuf.FileDescriptorSet`

## Implementations

- [Go](./go) - Go library with NATS KV backend.

## Specification

See [SPECIFICATION.md](./SPECIFICATION.md) for the detailed design.

