package schemaregistry

import (
	"context"
	"errors"
	"sync"
	"testing"
	"time"

	"github.com/nats-io/nats.go"
	"github.com/ytakio/nats_proto_schema_registry/go/example/mcap/sensorpb"
	"google.golang.org/protobuf/types/descriptorpb"
	"google.golang.org/protobuf/types/known/anypb"
	"google.golang.org/protobuf/types/known/wrapperspb"
)

// TestPushAndPull verifies that a protobuf message can be pushed for a subject
// and then pulled back, returning the correct metadata and FileDescriptorSet.
func TestPushAndPull(t *testing.T) {
	ctx := context.Background()
	reg := newRegistryWithKeyValues(newMockKeyValue(), newMockKeyValue())

	subject := "orders.created"
	msg := wrapperspb.String("hello")

	if err := reg.Push(ctx, subject, msg); err != nil {
		t.Fatalf("Push failed: %v", err)
	}

	fds, meta, err := reg.Pull(ctx, subject)
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
	ctx := context.Background()
	reg := newRegistryWithKeyValues(newMockKeyValue(), newMockKeyValue())

	if err := reg.Push(ctx, "events.any", &anypb.Any{}); err != nil {
		t.Fatalf("Push failed: %v", err)
	}

	_, meta, err := reg.Pull(ctx, "events.any")
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

// TestPullNotFound verifies that Pull blocks until the context is cancelled
// when the subject has not been registered.
func TestPullNotFound(t *testing.T) {
	ctx, cancel := context.WithTimeout(context.Background(), 50*time.Millisecond)
	defer cancel()

	reg := newRegistryWithKeyValues(newMockKeyValue(), newMockKeyValue())

	_, _, err := reg.Pull(ctx, "unknown.subject")
	if err == nil {
		t.Fatal("expected error for missing subject")
	}
}

// TestPullWaitsForPush verifies that Pull can start before a subject is
// registered and returns once the matching schema is pushed.
func TestPullWaitsForPush(t *testing.T) {
	ctx := context.Background()
	reg := newRegistryWithKeyValues(newMockKeyValue(), newMockKeyValue())

	subject := "orders.created"
	msg := wrapperspb.String("hello")

	type result struct {
		fds  *descriptorpb.FileDescriptorSet
		meta *Metadata
		err  error
	}
	ch := make(chan result, 1)

	go func() {
		fds, meta, err := reg.Pull(ctx, subject)
		ch <- result{fds, meta, err}
	}()

	time.Sleep(50 * time.Millisecond)

	if err := reg.Push(ctx, subject, msg); err != nil {
		t.Fatalf("Push failed: %v", err)
	}

	select {
	case res := <-ch:
		if res.err != nil {
			t.Fatalf("Pull failed: %v", res.err)
		}
		if res.meta.Schema.FullName != "google.protobuf.StringValue" {
			t.Errorf("unexpected full name: %s", res.meta.Schema.FullName)
		}
		if len(res.fds.File) == 0 {
			t.Error("lookup returned empty descriptor set")
		}
	case <-time.After(time.Second):
		t.Fatal("Pull did not return after Push")
	}
}

// TestPushValidation verifies that Push rejects invalid arguments.
func TestPushValidation(t *testing.T) {
	ctx := context.Background()
	reg := newRegistryWithKeyValues(newMockKeyValue(), newMockKeyValue())

	if err := reg.Push(ctx, "", wrapperspb.String("x")); err == nil {
		t.Error("expected error for empty subject")
	}
	if err := reg.Push(ctx, "sub", nil); err == nil {
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

// TestBuildFileDescriptorSetIncludesTransitiveGoogleImports verifies that a
// message importing google.protobuf.Type also includes the transitive Google
// imports (any.proto and source_context.proto) in the descriptor set. This
// mirrors the dependency chain used by the mcap example's SensorReading.
func TestBuildFileDescriptorSetIncludesTransitiveGoogleImports(t *testing.T) {
	fds := buildFileDescriptorSet((&sensorpb.SensorReading{}).ProtoReflect().Descriptor().ParentFile())
	if len(fds.File) == 0 {
		t.Fatal("expected files in descriptor set")
	}

	required := []string{
		"google/protobuf/type.proto",
		"google/protobuf/any.proto",
		"google/protobuf/source_context.proto",
	}
	for _, name := range required {
		found := false
		for _, f := range fds.File {
			if f.GetName() == name {
				found = true
				break
			}
		}
		if !found {
			t.Errorf("expected %s in descriptor set", name)
		}
	}
}

// mockKeyValue is a minimal in-memory implementation of nats.KeyValue for unit
// testing. It supports live watchers so that Pull can wait for keys that do not
// yet exist.
type mockKeyValue struct {
	mu       sync.Mutex
	data     map[string][]byte
	watchers map[string][]*mockWatcher
}

func newMockKeyValue() *mockKeyValue {
	return &mockKeyValue{
		data:     make(map[string][]byte),
		watchers: make(map[string][]*mockWatcher),
	}
}

// Create stores a new key. It returns nats.ErrKeyExists if the key is already
// present.
func (m *mockKeyValue) Create(key string, value []byte) (uint64, error) {
	m.mu.Lock()
	defer m.mu.Unlock()
	if _, ok := m.data[key]; ok {
		return 0, nats.ErrKeyExists
	}
	m.data[key] = value
	return 1, nil
}

// Put stores or overwrites a key and notifies any active watchers.
func (m *mockKeyValue) Put(key string, value []byte) (uint64, error) {
	m.mu.Lock()
	defer m.mu.Unlock()
	m.data[key] = value
	for _, w := range m.watchers[key] {
		select {
		case w.updates <- &mockEntry{key: key, value: value}:
		case <-w.ctx.Done():
		}
	}
	return 1, nil
}

// Get returns the value for a key, or nats.ErrKeyNotFound if it does not exist.
func (m *mockKeyValue) Get(key string) (nats.KeyValueEntry, error) {
	m.mu.Lock()
	defer m.mu.Unlock()
	v, ok := m.data[key]
	if !ok {
		return nil, nats.ErrKeyNotFound
	}
	return &mockEntry{key: key, value: v}, nil
}

// Watch creates a watcher for the given key. If the key already exists, the
// current value is emitted first, followed by a nil marker. Subsequent Put
// calls for the key are delivered to the watcher.
func (m *mockKeyValue) Watch(key string, opts ...nats.WatchOpt) (nats.KeyWatcher, error) {
	ctx := context.Background()
	for _, opt := range opts {
		if opt != nil {
			if c, ok := opt.(context.Context); ok {
				ctx = c
			}
		}
	}

	w := &mockWatcher{
		updates: make(chan nats.KeyValueEntry, 4),
		ctx:     ctx,
	}

	m.mu.Lock()
	if v, ok := m.data[key]; ok {
		w.updates <- &mockEntry{key: key, value: v}
	}
	w.updates <- nil
	m.watchers[key] = append(m.watchers[key], w)
	m.mu.Unlock()

	return w, nil
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

func (m *mockKeyValue) WatchAll(...nats.WatchOpt) (nats.KeyWatcher, error) {
	return nil, errors.New("not implemented")
}

func (m *mockKeyValue) Status() (nats.KeyValueStatus, error) {
	return nil, errors.New("not implemented")
}

// mockWatcher is a minimal in-memory implementation of nats.KeyWatcher.
type mockWatcher struct {
	updates chan nats.KeyValueEntry
	ctx     context.Context
}

func (w *mockWatcher) Updates() <-chan nats.KeyValueEntry { return w.updates }
func (w *mockWatcher) Stop() error {
	close(w.updates)
	return nil
}
func (w *mockWatcher) Context() context.Context { return w.ctx }

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
