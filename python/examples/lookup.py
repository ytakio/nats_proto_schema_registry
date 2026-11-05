"""Example: look up a protobuf schema for a NATS subject."""

from __future__ import annotations

import asyncio

import nats

from nats_proto_schema_registry import create_registry


async def main() -> None:
    nc = await nats.connect("nats://localhost:4444")
    try:
        js = nc.jetstream()
        reg = await create_registry(js, "metadata", "schemas")

        subject = "orders.created"
        fds, meta = await reg.pull(subject)

        print(f"subject: {subject}")
        print(f"schema path: {meta.schema.path}")
        print(f"schema full name: {meta.schema.full_name}")
        print(f"files in descriptor set: {len(fds.file)}")
    finally:
        await nc.close()


if __name__ == "__main__":
    asyncio.run(main())
