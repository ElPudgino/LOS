ARDUINO_CLI ?= arduino-cli
FQBN ?= arduino:avr:nano
CXX ?= c++
BUILD_DIR ?= .build
EXTRA_FLAGS ?=

.PHONY: all compile test clean
all: compile

compile:
	$(ARDUINO_CLI) compile --fqbn $(FQBN) --build-path $(CURDIR)/$(BUILD_DIR)/arduino --build-property 'compiler.cpp.extra_flags=$(EXTRA_FLAGS)' scs-transmitter

test:
	mkdir -p $(BUILD_DIR)
	$(CXX) -std=c++11 -Wall -Wextra -Werror -Itests tests/test.cpp -o $(BUILD_DIR)/test
	$(BUILD_DIR)/test

clean:
	rm -rf $(BUILD_DIR)
