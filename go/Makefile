.PHONY: build test test-race vet fmt lint check clean proto up down run-mcap-example

build:
	go build ./...

test:
	go test ./...

test-race:
	go test -race ./...

vet:
	go vet ./...

fmt:
	go fmt ./...

lint: fmt vet

check: lint test-race

clean:
	rm -f coverage.out *.mcap

PROTOC ?= protoc
PROTOC_GEN_GO ?= protoc-gen-go

proto:
	cd example/mcap && $(PROTOC) --proto_path=. --go_out=paths=source_relative:. sensorpb/sensor.proto

COMPOSE ?= docker compose
COMPOSE_FILE ?= ../docker-compose.yml

up:
	$(COMPOSE) -f $(COMPOSE_FILE) up -d

down:
	$(COMPOSE) -f $(COMPOSE_FILE) down

run-mcap-example: build
	$(COMPOSE) -f $(COMPOSE_FILE) up -d
	go run ./example/mcap/cmd/recorder &
	RECORDER_PID=$$!; \
	sleep 2; \
	go run ./example/mcap/cmd/producer; \
	wait $$RECORDER_PID; \
	$(COMPOSE) -f $(COMPOSE_FILE) down

