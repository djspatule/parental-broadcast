PORT        ?= /dev/ttyUSB0
BAUD        ?= 921600
FQBN        := esp32:esp32:esp32
BUILD       := ./build
DATA        := ./data
SRC         := src
MKLITTLEFS  := $(or $(shell which mklittlefs 2>/dev/null),$(HOME)/.local/bin/mklittlefs)

# LittleFS partition info for huge_app.csv:
#   spiffs  0x00290000  0x160000 (1 441 792 bytes = 1.375 MB)
FS_ADDR  := 0x290000
FS_SIZE  := 1441792

IMAGE    := littlefs.bin

.PHONY: all compile flash flashfs monitor clean

all: compile

## Compile firmware
compile:
	arduino-cli compile --fqbn $(FQBN) --build-path $(BUILD) $(SRC)

## Flash firmware only
flash: compile
	python3 -m esptool --chip esp32 --port $(PORT) --baud $(BAUD) \
		write-flash -z 0x10000 $(BUILD)/src.ino.bin

## Build and flash the LittleFS audio files
flashfs:
	$(MKLITTLEFS) -c $(DATA) -s $(FS_SIZE) $(IMAGE)
	python3 -m esptool --chip esp32 --port $(PORT) --baud $(BAUD) \
		write-flash $(FS_ADDR) $(IMAGE)

## Flash both firmware and filesystem (full install)
install: flash flashfs

## Open serial monitor (Ctrl+] to quit)
monitor:
	python3 -m serial.tools.miniterm --raw $(PORT) 115200

clean:
	rm -rf $(BUILD) $(IMAGE)
