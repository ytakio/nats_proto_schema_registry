"""Tests for the Python schema registry implementation."""

from __future__ import annotations

import asyncio
from collections.abc import AsyncIterator
from dataclasses import dataclass, field

import pytest
from google.protobuf.any_pb2 import Any
from google.protobuf.type_pb2 import Type
from google.protobuf.wrappers_pb2 import StringValue

from nats_proto_schema_registry.descriptor import build_file_descriptor_set
from nats_proto_schema_registry.nats_registry import NatsRegistry

KV_DEL = "DEL"
KV_PURGE = "PURGE"


@dataclass
class MockEntry:
    key: str
    value: bytes | None
    operation: str | None = None


@dataclass
class MockWatcher:
    updates_queue: asyncio.Queue[MockEntry | None] = field(
        default_factory=lambda: asyncio.Queue(maxsize=256)
    )
    stopped: bool = False

    async def updates(self, timeout: float | None = 5.0) -> MockEntry | None:
        if timeout is None:
            return await self.updates_queue.get()
        return await asyncio.wait_for(self.updates_queue.get(), timeout=timeout)

    async def stop(self) -> None:
        self.stopped = True

    def __aiter__(self) -> AsyncIterator[MockEntry | None]:
        return self

    async def __anext__(self) -> MockEntry | None:
        return await self.updates_queue.get()


@dataclass
class MockKeyValue:
    data: dict[str, bytes] = field(default_factory=dict)
    watchers: dict[str, list[MockWatcher]] = field(default_factory=dict)

    async def put(self, key: str, value: bytes) -> int:
        self.data[key] = value
        for watcher in self.watchers.get(key, []):
            await watcher.updates_queue.put(MockEntry(key=key, value=value))
        return 1

    async def get(self, key: str) -> MockEntry:
        if key not in self.data:
            raise KeyError(key)
        return MockEntry(key=key, value=self.data[key])

    async def watch(self, key: str, **kwargs: object) -> MockWatcher:
        watcher = MockWatcher()
        if key in self.data:
            await watcher.updates_queue.put(MockEntry(key=key, value=self.data[key]))
        await watcher.updates_queue.put(None)
        self.watchers.setdefault(key, []).append(watcher)
        return watcher


@pytest.fixture
def registry() -> NatsRegistry:
    return NatsRegistry(
        metadata_kv=MockKeyValue(),  # type: ignore[arg-type]
        schema_kv=MockKeyValue(),  # type: ignore[arg-type]
    )


async def test_push_and_pull(registry: NatsRegistry) -> None:
    subject = "orders.created"
    msg = StringValue(value="hello")

    await registry.push(subject, msg)

    fds, meta = await registry.pull(subject)

    assert meta.schema.full_name == "google.protobuf.StringValue"
    assert meta.schema.path == "google/protobuf/wrappers.proto"
    assert len(fds.file) > 0


async def test_push_any(registry: NatsRegistry) -> None:
    subject = "events.any"
    await registry.push(subject, Any())

    _, meta = await registry.pull(subject)

    assert meta.schema.full_name == "google.protobuf.Any"
    assert meta.schema.path == "google/protobuf/any.proto"


async def test_pull_not_found(registry: NatsRegistry) -> None:
    with pytest.raises(TimeoutError):
        await registry.pull("unknown.subject", timeout=0.05)


async def test_pull_waits_for_push(registry: NatsRegistry) -> None:
    subject = "orders.created"
    msg = StringValue(value="hello")

    task = asyncio.create_task(registry.pull(subject, timeout=1.0))
    await asyncio.sleep(0.05)

    await registry.push(subject, msg)

    fds, meta = await task
    assert meta.schema.full_name == "google.protobuf.StringValue"
    assert len(fds.file) > 0


async def test_pull_waits_longer_than_default_updates_timeout(
    registry: NatsRegistry,
) -> None:
    """pull must not be limited by the nats-py updates() default timeout."""
    subject = "orders.created"
    msg = StringValue(value="hello")

    task = asyncio.create_task(registry.pull(subject, timeout=10.0))
    await asyncio.sleep(0.1)

    await registry.push(subject, msg)

    fds, meta = await task
    assert meta.schema.full_name == "google.protobuf.StringValue"
    assert len(fds.file) > 0


async def test_pull_cancellation(registry: NatsRegistry) -> None:
    task = asyncio.create_task(registry.pull("unknown.subject"))
    await asyncio.sleep(0.05)
    task.cancel()

    with pytest.raises(asyncio.CancelledError):
        await task


async def test_push_validation(registry: NatsRegistry) -> None:
    with pytest.raises(ValueError):
        await registry.push("", StringValue(value="x"))
    with pytest.raises(ValueError):
        await registry.push("sub", None)  # type: ignore[arg-type]


async def test_build_file_descriptor_set_includes_dependencies() -> None:
    fds = build_file_descriptor_set(Any.DESCRIPTOR.file)
    assert len(fds.file) > 0

    found = any(f.name == "google/protobuf/any.proto" for f in fds.file)
    assert found


async def test_build_file_descriptor_set_includes_transitive_google_imports() -> None:
    fds = build_file_descriptor_set(Type.DESCRIPTOR.file)
    assert len(fds.file) > 0

    required = {
        "google/protobuf/type.proto",
        "google/protobuf/any.proto",
        "google/protobuf/source_context.proto",
    }
    found = {f.name for f in fds.file}
    assert required.issubset(found)
