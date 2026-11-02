package schemaregistry

import (
	"google.golang.org/protobuf/reflect/protodesc"
	"google.golang.org/protobuf/reflect/protoreflect"
	"google.golang.org/protobuf/types/descriptorpb"
)

// buildFileDescriptorSet walks from the file descriptor through all transitive
// imports, producing a FileDescriptorSet. The resulting files are ordered so
// that dependencies appear before the files that import them.
func buildFileDescriptorSet(file protoreflect.FileDescriptor) *descriptorpb.FileDescriptorSet {
	seen := make(map[string]struct{})
	var files []*descriptorpb.FileDescriptorProto

	var collect func(file protoreflect.FileDescriptor)
	collect = func(file protoreflect.FileDescriptor) {
		path := file.Path()
		if _, ok := seen[path]; ok {
			return
		}
		seen[path] = struct{}{}

		// Collect dependencies first so the resulting FileDescriptorSet is in a
		// valid topological order.
		imports := file.Imports()
		for i := 0; i < imports.Len(); i++ {
			collect(imports.Get(i).FileDescriptor)
		}

		fdp := protodesc.ToFileDescriptorProto(file)
		files = append(files, fdp)
	}

	collect(file)

	return &descriptorpb.FileDescriptorSet{File: files}
}
