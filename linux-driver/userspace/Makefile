CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c11 -D_GNU_SOURCE $(shell pkg-config --cflags libevdev)
LDFLAGS = $(shell pkg-config --libs libevdev) -lm

SOURCES = rawaccel-daemon.c \
          rawaccel-device.c \
          rawaccel-accel.c \
          rawaccel-uinput.c \
          rawaccel-config.c

OBJECTS = $(SOURCES:.c=.o)
TARGET = rawaccel-daemon

.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

install: $(TARGET)
	install -D -m 755 $(TARGET) /usr/local/bin/$(TARGET)
	@if [ -d systemd ]; then \
		install -D -m 644 systemd/rawaccel.service /etc/systemd/system/rawaccel.service; \
		echo "Systemd service installed"; \
	fi

uninstall:
	rm -f /usr/local/bin/$(TARGET)
	rm -f /etc/systemd/system/rawaccel.service
	systemctl daemon-reload

# Dependencies
rawaccel-daemon.o: rawaccel-daemon.c rawaccel-device.h rawaccel-config.h rawaccel-types.h
rawaccel-device.o: rawaccel-device.c rawaccel-device.h rawaccel-accel.h rawaccel-types.h rawaccel-uinput.h
rawaccel-accel.o: rawaccel-accel.c rawaccel-accel.h rawaccel-types.h
rawaccel-uinput.o: rawaccel-uinput.c rawaccel-uinput.h
rawaccel-config.o: rawaccel-config.c rawaccel-config.h rawaccel-device.h rawaccel-types.h
