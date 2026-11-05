"""Example: publish SensorReading messages and register the schema."""

from __future__ import annotations

import argparse
import asyncio
import contextlib
import signal
import sys
from pathlib import Path

import nats

sys.path.insert(0, str(Path(__file__).parent.parent))

from google.protobuf import descriptor_pool
from google.protobuf.type_pb2 import Type
from sensorpb.sensor_pb2 import SensorReading

from nats_proto_schema_registry import create_registry

_SYNTAX_PROTO3 = (
    descriptor_pool.Default()
    .FindEnumTypeByName("google.protobuf.Syntax")
    .values_by_name["SYNTAX_PROTO3"]
    .number
)


async def main() -> None:
    parser = argparse.ArgumentParser(
        description="Publish SensorReading messages and register the schema.",
    )
    parser.add_argument(
        "--url",
        default="nats://localhost:4444",
        help="NATS server URL",
    )
    parser.add_argument(
        "--subject",
        default="sensor.readings",
        help="NATS subject to publish on",
    )
    parser.add_argument(
        "--metadata-bucket",
        default="metadata",
        help="metadata KV bucket name",
    )
    parser.add_argument(
        "--schema-bucket",
        default="schemas",
        help="schema KV bucket name",
    )
    args = parser.parse_args()

    nc = await nats.connect(args.url, allow_reconnect=False)
    try:
        js = nc.jetstream()
        reg = await create_registry(js, args.metadata_bucket, args.schema_bucket)

        # Register the schema for the subject. This unblocks the recorder which
        # is waiting for the schema via pull().
        await reg.push(args.subject, SensorReading())
        print(f"registered schema for subject {args.subject!r}")

        # Publish one reading per second until a signal requests shutdown.
        stop_event = asyncio.Event()
        loop = asyncio.get_running_loop()
        for sig in (signal.SIGINT, signal.SIGTERM):
            loop.add_signal_handler(sig, stop_event.set)

        seq = 0
        try:
            while not stop_event.is_set():
                now = asyncio.get_event_loop().time()
                seq += 1
                msg = SensorReading(
                    sequence=seq,
                    temperature=20.0 + (seq - 1) * 0.1,
                    humidity=50.0 + (seq - 1) * 0.2,
                    pressure=1013.0 + (seq - 1) * 0.05,
                    timestamp_ns=int(now * 1e9),
                    device_type=Type(
                        name="sensorpb.SensorReading",
                        syntax=_SYNTAX_PROTO3,
                    ),
                )

                await nc.publish(args.subject, msg.SerializeToString())
                print(f"published seq={msg.sequence}")

                with contextlib.suppress(TimeoutError):
                    await asyncio.wait_for(stop_event.wait(), timeout=1.0)
        finally:
            for sig in (signal.SIGINT, signal.SIGTERM):
                loop.remove_signal_handler(sig)

        print(f"producer shutting down after {seq} messages")
    finally:
        await nc.close()


if __name__ == "__main__":
    asyncio.run(main())
