package main

import (
	"context"
	"flag"
	"fmt"
	"log"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/foxglove/mcap/go/mcap"
	"github.com/nats-io/nats.go"
	schemaregistry "github.com/ytakio/nats_proto_schema_registry/go"
	"google.golang.org/protobuf/proto"
)

func main() {
	url := flag.String("url", "nats://localhost:4444", "NATS server URL")
	subject := flag.String("subject", "sensor.readings", "NATS subject to record")
	metadataBucket := flag.String("metadata-bucket", "metadata", "metadata KV bucket name")
	schemaBucket := flag.String("schema-bucket", "schemas", "schema KV bucket name")
	output := flag.String("output", "sensor.mcap", "output MCAP file")
	flag.Parse()

	// Stop cleanly on SIGINT/SIGTERM in addition to the normal stop conditions.
	sigCtx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()

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

	// Subscribe before waiting for the schema so messages published while we are
	// waiting are buffered. We use an async subscription with a small application
	// channel instead of SubscribeSync to avoid the large per-subscription queue
	// allocated by the synchronous API.
	msgCh := make(chan *nats.Msg, 256)
	sub, err := nc.Subscribe(*subject, func(msg *nats.Msg) {
		msgCh <- msg
	})
	if err != nil {
		log.Fatalf("subscribe: %v", err)
	}
	defer sub.Unsubscribe()
	fmt.Printf("recorder subscribed to %q, waiting for schema...\n", *subject)

	// Pull blocks until the producer registers the schema.
	ctx, cancel := context.WithTimeout(sigCtx, 2*time.Minute)
	defer cancel()

	fds, meta, err := reg.Pull(ctx, *subject)
	if err != nil {
		log.Fatalf("pull schema: %v", err)
	}
	fmt.Printf("resolved schema %s (%s)\n", meta.Schema.FullName, meta.Schema.Path)

	// Open the MCAP file and register the schema/channel.
	f, err := os.Create(*output)
	if err != nil {
		log.Fatalf("create output file: %v", err)
	}
	defer f.Close()

	writer, err := mcap.NewWriter(f, &mcap.WriterOptions{
		Chunked:     true,
		Compression: mcap.CompressionZSTD,
	})
	if err != nil {
		log.Fatalf("mcap writer: %v", err)
	}
	defer writer.Close()

	if err := writer.WriteHeader(&mcap.Header{Profile: "protobuf"}); err != nil {
		log.Fatalf("write header: %v", err)
	}

	schemaData, err := proto.Marshal(fds)
	if err != nil {
		log.Fatalf("marshal schema: %v", err)
	}

	// MCAP schema IDs must be non-zero, while channel IDs may be zero. The two
	// ID spaces are independent, so we start the schema at 1 and the channel at
	// 0 to reflect the specification's distinction.
	schema := &mcap.Schema{
		ID:       1,
		Name:     meta.Schema.FullName,
		Encoding: "protobuf",
		Data:     schemaData,
	}
	if err := writer.WriteSchema(schema); err != nil {
		log.Fatalf("write schema: %v", err)
	}

	channel := &mcap.Channel{
		ID:              0,
		SchemaID:        schema.ID,
		Topic:           *subject,
		MessageEncoding: "protobuf",
	}
	if err := writer.WriteChannel(channel); err != nil {
		log.Fatalf("write channel: %v", err)
	}

	// Record messages until a signal requests shutdown.
	var received uint32
	for {
		select {
		case <-sigCtx.Done():
			fmt.Printf("shutdown signal received, wrote %d messages\n", received)
			return
		case msg := <-msgCh:
			received++
			now := time.Now().UnixNano()
			if err := writer.WriteMessage(&mcap.Message{
				ChannelID:   channel.ID,
				Sequence:    received,
				LogTime:     uint64(now),
				PublishTime: uint64(now),
				Data:        msg.Data,
			}); err != nil {
				log.Fatalf("write message: %v", err)
			}
			fmt.Printf("recorded seq=%d\n", received)
		}
	}
}
