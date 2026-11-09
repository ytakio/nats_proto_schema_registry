.PHONY: build test test-race vet fmt lint check clean proto up down run-mcap-example

BUILD_DIR ?= build
INSTALL_DIR ?= $(BUILD_DIR)/install
CMAKE ?= cmake
COMPOSE ?= docker compose
COMPOSE_FILE ?= docker-compose.yml
PROTOC ?= protoc

build:
	$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$(INSTALL_DIR)
	$(CMAKE) --build $(BUILD_DIR)

# Race detection is supported by sanitizers in C++.
test:
	$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release
	$(CMAKE) --build $(BUILD_DIR) --target registry_test
	cd $(BUILD_DIR) && ctest -R registry_test --output-on-failure

test-race:
	$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-fsanitize=thread -fno-omit-frame-pointer"
	$(CMAKE) --build $(BUILD_DIR) --target registry_test
	cd $(BUILD_DIR) && ctest -R registry_test --output-on-failure

vet:
	@echo "vet: no-op for C++"

fmt:
	@echo "fmt: no-op for C++"

lint:
	@echo "lint: no-op for C++"

check: lint test-race

clean:
	rm -rf $(BUILD_DIR)
	rm -rf cpp/examples/register/build cpp/examples/lookup/build cpp/examples/mcap/build
	rm -f coverage.out *.mcap

proto:
	cd cpp/examples/mcap && $(PROTOC) --proto_path=. --cpp_out=generated sensor.proto

up:
	$(COMPOSE) -f $(COMPOSE_FILE) up -d

down:
	$(COMPOSE) -f $(COMPOSE_FILE) down --remove-orphans

run-mcap-example: build
	$(CMAKE) --install $(BUILD_DIR) --prefix $(INSTALL_DIR)
	$(CMAKE) -S cpp/examples/mcap -B cpp/examples/mcap/build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$(CURDIR)/$(INSTALL_DIR)
	$(CMAKE) --build cpp/examples/mcap/build
	$(COMPOSE) -f $(COMPOSE_FILE) up -d
	cpp/examples/mcap/build/recorder & RECORDER_PID=$$!; \
	sleep 2; \
	cpp/examples/mcap/build/producer & PRODUCER_PID=$$!; \
	sleep 10; \
	kill -s INT $$RECORDER_PID; \
	kill -s INT $$PRODUCER_PID; \
	wait $$RECORDER_PID; \
	wait $$PRODUCER_PID; \
	$(COMPOSE) -f $(COMPOSE_FILE) down --remove-orphans

