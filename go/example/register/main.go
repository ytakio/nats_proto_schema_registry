package main

import (
	"context"
	"fmt"
	"log"

	"github.com/nats-io/nats.go"
	schemaregistry "github.com/ytakio/nats_proto_schema_registry/go"
	"google.golang.org/protobuf/types/known/wrapperspb"
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
	msg := wrapperspb.String("order-123")
	ctx := context.Background()

	if err := reg.Push(ctx, subject, msg); err != nil {
		log.Fatalf("push: %v", err)
	}

	fmt.Printf("pushed schema for subject %q\n", subject)
}
