"""Example: record SensorReading messages to an MCAP file."""

from __future__ import annotations

import argparse
import asyncio
import contextlib
import signal
import sys
import time
from pathlib import Path

import nats
from mcap.well_known import MessageEncoding
from mcap.writer import CompressionType
from mcap.writer import Writer as McapWriter

sys.path.insert(0, str(Path(__file__).parent.parent))

from nats_proto_schema_registry import create_registry


async def main() -> None:
    parser = argparse.ArgumentParser(
        description="Record SensorReading messages to an MCAP file.",
    )
    parser.add_argument(
        "--url",
        default="nats://localhost:4444",
        help="NATS server URL",
    )
    parser.add_argument(
        "--subject",
        default="sensor.readings",
        help="NATS subject to record",
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
    parser.add_argument(
        "--output",
        default="sensor.mcap",
        help="output MCAP file",
    )
    args = parser.parse_args()

    nc = await nats.connect(args.url, allow_reconnect=False)
    try:
        js = nc.jetstream()
        reg = await create_registry(js, args.metadata_bucket, args.schema_bucket)

        # Subscribe before waiting for the schema so messages published while we
        # are waiting are buffered by the NATS client.
        sub = await nc.subscribe(args.subject)
        print(f"recorder subscribed to {args.subject!r}, waiting for schema...")

        # Pull blocks until the producer registers the schema.
        fds, meta = await reg.pull(args.subject, timeout=120.0)
        print(f"resolved schema {meta.schema.full_name} ({meta.schema.path})")

        # Open the MCAP file and register the schema/channel.
        with open(args.output, "wb") as f:
            writer = McapWriter(
                f,
                compression=CompressionType.ZSTD,
            )
            writer.start()

            schema_id = writer.register_schema(
                name=meta.schema.full_name,
                encoding="protobuf",
                data=fds.SerializeToString(),
            )
            channel_ids: dict[str, int] = {}

            # Record messages until a signal requests shutdown or no message
            # arrives for a short grace period.
            stop_event = asyncio.Event()
            loop = asyncio.get_running_loop()
            for sig in (signal.SIGINT, signal.SIGTERM):
                loop.add_signal_handler(sig, stop_event.set)

            idle_timeout = 3.0
            received = 0
            try:
                while not stop_event.is_set():
                    try:
                        msg = await sub.next_msg(timeout=idle_timeout)
                    except TimeoutError:
                        if stop_event.is_set():
                            print(f"shutdown signal received, wrote {received} messages")
                            break
                        print(f"idle timeout, wrote {received} messages")
                        break
                    except nats.errors.ConnectionClosedError:
                        print(f"connection closed, wrote {received} messages")
                        break
                    except nats.errors.UnexpectedEOF:
                        print(f"connection closed, wrote {received} messages")
                        break

                    received += 1

                    # log_time is the time at which the recorder received and
                    # recorded the message. publish_time falls back to log_time
                    # because this example does not depend on the generated
                    # protobuf message type.
                    log_time = time.time_ns()
                    publish_time = log_time

                    subject = msg.subject
                    if subject not in channel_ids:
                        channel_ids[subject] = writer.register_channel(
                            topic=subject,
                            message_encoding=MessageEncoding.Protobuf,
                            schema_id=schema_id,
                        )

                    writer.add_message(
                        channel_id=channel_ids[subject],
                        log_time=log_time,
                        publish_time=publish_time,
                        sequence=received,
                        data=msg.data,
                    )
                    print(f"recorded seq={received} channel={channel_ids[subject]} topic={subject}")
            finally:
                for sig in (signal.SIGINT, signal.SIGTERM):
                    loop.remove_signal_handler(sig)

            writer.finish()

        print(f"wrote {args.output}")
    finally:
        with contextlib.suppress(Exception):
            await nc.drain()
        await nc.close()


if __name__ == "__main__":
    asyncio.run(main())
