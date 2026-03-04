# ani2xcursor — build file for Nix derivations (and plain make on any distro).
# For development, xmake is the primary build system.

CXX      ?= c++
CXXFLAGS ?= -std=c++20 -Wall -Wextra -O2
PREFIX   ?= /usr

SRCS   := $(wildcard src/*.cpp)
OBJS   := $(patsubst src/%.cpp, _build/%.o, $(SRCS))
TARGET := _build/ani2xcursor

PKG_CFLAGS := $(shell pkg-config --cflags spdlog xcursor)
PKG_LIBS   := $(shell pkg-config --libs   spdlog xcursor) -lpthread

ALL_CXXFLAGS := $(CXXFLAGS) -Iinclude $(PKG_CFLAGS)

.PHONY: all install clean

all: $(TARGET)

$(TARGET): $(OBJS) | _build
	$(CXX) $(ALL_CXXFLAGS) -o $@ $^ $(PKG_LIBS)

_build/%.o: src/%.cpp | _build
	$(CXX) $(ALL_CXXFLAGS) -c -o $@ $<

_build:
	mkdir -p _build

install: all
	install -Dm755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/ani2xcursor
	@for po in locale/*.po; do \
	  lang=$$(basename $$po .po); \
	  install -dm755 $(DESTDIR)$(PREFIX)/share/locale/$$lang/LC_MESSAGES; \
	  msgfmt -o $(DESTDIR)$(PREFIX)/share/locale/$$lang/LC_MESSAGES/ani2xcursor.mo $$po; \
	  echo "installed translation: $$lang"; \
	done

clean:
	rm -rf _build
