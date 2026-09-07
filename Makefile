# traliran-hub — Makefile
# Совместимость: GNU make (любой Linux), clang/gcc, pkg-config/pkgconf, musl/glibc
# Вес: -Os + --gc-sections + -s + LTO (опционально), без -g по умолчанию
# Функции сохранены: all, install, uninstall, clean (+ strip/install-strip/help)

CC         ?= cc
PKG_CONFIG ?= pkg-config
STRIP      ?= strip
INSTALL    ?= install

PREFIX  ?= /usr/local
BINDIR   = $(PREFIX)/bin
DESTDIR ?=

TARGET  = traliran-hub
SRCDIR  = src
VENDOR  = vendor/cjson
OBJDIR  = build

# --- исходники ---
SRCS = $(wildcard $(SRCDIR)/*.c) $(VENDOR)/cJSON.c
OBJS = $(patsubst %.c,$(OBJDIR)/%.o,$(SRCS))
DEPS = $(OBJS:.o=.d)

# --- базовые флаги (переопределяются извне: make CFLAGS=... LDFLAGS=...) ---
CFLAGS  ?= -Os
LDFLAGS ?=
LDLIBS  ?=

# строгий C11, предупреждения, дефайны для Linux/glibc/musl
CFLAGS += -std=c11 -Wall -Wextra -pedantic \
          -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=600 -D_POSIX_C_SOURCE=200809L

# оптимизация под размер (не применяется при DEBUG=1)
ifdef DEBUG
CFLAGS := $(filter-out -Os -O%,$(CFLAGS))
CFLAGS  += -O0 -g3 -fno-omit-frame-pointer
else
CFLAGS  += -DNDEBUG -ffunction-sections -fdata-sections -fvisibility=hidden
LDFLAGS += -Wl,--gc-sections -Wl,-O1 -Wl,--as-needed -Wl,-s
endif

# LTO: включи только если компилятор умеет (make LTO=0 чтобы выключить)
LTO ?= 1
ifeq ($(LTO),1)
ifndef DEBUG
CC_SUPPORTS_LTO := $(shell echo 'int main(){return 0;}' | $(CC) -flto -c -x c -o /dev/null - 2>/dev/null && echo yes)
ifeq ($(CC_SUPPORTS_LTO),yes)
CFLAGS  += -flto
LDFLAGS += -flto
endif
endif
endif

# зависимости для инкрементальной сборки (не влияют на вес)
CFLAGS += -MMD -MP

# --- зависимости: ncursesw -> ncurses fallback, libcurl ---
# pkg-config может отсутствовать (миниконтейнеры) — fallback на -l...
PKG_CFLAGS_NCURSES := $(shell $(PKG_CONFIG) --cflags ncursesw 2>/dev/null || $(PKG_CONFIG) --cflags ncurses 2>/dev/null || echo "")
PKG_LIBS_NCURSES   := $(shell $(PKG_CONFIG) --libs ncursesw 2>/dev/null || $(PKG_CONFIG) --libs ncurses 2>/dev/null || echo "-lncursesw")
PKG_CFLAGS_CURL    := $(shell $(PKG_CONFIG) --cflags libcurl 2>/dev/null || echo "")
PKG_LIBS_CURL      := $(shell $(PKG_CONFIG) --libs libcurl 2>/dev/null || echo "-lcurl")

# только если pkg-config нашёл — добавляем, иначе fallback уже в LIBS
CFLAGS += $(PKG_CFLAGS_NCURSES) $(PKG_CFLAGS_CURL)

# порядок важен: -lncursesw/-lcurl от pkg-config, затем системные
LDLIBS += $(PKG_LIBS_NCURSES) $(PKG_LIBS_CURL) -lpthread -lm

# инклюды проекта
CFLAGS += -I$(VENDOR)

# --- цели ---
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)
ifeq (,$(DEBUG))
	-@$(STRIP) --strip-all $@ 2>/dev/null || $(STRIP) $@ 2>/dev/null || true
endif

$(OBJDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

# авто-зависимости от заголовков
-include $(DEPS)

install: $(TARGET)
	$(INSTALL) -d $(DESTDIR)$(BINDIR)
	$(INSTALL) -m 0755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)

install-strip: $(TARGET)
	$(INSTALL) -d $(DESTDIR)$(BINDIR)
	$(INSTALL) -s -m 0755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET) 2>/dev/null || \
		{ $(INSTALL) -m 0755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET) && $(STRIP) --strip-all $(DESTDIR)$(BINDIR)/$(TARGET) 2>/dev/null || true; }

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)

strip: $(TARGET)
	$(STRIP) --strip-all $(TARGET) 2>/dev/null || $(STRIP) $(TARGET)

clean:
	rm -rf $(OBJDIR) $(TARGET)

help:
	@printf "targets: all install install-strip uninstall strip clean help\n"
	@printf "vars:    CC=$(CC) PREFIX=$(PREFIX) DESTDIR=$(DESTDIR) DEBUG=$(DEBUG) LTO=$(LTO)\n"
	@printf "example: make -j$$(nproc)\n"
	@printf "         make DEBUG=1           # сборка с отладкой (-O0 -g3)\n"
	@printf "         make LTO=0             # без LTO\n"
	@printf "         make PREFIX=/usr install DESTDIR=/tmp/pkg\n"

.PHONY: all install install-strip uninstall strip clean help
.DELETE_ON_ERROR:
.SUFFIXES:
