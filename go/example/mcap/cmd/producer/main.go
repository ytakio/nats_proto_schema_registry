package main

import (
	"context"
	"flag"
	"fmt"
	"log"
	"time"

	"github.com/nats-io/nats.go"
	schemaregistry "github.com/ytakio/nats_proto_schema_registry/go"
	"github.com/ytakio/nats_proto_schema_registry/go/example/mcap/sensorpb"
	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/types/known/typepb"
)

func main() {
	url := flag.String("url", "nats://localhost:4444", "NATS server URL")
	subject := flag.String("subject", "sensor.readings", "NATS subject to publish on")
	metadataBucket := flag.String("metadata-bucket", "metadata", "metadata KV bucket name")
	schemaBucket := flag.String("schema-bucket", "schemas", "schema KV bucket name")
	flag.Parse()

	nc, err := nats.Connect(*url)
	if err != nil {
		log.Fatalf("connect: %v", err)
	}
	defer nc.Close()

	js, err := nc.JetStream()
	if err != nil {
		log.Fatalf("jetstream: %v", err)
	}

	reg, err := schemaregistry.New(js, *metadataBucket, *schemaBucket)
	if err != nil {
		log.Fatalf("registry: %v", err)
	}

	ctx := context.Background()

	// Register the schema for the subject. This unblocks the recorder which is
	// waiting for the schema via Pull().
	if err := reg.Push(ctx, *subject, &sensorpb.SensorReading{}); err != nil {
		log.Fatalf("push schema: %v", err)
	}
	fmt.Printf("registered schema for subject %q\n", *subject)

	// Publish one reading per second for 30 seconds.
	const total = 30
	for i := range total {
		now := time.Now()
		msg := &sensorpb.SensorReading{
			Sequence:    uint64(i + 1),
			Temperature: 20.0 + float64(i)*0.1,
			Humidity:    50.0 + float64(i)*0.2,
			Pressure:    1013.0 + float64(i)*0.05,
			TimestampNs: now.UnixNano(),
			DeviceType: &typepb.Type{
				Name:   "sensorpb.SensorReading",
				Syntax: typepb.Syntax_SYNTAX_PROTO3,
			},
		}

		data, err := proto.Marshal(msg)
		if err != nil {
			log.Fatalf("marshal: %v", err)
		}

		if err := nc.Publish(*subject, data); err != nil {
			log.Fatalf("publish: %v", err)
		}
		fmt.Printf("published seq=%d\n", msg.Sequence)

		if i < total-1 {
			time.Sleep(time.Until(now.Add(time.Second)))
		}
	}

	fmt.Println("producer finished")
}
