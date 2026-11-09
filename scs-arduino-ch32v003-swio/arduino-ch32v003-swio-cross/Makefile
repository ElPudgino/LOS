# Arduino Uno by default; Nano: make FQBN=arduino:avr:nano
FQBN       ?= arduino:avr:uno
SKETCH     ?= swio_adapter
BUILD_PATH ?= .build
PORT       ?= /dev/ttyACM0

.PHONY: all build flash clean

all: build

build:
	arduino-cli compile --fqbn $(FQBN) --build-path $(BUILD_PATH) $(SKETCH)

flash: build
	arduino-cli upload --fqbn $(FQBN) --build-path $(BUILD_PATH) $(SKETCH) --port $(PORT)

clean:
	rm -rf $(BUILD_PATH)
