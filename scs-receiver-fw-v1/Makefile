ARDUINO_CLI ?= arduino-cli
FQBN ?= arduino:avr:nano
CXX ?= c++
BUILD_DIR ?= .build
EXTRA_FLAGS ?=

.PHONY: all compile test clean
all: compile

compile:
	$(ARDUINO_CLI) compile --fqbn $(FQBN) --build-path $(CURDIR)/$(BUILD_DIR)/arduino --build-property 'compiler.cpp.extra_flags=$(EXTRA_FLAGS)' scs-receiver

test:
	mkdir -p $(BUILD_DIR)
	$(CXX) -std=c++11 -Wall -Wextra -Werror -Itests tests/test.cpp -o $(BUILD_DIR)/test
	$(BUILD_DIR)/test
	$(CXX) -std=c++11 -Wall -Wextra -Werror -Itests tests/radio_test.cpp -o $(BUILD_DIR)/radio-test
	$(BUILD_DIR)/radio-test
	$(CXX) -std=c++11 -Wall -Wextra -Werror -Itests tests/loop_test.cpp -o $(BUILD_DIR)/loop-test
	$(BUILD_DIR)/loop-test

clean:
	rm -rf $(BUILD_DIR)
