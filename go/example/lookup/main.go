package main

import (
	"context"
	"fmt"
	"log"

	"github.com/nats-io/nats.go"
	schemaregistry "github.com/ytakio/nats_proto_schema_registry/go"
)

func main() {
	nc, err := nats.Connect(nats.DefaultURL)
	if err != nil {
		log.Fatalf("connect: %v", err)
	}
	defer nc.Close()

	js, err := nc.JetStream()
	if err != nil {
		log.Fatalf("jetstream: %v", err)
	}

	reg, err := schemaregistry.New(js, "metadata", "schemas")
	if err != nil {
		log.Fatalf("registry: %v", err)
	}

	subject := "orders.created"
	ctx := context.Background()
	fds, meta, err := reg.Pull(ctx, subject)
	if err != nil {
		log.Fatalf("pull: %v", err)
	}

	fmt.Printf("subject: %s\n", subject)
	fmt.Printf("schema path: %s\n", meta.Schema.Path)
	fmt.Printf("schema full name: %s\n", meta.Schema.FullName)
	fmt.Printf("files in descriptor set: %d\n", len(fds.File))
}
