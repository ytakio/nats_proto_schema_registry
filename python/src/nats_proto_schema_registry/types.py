"""Public types for the NATS protobuf schema registry."""

from __future__ import annotations

import json
from dataclasses import asdict, dataclass


@dataclass
class SchemaRef:
    """Identifies a protobuf message type and the .proto file that defines it.

    This is stored as JSON in the metadata bucket keyed by the NATS subject.
    """

    # Path is the .proto file path, for example "google/protobuf/any.proto".
    path: str
    # FullName is the fully qualified protobuf message name, for example
    # "google.protobuf.Any".
    full_name: str

    def to_json(self) -> str:
        return json.dumps(asdict(self))

    @classmethod
    def from_dict(cls, data: dict[str, str]) -> SchemaRef:
        return cls(path=data["path"], full_name=data["full_name"])


@dataclass
class Metadata:
    """JSON document stored per subject in the metadata bucket."""

    schema: SchemaRef

    def to_json(self) -> str:
        return json.dumps(asdict(self))

    @classmethod
    def from_json(cls, data: bytes) -> Metadata:
        doc = json.loads(data)
        return cls(schema=SchemaRef.from_dict(doc["schema"]))


__all__ = [
    "Metadata",
    "SchemaRef",
]
