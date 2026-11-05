# Python Schema Registry

Python implementation of the NATS protobuf schema registry.

## Features

- Register a protobuf message type for a NATS subject.
- Automatically build a `FileDescriptorSet` from a generated protobuf message,
  including transitive imports.
- Store schema metadata per NATS subject in a NATS KV bucket.
- Store `FileDescriptorSet` blobs per `.proto` path in a NATS KV bucket.
- Look up the schema for a subject at runtime.

## Installation

This project uses [uv](https://docs.astral.sh/uv/) and requires Python 3.14 or
later.

```bash
cd python
uv sync --extra dev
```

## Usage

Connect to NATS, create a `Registry`, and push or pull schemas.

### Register a schema

```python
import asyncio
import nats
from google.protobuf.wrappers_pb2 import StringValue
from nats_proto_schema_registry import create_registry


async def main():
    nc = await nats.connect("nats://localhost:4444")
    js = nc.jetstream()
    reg = await create_registry(js, "metadata", "schemas")

    await reg.push("orders.created", StringValue(value="order-123"))
    print("schema registered")

    await nc.close()


asyncio.run(main())
```

### Look up a schema

```python
import asyncio
import nats
from nats_proto_schema_registry import create_registry


async def main():
    nc = await nats.connect("nats://localhost:4444")
    js = nc.jetstream()
    reg = await create_registry(js, "metadata", "schemas")

    fds, meta = await reg.pull("orders.created")

    print(meta.schema.path)  # google/protobuf/wrappers.proto
    print(meta.schema.full_name)  # google.protobuf.StringValue
    print(len(fds.file))  # number of files in descriptor set

    await nc.close()


asyncio.run(main())
```

## API

The main entry point is the `Registry` protocol in `types.py`:

- `await push(subject, msg)` - register a generated protobuf message.
- `await pull(subject)` - retrieve the `FileDescriptorSet` and `Metadata`.
  `pull` watches the metadata and schema buckets, so it blocks until the
  requested subject is registered or the operation is cancelled.

Use `create_registry(js, metadata_bucket, schema_bucket)` to build a registry
backed by NATS KV. If the buckets do not exist they are created with sensible
defaults.

## Development

```bash
uv run pytest          # run tests
uv run ruff check .    # lint
uv run ruff format .   # format
uv run mypy src        # type check
```

## Storage layout

- Metadata bucket (configured by caller):
  - Key: NATS subject (e.g. `orders.created`)
  - Value: JSON `{"schema":{"path":"...","full_name":"..."}}`
- Schema bucket (configured by caller):
  - Key: `.proto` file path (e.g. `google/protobuf/wrappers.proto`)
  - Value: serialized `google.protobuf.FileDescriptorSet`

## Examples

- [examples/register.py](./examples/register.py) - register a schema for a subject.
- [examples/lookup.py](./examples/lookup.py) - look a schema up, waiting for it.
- [examples/mcap](./examples/mcap) - producer and recorder that writes messages
  to an MCAP file, resolving the schema through the registry.

Run the MCAP example with:

```bash
make run-mcap-example
```

