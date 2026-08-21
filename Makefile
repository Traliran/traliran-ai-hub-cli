CC      ?= cc
PREFIX  ?= /usr/local
BINDIR   = $(PREFIX)/bin

TARGET   = traliran-hub
SRCDIR   = src
VENDOR   = vendor/cjson
OBJDIR   = build

SRCS = $(wildcard $(SRCDIR)/*.c) $(VENDOR)/cJSON.c
OBJS = $(patsubst %.c,$(OBJDIR)/%.o,$(SRCS))

CFLAGS   ?= -O2 -g
CFLAGS   += -std=c11 -Wall -Wextra -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=600 -I$(VENDOR)
CFLAGS   += $(shell pkg-config --cflags ncursesw libcurl 2>/dev/null || echo "-I/usr/include/ncursesw -I/usr/include")
LDLIBS   += $(shell pkg-config --libs ncursesw libcurl 2>/dev/null || echo "-lncursesw -lcurl")
LDLIBS   += -lpthread -lm

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

$(OBJDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -m 0755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)

clean:
	rm -rf $(OBJDIR) $(TARGET)

.PHONY: all install uninstall clean