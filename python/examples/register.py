"""Example: register a protobuf schema for a NATS subject."""

from __future__ import annotations

import asyncio

import nats
from google.protobuf.wrappers_pb2 import StringValue

from nats_proto_schema_registry import create_registry


async def main() -> None:
    nc = await nats.connect("nats://localhost:4444")
    try:
        js = nc.jetstream()
        reg = await create_registry(js, "metadata", "schemas")

        subject = "orders.created"
        msg = StringValue(value="order-123")

        await reg.push(subject, msg)
        print(f"pushed schema for subject {subject!r}")
    finally:
        await nc.close()


if __name__ == "__main__":
    asyncio.run(main())
