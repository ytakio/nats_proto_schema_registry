"""Utilities for building protobuf FileDescriptorSets."""

from __future__ import annotations

from typing import Any

from google.protobuf.descriptor_pb2 import FileDescriptorProto, FileDescriptorSet


def build_file_descriptor_set(file: Any) -> FileDescriptorSet:
    """Build a FileDescriptorSet from ``file`` including transitive imports.

    ``file`` is typically ``msg.DESCRIPTOR.file``. Its concrete type depends on
    the protobuf runtime: it may be the pure-Python ``FileDescriptor``, the C
    extension descriptor, or the upb ``google._upb._message.FileDescriptor``.
    These types share the same interface but are not related in mypy's view,
    so ``Any`` is used here.

    The resulting files are ordered so that dependencies appear before the
    files that import them.
    """
    seen: set[str] = set()
    files: list[FileDescriptorProto] = []

    def collect(fd: Any) -> None:
        if fd.name in seen:
            return
        seen.add(fd.name)

        # Collect dependencies first so the resulting FileDescriptorSet is in a
        # valid topological order.
        for dep in fd.dependencies:
            collect(dep)

        proto = FileDescriptorProto()
        fd.CopyToProto(proto)
        files.append(proto)

    collect(file)

    return FileDescriptorSet(file=files)
