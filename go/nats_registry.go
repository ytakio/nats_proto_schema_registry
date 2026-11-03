package schemaregistry

import (
	"context"
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
func (r *NatsRegistry) Push(ctx context.Context, subject string, msg proto.Message) error {
	if ctx == nil {
		return errors.New("schemaregistry: context is required")
	}
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
//
// Pull watches the metadata bucket for the subject and then watches the schema
// bucket for the referenced .proto file. If either key does not exist yet, Pull
// blocks until it is created or until ctx is cancelled.
func (r *NatsRegistry) Pull(ctx context.Context, subject string) (*descriptorpb.FileDescriptorSet, *Metadata, error) {
	if ctx == nil {
		return nil, nil, errors.New("schemaregistry: context is required")
	}
	if subject == "" {
		return nil, nil, errors.New("schemaregistry: subject is required")
	}

	metaEntry, err := r.watchKey(ctx, r.metadataKV, subject)
	if err != nil {
		return nil, nil, fmt.Errorf("schemaregistry: wait for metadata for subject %q: %w", subject, err)
	}

	var meta Metadata
	if err := json.Unmarshal(metaEntry.Value(), &meta); err != nil {
		return nil, nil, fmt.Errorf("schemaregistry: unmarshal metadata for subject %q: %w", subject, err)
	}

	schemaEntry, err := r.watchKey(ctx, r.schemaKV, meta.Schema.Path)
	if err != nil {
		return nil, nil, fmt.Errorf("schemaregistry: wait for schema %q: %w", meta.Schema.Path, err)
	}

	var fds descriptorpb.FileDescriptorSet
	if err := proto.Unmarshal(schemaEntry.Value(), &fds); err != nil {
		return nil, nil, fmt.Errorf("schemaregistry: unmarshal FileDescriptorSet %q: %w", meta.Schema.Path, err)
	}

	return &fds, &meta, nil
}

// watchKey waits until the given key exists in kv and returns it. Deleted or
// purged entries are ignored so that Pull waits for a real value. If ctx is
// cancelled, watchKey returns the context error.
func (r *NatsRegistry) watchKey(ctx context.Context, kv nats.KeyValue, key string) (nats.KeyValueEntry, error) {
	watcher, err := kv.Watch(key, nats.Context(ctx))
	if err != nil {
		return nil, err
	}
	defer watcher.Stop()

	for {
		select {
		case entry, ok := <-watcher.Updates():
			if !ok {
				return nil, ctx.Err()
			}
			if entry == nil {
				// Initial state marker; keep waiting for a real value.
				continue
			}
			if entry.Operation() == nats.KeyValueDelete || entry.Operation() == nats.KeyValuePurge {
				continue
			}
			return entry, nil
		case <-ctx.Done():
			return nil, ctx.Err()
		}
	}
}
