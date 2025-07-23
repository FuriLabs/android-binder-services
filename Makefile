CC = gcc

CFLAGS = `pkg-config --cflags glib-2.0 gio-2.0 libgbinder`
LDFLAGS = `pkg-config --libs glib-2.0 gio-2.0 libgbinder`

SOURCES_VIBRATOR = vibrator/vibratorhal.c

TARGET_VIBRATOR = android-vibrator

PREFIX ?= /usr

$(TARGET_VIBRATOR): $(SOURCES)
	$(CC) $(SOURCES_VIBRATOR) -o $(TARGET_VIBRATOR) $(CFLAGS) $(LDFLAGS)

install: $(TARGET_VIBRATOR)
	install -d $(DESTDIR)$(PREFIX)/libexec
	install -m 755 $(TARGET_VIBRATOR) $(DESTDIR)$(PREFIX)/libexec/
	install -d $(DESTDIR)$(PREFIX)/lib/systemd/user
	install -m 0644 vibrator/android-vibrator.service $(DESTDIR)$(PREFIX)/lib/systemd/user/

clean:
	rm -f $(TARGET)
