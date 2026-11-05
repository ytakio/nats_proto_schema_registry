.PHONY: build build-example-mcap test test-race vet fmt lint check clean proto up down run-mcap-example

build:
	go build ./...

build-example-mcap:
	go build -o recorder ./example/mcap/cmd/recorder
	go build -o producer ./example/mcap/cmd/producer

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

run-mcap-example: build-example-mcap
	$(COMPOSE) -f $(COMPOSE_FILE) up -d
	./recorder & RECORDER_PID=$$!; \
	sleep 2; \
	./producer & PRODUCER_PID=$$!; \
	sleep 10; \
	kill -s INT $$RECORDER_PID; \
	kill -s INT $$PRODUCER_PID; \
	wait $$RECORDER_PID; \
	wait $$PRODUCER_PID; \
	rm -f recorder producer; \
	$(COMPOSE) -f $(COMPOSE_FILE) down --remove-orphans 
