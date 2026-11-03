# Go Schema Registry

Go implementation of the NATS protobuf schema registry.

## Features

- Register a protobuf message type for a NATS subject.
- Automatically build a `FileDescriptorSet` from a generated protobuf message,
  including transitive imports.
- Store schema metadata per NATS subject in a NATS KV bucket.
- Store `FileDescriptorSet` blobs per `.proto` path in a NATS KV bucket.
- Look up the schema for a subject at runtime.

## Installation

```bash
go get github.com/ytakio/nats_proto_schema_registry/go
```

## Usage

Connect to NATS, create a `Registry`, and push or pull schemas.

### Register a schema

```go
package main

import (
    "context"
    "log"

    "github.com/nats-io/nats.go"
    schemaregistry "github.com/ytakio/nats_proto_schema_registry/go"
    "google.golang.org/protobuf/types/known/wrapperspb"
)

func main() {
    nc, err := nats.Connect(nats.DefaultURL)
    if err != nil {
        log.Fatal(err)
    }
    defer nc.Close()

    js, err := nc.JetStream()
    if err != nil {
        log.Fatal(err)
    }

    reg, err := schemaregistry.New(js, "metadata", "schemas")
    if err != nil {
        log.Fatal(err)
    }

    ctx := context.Background()

    if err := reg.Push(ctx, "orders.created", wrapperspb.String("order-123")); err != nil {
        log.Fatal(err)
    }
}
```

### Look up a schema

```go
ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
defer cancel()

fds, meta, err := reg.Pull(ctx, "orders.created")
if err != nil {
    log.Fatal(err)
}

fmt.Println(meta.Schema.Path)      // google/protobuf/wrappers.proto
fmt.Println(meta.Schema.FullName)  // google.protobuf.StringValue
fmt.Println(len(fds.File))         // number of files in descriptor set
```

## API

The main entry point is the `Registry` interface in `types.go`:

- `Push(ctx, subject, msg)` - register a generated protobuf message.
- `Pull(ctx, subject)` - retrieve the `FileDescriptorSet` and `Metadata`. `Pull`
  watches the metadata and schema buckets, so it blocks until the requested
  subject is registered or `ctx` is cancelled.

## Bucket configuration

`New` creates the metadata and schema buckets with default settings if they do
not exist. If you need fine-grained control over bucket configuration (TTL,
history, storage type, etc.), create the buckets yourself with
`js.CreateKeyValue` before calling `New`. `New` uses existing buckets when they
are already present.

## Development

```bash
make build       # go build ./...
make test        # go test ./...
make test-race   # go test -race ./...
make vet         # go vet ./...
make fmt         # go fmt ./...
make check       # fmt + vet + race tests
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

- [example/register](./example/register) - register a schema for a subject.
- [example/lookup](./example/lookup) - look a schema up, waiting for it.
- [example/mcap](./example/mcap) - producer and receiver that records messages
  to an MCAP file, resolving the schema through the lookup.

