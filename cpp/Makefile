BUILD_DIR ?= build
CMAKE ?= cmake
COMPOSE ?= docker compose
COMPOSE_FILE ?= ../docker-compose.yml
PROTOC ?= protoc

.PHONY: build test test-race check clean proto up down run-mcap-example

build:
	$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release
	$(CMAKE) --build $(BUILD_DIR)

# ThreadSanitizer is used for race detection in C++.
test:
	$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release
	$(CMAKE) --build $(BUILD_DIR) --target registry_test
	cd $(BUILD_DIR) && ctest -R registry_test --output-on-failure --timeout 30

test-race:
	$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-fsanitize=thread -fno-omit-frame-pointer"
	$(CMAKE) --build $(BUILD_DIR) --target registry_test
	cd $(BUILD_DIR) && ctest -R registry_test --output-on-failure --timeout 30

check: test-race

clean:
	rm -rf $(BUILD_DIR)
	rm -rf examples/register/build examples/lookup/build examples/mcap/build
	rm -f coverage.out *.mcap

proto:
	cd examples/mcap && $(PROTOC) --proto_path=. --cpp_out=generated sensor.proto

up:
	$(COMPOSE) -f $(COMPOSE_FILE) up -d

down:
	$(COMPOSE) -f $(COMPOSE_FILE) down --remove-orphans

run-mcap-example: build
	$(COMPOSE) -f $(COMPOSE_FILE) up -d
	$(BUILD_DIR)/examples/mcap/recorder & RECORDER_PID=$$!; \
	sleep 2; \
	$(BUILD_DIR)/examples/mcap/producer & PRODUCER_PID=$$!; \
	sleep 10; \
	kill -s INT $$RECORDER_PID; \
	kill -s INT $$PRODUCER_PID; \
	wait $$RECORDER_PID; \
	wait $$PRODUCER_PID; \
	$(COMPOSE) -f $(COMPOSE_FILE) down --remove-orphans

