"""NATS Key-Value backed implementation of the schema registry."""

from __future__ import annotations

import asyncio
from typing import cast

from google.protobuf.descriptor_pb2 import FileDescriptorSet
from google.protobuf.message import Message
from nats.js import JetStreamContext, kv
from nats.js.api import KeyValueConfig
from nats.js.errors import BucketNotFoundError

from nats_proto_schema_registry.descriptor import build_file_descriptor_set
from nats_proto_schema_registry.types import Metadata, SchemaRef


async def create_registry(
    js: JetStreamContext,
    metadata_bucket: str,
    schema_bucket: str,
) -> NatsRegistry:
    """Create a Registry backed by NATS KV buckets.

    If the buckets do not exist they are created with sensible defaults. The
    caller retains ownership of ``js`` and is responsible for closing the
    underlying NATS connection.
    """
    if js is None:
        raise ValueError("schemaregistry: JetStreamContext is required")
    if not metadata_bucket:
        raise ValueError("schemaregistry: metadata bucket name is required")
    if not schema_bucket:
        raise ValueError("schemaregistry: schema bucket name is required")

    metadata_kv = await _get_or_create_kv(js, metadata_bucket)
    schema_kv = await _get_or_create_kv(js, schema_bucket)

    return NatsRegistry(metadata_kv, schema_kv)


async def _get_or_create_kv(js: JetStreamContext, bucket: str) -> kv.KeyValue:
    try:
        return await js.key_value(bucket)
    except BucketNotFoundError:
        return await js.create_key_value(config=KeyValueConfig(bucket=bucket))


class NatsRegistry:
    """Registry implementation backed by NATS Key-Value stores."""

    def __init__(
        self,
        metadata_kv: kv.KeyValue,
        schema_kv: kv.KeyValue,
    ) -> None:
        self._metadata_kv = metadata_kv
        self._schema_kv = schema_kv

    async def push(
        self,
        subject: str,
        msg: Message,
        *,
        timeout: float | None = None,
    ) -> None:
        """Register the protobuf message type of ``msg`` for ``subject``."""

        if not subject:
            raise ValueError("schemaregistry: subject is required")
        if msg is None:
            raise ValueError("schemaregistry: message is required")

        async with asyncio.timeout(timeout):
            desc = msg.DESCRIPTOR
            fds = build_file_descriptor_set(desc.file)
            schema_bytes = fds.SerializeToString()

            metadata = Metadata(
                schema=SchemaRef(
                    path=desc.file.name,
                    full_name=desc.full_name,
                ),
            )
            metadata_bytes = metadata.to_json().encode("utf-8")

            try:
                await self._schema_kv.put(desc.file.name, schema_bytes)
            except Exception as exc:
                raise RuntimeError(
                    f"schemaregistry: store schema {desc.file.name!r}: {exc}"
                ) from exc

            try:
                await self._metadata_kv.put(subject, metadata_bytes)
            except Exception as exc:
                raise RuntimeError(
                    f"schemaregistry: store metadata for subject {subject!r}: {exc}"
                ) from exc

    async def pull(
        self,
        subject: str,
        *,
        timeout: float | None = None,
    ) -> tuple[FileDescriptorSet, Metadata]:
        """Return the FileDescriptorSet and metadata for ``subject``."""

        if not subject:
            raise ValueError("schemaregistry: subject is required")

        async with asyncio.timeout(timeout):
            meta_entry = await _watch_key(self._metadata_kv, subject)
            if meta_entry.value is None:
                raise RuntimeError(f"schemaregistry: metadata for subject {subject!r} is empty")
            metadata = Metadata.from_json(meta_entry.value)

            schema_entry = await _watch_key(self._schema_kv, metadata.schema.path)
            if schema_entry.value is None:
                raise RuntimeError(f"schemaregistry: schema {metadata.schema.path!r} is empty")

            fds = FileDescriptorSet.FromString(schema_entry.value)
            return fds, metadata


async def _watch_key(key_value: kv.KeyValue, key: str) -> kv.KeyValue.Entry:
    """Wait until ``key`` exists in ``key_value`` and return it.

    Deleted or purged entries are ignored so that ``pull`` waits for a real
    value. If the operation is cancelled, the cancellation is propagated.
    """

    watcher = await key_value.watch(key)
    try:
        async for entry in watcher:
            if entry is None:
                # Initial state marker; keep waiting for a real value.
                continue
            typed_entry = cast(kv.KeyValue.Entry, entry)
            if typed_entry.operation in (kv.KV_DEL, kv.KV_PURGE):
                continue
            return typed_entry
    finally:
        try:
            await watcher.stop()  # type: ignore[no-untyped-call]
        except asyncio.CancelledError:
            raise
        except Exception:
            # Cleanup errors must not mask the original result or cancellation.
            pass

    raise RuntimeError(f"schemaregistry: watcher for key {key!r} closed unexpectedly")
