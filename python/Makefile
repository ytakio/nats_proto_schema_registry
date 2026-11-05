.PHONY: build test lint fmt check clean proto up down run-mcap-example

build:
	uv build

test:
	uv run pytest

lint:
	uv run ruff check .

fmt:
	uv run ruff format .

check: lint test

clean:
	rm -f coverage.out *.mcap

PROTOC ?= python -m grpc_tools.protoc

proto:
	cd examples/mcap && $(PROTOC) --proto_path=. --python_out=. sensorpb/sensor.proto

COMPOSE ?= docker compose
COMPOSE_FILE ?= ../docker-compose.yml

up:
	$(COMPOSE) -f $(COMPOSE_FILE) up -d

down:
	$(COMPOSE) -f $(COMPOSE_FILE) down --remove-orphans

run-mcap-example:
	$(COMPOSE) -f $(COMPOSE_FILE) up -d
	uv run python -u examples/mcap/cmd/recorder.py & RECORDER_PID=$$!; \
	sleep 2; \
	uv run python examples/mcap/cmd/producer.py & PRODUCER_PID=$$!; \
	sleep 10; \
	kill -s INT $$RECORDER_PID; \
	kill -s INT $$PRODUCER_PID; \
	wait $$RECORDER_PID; \
	wait $$PRODUCER_PID; \
	$(COMPOSE) -f $(COMPOSE_FILE) down --remove-orphans

