# Сборка и прошивка скетча air_valve_arduino (Arduino Nano / ATmega328P)
#
# Использование:
#   make            - собрать
#   make flash      - собрать и прошить (порт по умолчанию /dev/ttyUSB0)
#   make flash PORT=/dev/ttyUSB1
#   make monitor    - прочитать вывод Serial (Ctrl+C для выхода)
#   make clean      - удалить артефакты сборки

FQBN       ?= arduino:avr:nano
SKETCH     ?= air_valve_arduino
BUILD_PATH ?= .build
PORT       ?= /dev/ttyUSB0
BAUD       ?= 115200

.PHONY: all build flash monitor clean

all: build

build:
	arduino-cli compile --fqbn $(FQBN) --build-path $(BUILD_PATH) $(SKETCH)

flash: build
	arduino-cli upload --fqbn $(FQBN) --build-path $(BUILD_PATH) $(SKETCH) --port $(PORT)

monitor:
	-stty -F $(PORT) $(BAUD) raw -echo
	cat $(PORT)

clean:
	rm -rf $(BUILD_PATH)
