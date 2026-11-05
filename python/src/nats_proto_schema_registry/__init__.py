"""Python implementation of the NATS protobuf schema registry."""

from __future__ import annotations

from nats_proto_schema_registry.nats_registry import NatsRegistry, create_registry
from nats_proto_schema_registry.types import Metadata, SchemaRef

__all__ = [
    "create_registry",
    "Metadata",
    "NatsRegistry",
    "SchemaRef",
]
