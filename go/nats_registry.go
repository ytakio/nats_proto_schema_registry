package schemaregistry

import (
	"encoding/json"
	"errors"
	"fmt"

	"github.com/nats-io/nats.go"
	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/types/descriptorpb"
)

// NatsRegistry is a Registry implementation backed by NATS Key-Value stores.
type NatsRegistry struct {
	metadataKV nats.KeyValue
	schemaKV   nats.KeyValue
}

// New creates a Registry backed by NATS KV buckets. If the buckets do not
// exist they are created with sensible defaults. The caller retains ownership
// of js and is responsible for closing the underlying NATS connection.
func New(js nats.JetStreamContext, metadataBucket, schemaBucket string) (Registry, error) {
	if js == nil {
		return nil, errors.New("schemaregistry: JetStreamContext is required")
	}
	if metadataBucket == "" {
		return nil, errors.New("schemaregistry: metadata bucket name is required")
	}
	if schemaBucket == "" {
		return nil, errors.New("schemaregistry: schema bucket name is required")
	}

	metadataKV, err := getOrCreateKV(js, metadataBucket)
	if err != nil {
		return nil, fmt.Errorf("schemaregistry: metadata bucket %q: %w", metadataBucket, err)
	}

	schemaKV, err := getOrCreateKV(js, schemaBucket)
	if err != nil {
		return nil, fmt.Errorf("schemaregistry: schema bucket %q: %w", schemaBucket, err)
	}

	return &NatsRegistry{
		metadataKV: metadataKV,
		schemaKV:   schemaKV,
	}, nil
}

func getOrCreateKV(js nats.JetStreamContext, bucket string) (nats.KeyValue, error) {
	kv, err := js.KeyValue(bucket)
	if err == nil {
		return kv, nil
	}
	if !errors.Is(err, nats.ErrBucketNotFound) {
		return nil, err
	}
	return js.CreateKeyValue(&nats.KeyValueConfig{Bucket: bucket})
}

// Push implements Registry.Push.
func (r *NatsRegistry) Push(subject string, msg proto.Message) error {
	if subject == "" {
		return errors.New("schemaregistry: subject is required")
	}
	if msg == nil {
		return errors.New("schemaregistry: message is required")
	}

	desc := msg.ProtoReflect().Descriptor()
	fds := buildFileDescriptorSet(desc.ParentFile())

	schemaBytes, err := proto.Marshal(fds)
	if err != nil {
		return fmt.Errorf("schemaregistry: marshal FileDescriptorSet: %w", err)
	}

	fullName := desc.FullName()
	path := desc.ParentFile().Path()
	metadata := Metadata{
		Schema: SchemaRef{
			Path:     path,
			FullName: string(fullName),
		},
	}
	metadataBytes, err := json.Marshal(metadata)
	if err != nil {
		return fmt.Errorf("schemaregistry: marshal metadata: %w", err)
	}

	if _, err := r.schemaKV.Put(path, schemaBytes); err != nil {
		return fmt.Errorf("schemaregistry: store schema %q: %w", path, err)
	}
	if _, err := r.metadataKV.Put(subject, metadataBytes); err != nil {
		return fmt.Errorf("schemaregistry: store metadata for subject %q: %w", subject, err)
	}

	return nil
}

// Pull implements Registry.Pull.
func (r *NatsRegistry) Pull(subject string) (*descriptorpb.FileDescriptorSet, *Metadata, error) {
	if subject == "" {
		return nil, nil, errors.New("schemaregistry: subject is required")
	}

	metaEntry, err := r.metadataKV.Get(subject)
	if err != nil {
		if errors.Is(err, nats.ErrKeyNotFound) {
			return nil, nil, fmt.Errorf("schemaregistry: subject %q not found", subject)
		}
		return nil, nil, fmt.Errorf("schemaregistry: get metadata for subject %q: %w", subject, err)
	}

	var meta Metadata
	if err := json.Unmarshal(metaEntry.Value(), &meta); err != nil {
		return nil, nil, fmt.Errorf("schemaregistry: unmarshal metadata for subject %q: %w", subject, err)
	}

	schemaEntry, err := r.schemaKV.Get(meta.Schema.Path)
	if err != nil {
		if errors.Is(err, nats.ErrKeyNotFound) {
			return nil, nil, fmt.Errorf("schemaregistry: schema %q for subject %q not found", meta.Schema.Path, subject)
		}
		return nil, nil, fmt.Errorf("schemaregistry: get schema %q: %w", meta.Schema.Path, err)
	}

	var fds descriptorpb.FileDescriptorSet
	if err := proto.Unmarshal(schemaEntry.Value(), &fds); err != nil {
		return nil, nil, fmt.Errorf("schemaregistry: unmarshal FileDescriptorSet %q: %w", meta.Schema.Path, err)
	}

	return &fds, &meta, nil
}

// Close implements Registry.Close.
func (r *NatsRegistry) Close() error {
	return nil
}
