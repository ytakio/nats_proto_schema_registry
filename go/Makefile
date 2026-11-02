.PHONY: build test test-race vet fmt lint check clean

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
	rm -f coverage.out

