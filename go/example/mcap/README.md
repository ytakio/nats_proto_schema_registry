# MCAP Example

This example shows the schema registry's "lookup that waits" behaviour:

1. The **recorder** starts first, subscribes to a NATS subject, and waits for
   the schema to appear in the registry.
2. The **producer** starts afterwards, registers the schema, and publishes
   `SensorReading` messages at 1 Hz for 30 seconds.
3. The recorder resolves the schema, opens an MCAP file, and writes every
   received message to it.

## Running

Start NATS with JetStream from the repository root:

```bash
docker compose up -d
```

Then, in one terminal, start the recorder:

```bash
cd go
go run ./example/mcap/cmd/recorder
```

In another terminal, start the producer:

```bash
cd go
go run ./example/mcap/cmd/producer
```

The recorder writes `sensor.mcap` in the current directory. You can inspect it
with the [`mcap` CLI](https://mcap.dev/guides/cli):

```bash
mcap info sensor.mcap
mcap cat sensor.mcap
```

Or use the convenience target in `go/Makefile`:

```bash
cd go
make run-mcap-example
```

