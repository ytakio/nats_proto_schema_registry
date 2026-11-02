package schemaregistry

import (
	"errors"
	"testing"
	"time"

	"github.com/nats-io/nats.go"
	"google.golang.org/protobuf/types/known/anypb"
	"google.golang.org/protobuf/types/known/wrapperspb"
)

// TestPushAndPull verifies that a protobuf message can be pushed for a subject
// and then pulled back, returning the correct metadata and FileDescriptorSet.
func TestPushAndPull(t *testing.T) {
	reg := newRegistryWithKeyValues(newMockKeyValue(), newMockKeyValue())

	subject := "orders.created"
	msg := wrapperspb.String("hello")

	if err := reg.Push(subject, msg); err != nil {
		t.Fatalf("Push failed: %v", err)
	}

	fds, meta, err := reg.Pull(subject)
	if err != nil {
		t.Fatalf("Pull failed: %v", err)
	}
	if meta.Schema.FullName != "google.protobuf.StringValue" {
		t.Errorf("unexpected full name: %s", meta.Schema.FullName)
	}
	if meta.Schema.Path != "google/protobuf/wrappers.proto" {
		t.Errorf("unexpected path: %s", meta.Schema.Path)
	}
	if len(fds.File) == 0 {
		t.Error("lookup returned empty descriptor set")
	}
}

// TestPushAny verifies that a message with transitive imports (google.protobuf.Any)
// is registered with the correct proto path and full name.
func TestPushAny(t *testing.T) {
	reg := newRegistryWithKeyValues(newMockKeyValue(), newMockKeyValue())

	if err := reg.Push("events.any", &anypb.Any{}); err != nil {
		t.Fatalf("Push failed: %v", err)
	}

	_, meta, err := reg.Pull("events.any")
	if err != nil {
		t.Fatalf("Pull failed: %v", err)
	}
	if meta.Schema.FullName != "google.protobuf.Any" {
		t.Errorf("unexpected full name: %s", meta.Schema.FullName)
	}
	if meta.Schema.Path != "google/protobuf/any.proto" {
		t.Errorf("unexpected path: %s", meta.Schema.Path)
	}
}

// TestPullNotFound verifies that Pull returns an error when the subject has not
// been registered.
func TestPullNotFound(t *testing.T) {
	reg := newRegistryWithKeyValues(newMockKeyValue(), newMockKeyValue())

	_, _, err := reg.Pull("unknown.subject")
	if err == nil {
		t.Fatal("expected error for missing subject")
	}
}

// TestPushValidation verifies that Push rejects invalid arguments.
func TestPushValidation(t *testing.T) {
	reg := newRegistryWithKeyValues(newMockKeyValue(), newMockKeyValue())

	if err := reg.Push("", wrapperspb.String("x")); err == nil {
		t.Error("expected error for empty subject")
	}
	if err := reg.Push("sub", nil); err == nil {
		t.Error("expected error for nil message")
	}
}

// TestBuildFileDescriptorSetIncludesDependencies verifies that
// buildFileDescriptorSet collects the target file and its transitive imports.
func TestBuildFileDescriptorSetIncludesDependencies(t *testing.T) {
	fds := buildFileDescriptorSet((&anypb.Any{}).ProtoReflect().Descriptor().ParentFile())
	if len(fds.File) == 0 {
		t.Fatal("expected files in descriptor set")
	}

	found := false
	for _, f := range fds.File {
		if f.GetName() == "google/protobuf/any.proto" {
			found = true
			break
		}
	}
	if !found {
		t.Error("expected any.proto in descriptor set")
	}
}

// mockKeyValue is a minimal in-memory implementation of nats.KeyValue for unit
// testing.
type mockKeyValue struct {
	data map[string][]byte
}

func newMockKeyValue() *mockKeyValue {
	return &mockKeyValue{data: make(map[string][]byte)}
}

// Create stores a new key. It returns nats.ErrKeyExists if the key is already
// present.
func (m *mockKeyValue) Create(key string, value []byte) (uint64, error) {
	if _, ok := m.data[key]; ok {
		return 0, nats.ErrKeyExists
	}
	m.data[key] = value
	return 1, nil
}

// Put stores or overwrites a key.
func (m *mockKeyValue) Put(key string, value []byte) (uint64, error) {
	m.data[key] = value
	return 1, nil
}

// Get returns the value for a key, or nats.ErrKeyNotFound if it does not exist.
func (m *mockKeyValue) Get(key string) (nats.KeyValueEntry, error) {
	v, ok := m.data[key]
	if !ok {
		return nil, nats.ErrKeyNotFound
	}
	return &mockEntry{key: key, value: v}, nil
}

// The remaining methods are required to satisfy the nats.KeyValue interface but
// are not used by these tests.

func (m *mockKeyValue) Update(string, []byte, uint64) (uint64, error) {
	return 0, errors.New("not implemented")
}

func (m *mockKeyValue) Delete(string, ...nats.DeleteOpt) error {
	return errors.New("not implemented")
}

func (m *mockKeyValue) GetRevision(string, uint64) (nats.KeyValueEntry, error) {
	return nil, errors.New("not implemented")
}

func (m *mockKeyValue) PutString(string, string) (uint64, error) {
	return 0, errors.New("not implemented")
}

func (m *mockKeyValue) ListKeys(...nats.WatchOpt) (nats.KeyLister, error) {
	return nil, errors.New("not implemented")
}

func (m *mockKeyValue) PurgeDeletes(...nats.PurgeOpt) error {
	return errors.New("not implemented")
}

func (m *mockKeyValue) Keys(...nats.WatchOpt) ([]string, error) {
	return nil, errors.New("not implemented")
}

func (m *mockKeyValue) History(string, ...nats.WatchOpt) ([]nats.KeyValueEntry, error) {
	return nil, errors.New("not implemented")
}

func (m *mockKeyValue) Bucket() string {
	return "mock"
}

func (m *mockKeyValue) Purge(string, ...nats.DeleteOpt) error {
	return errors.New("not implemented")
}

func (m *mockKeyValue) Watch(string, ...nats.WatchOpt) (nats.KeyWatcher, error) {
	return nil, errors.New("not implemented")
}

func (m *mockKeyValue) WatchAll(...nats.WatchOpt) (nats.KeyWatcher, error) {
	return nil, errors.New("not implemented")
}

func (m *mockKeyValue) Status() (nats.KeyValueStatus, error) {
	return nil, errors.New("not implemented")
}

// mockEntry is a minimal in-memory implementation of nats.KeyValueEntry.
type mockEntry struct {
	key   string
	value []byte
}

func (e *mockEntry) Key() string                { return e.key }
func (e *mockEntry) Value() []byte              { return e.value }
func (e *mockEntry) Revision() uint64           { return 1 }
func (e *mockEntry) Created() time.Time         { return time.Now() }
func (e *mockEntry) Operation() nats.KeyValueOp { return nats.KeyValuePut }
func (e *mockEntry) Bucket() string             { return "mock" }
func (e *mockEntry) Delta() uint64              { return 0 }

var _ nats.KeyValue = (*mockKeyValue)(nil)

// newRegistryWithKeyValues creates a Registry backed by the supplied key-value
// stores. It is useful for tests that want to inject a mock or pre-populated KV.
func newRegistryWithKeyValues(metadataKV, schemaKV nats.KeyValue) Registry {
	return &NatsRegistry{
		metadataKV: metadataKV,
		schemaKV:   schemaKV,
	}
}
