// Package schemaregistry provides a NATS KV backed schema registry for
// arbitrary protobuf messages.
//
// The registry stores two pieces of information:
//  1. Per-subject metadata that records which protobuf message type is sent on
//     the subject.
//  2. Per-proto-file FileDescriptorSet blobs that contain the schema
//     definitions.
//
// This allows publishers to register the schema for a subject, and consumers to
// look up the FileDescriptorSet and message type information at runtime.
package schemaregistry

import (
	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/types/descriptorpb"
)

// SchemaRef identifies a protobuf message type and the .proto file that defines
// it. It is stored as JSON in the metadata bucket keyed by the NATS subject.
type SchemaRef struct {
	// Path is the .proto file path, for example "google/protobuf/any.proto".
	Path string `json:"path"`
	// FullName is the fully qualified protobuf message name, for example
	// "google.protobuf.Any".
	FullName string `json:"full_name"`
}

// Metadata is the JSON document stored per subject in the metadata bucket.
type Metadata struct {
	Schema SchemaRef `json:"schema"`
}

// Registry is the public interface implemented by schema registry backends.
type Registry interface {
	// Push inspects the protobuf message type of msg, builds a
	// FileDescriptorSet containing the message's file and its transitive
	// dependencies, and stores it in the registry under the given subject.
	Push(subject string, msg proto.Message) error

	// Pull returns the FileDescriptorSet and metadata associated with the
	// given NATS subject.
	Pull(subject string) (*descriptorpb.FileDescriptorSet, *Metadata, error)
}
