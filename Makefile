# ani2xcursor — build file for Nix derivations (and plain make on any distro).
# For development, xmake is the primary build system.

CXX      ?= c++
CXXFLAGS += -std=c++20 -Wall -Wextra -O2
PREFIX   ?= /usr
VERSION  := $(strip $(shell cat VERSION))
BUILDDIR := build
OBJDIR   := $(BUILDDIR)/obj

SRCS   := $(wildcard src/*.cpp)
OBJS   := $(patsubst src/%.cpp, $(OBJDIR)/%.o, $(SRCS))
TARGET := $(BUILDDIR)/ani2xcursor

PKG_CFLAGS := $(shell pkg-config --cflags spdlog xcursor stb)
PKG_LIBS   := $(shell pkg-config --libs   spdlog xcursor) -lpthread

ALL_CXXFLAGS := $(CXXFLAGS) -Iinclude -DANI2XCURSOR_VERSION=\"$(VERSION)\" $(PKG_CFLAGS)

.PHONY: all install clean

all: $(TARGET)

$(TARGET): $(OBJS) | $(BUILDDIR)
	$(CXX) $(ALL_CXXFLAGS) -o $@ $^ $(PKG_LIBS)

$(OBJDIR)/%.o: src/%.cpp | $(OBJDIR)
	$(CXX) $(ALL_CXXFLAGS) -c -o $@ $<

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(OBJDIR):
	mkdir -p $(OBJDIR)

install: all
	install -Dm755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/ani2xcursor
	install -Dm644 packaging/appimage/ani2xcursor.desktop \
	  $(DESTDIR)$(PREFIX)/share/applications/ani2xcursor.desktop
	install -Dm644 packaging/appimage/ani2xcursor.svg \
	  $(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps/ani2xcursor.svg
	install -Dm644 completions/bash/ani2xcursor \
	  $(DESTDIR)$(PREFIX)/share/bash-completion/completions/ani2xcursor
	install -Dm644 completions/zsh/_ani2xcursor \
	  $(DESTDIR)$(PREFIX)/share/zsh/site-functions/_ani2xcursor
	install -Dm644 completions/fish/ani2xcursor.fish \
	  $(DESTDIR)$(PREFIX)/share/fish/vendor_completions.d/ani2xcursor.fish
	@for po in locale/*.po; do \
	  lang=$$(basename $$po .po); \
	  install -dm755 $(DESTDIR)$(PREFIX)/share/locale/$$lang/LC_MESSAGES; \
	  msgfmt -o $(DESTDIR)$(PREFIX)/share/locale/$$lang/LC_MESSAGES/ani2xcursor.mo $$po; \
	  echo "installed translation: $$lang"; \
	done

clean:
	rm -rf $(OBJDIR) $(TARGET)
