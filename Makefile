CC = gcc
CFLAGS = `pkg-config --cflags glib-2.0 gio-2.0 libgbinder`
LDFLAGS = `pkg-config --libs glib-2.0 gio-2.0 libgbinder`
SRC = vibratorhal.c
TARGET = android-vibrator
PREFIX ?= /usr
LIBEXEC_DIR = $(PREFIX)/libexec

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(SRC) -o $(TARGET) $(CFLAGS) $(LDFLAGS)

install: $(TARGET)
	install -d $(DESTDIR)$(LIBEXEC_DIR)
	install -m 755 $(TARGET) $(DESTDIR)$(LIBEXEC_DIR)/$(TARGET)
	install -d $(DESTDIR)$(PREFIX)/lib/systemd/user
	install -m 0644 android-vibrator.service $(DESTDIR)$(PREFIX)/lib/systemd/user

clean:
	rm -f $(TARGET)
