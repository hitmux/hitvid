.PHONY: native test build

native:
	./scripts/build-native.sh

test:
	go test ./...

build:
	go build -o hitvid .
