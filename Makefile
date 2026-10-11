CXX ?= g++
CC ?= gcc

INCLUDES = -I. -Iboilerplate
CXXFLAGS ?= -O2 -Wall -Wextra -std=c++17 $(INCLUDES)
CFLAGS ?= -O2 -Wall -Wextra $(INCLUDES)

PKG_CFLAGS = $(shell pkg-config --cflags wayland-client wayland-egl egl glesv2)
PKG_LIBS = $(shell pkg-config --libs wayland-client wayland-egl egl glesv2) -lm

WAYLAND_PROTOCOLS_DIR = /usr/share/wayland-protocols

DRIVER_SRCS = vangoghflow_wayland.cpp
PLUGIN_SRCS = $(wildcard plugins/*/*.cpp)

PROTO_C_SRCS = boilerplate/wlr-layer-shell-unstable-v1-protocol.c \
               boilerplate/xdg-shell-protocol.c

OBJS = $(DRIVER_SRCS:.cpp=.o) \
       $(PLUGIN_SRCS:.cpp=.o) \
       $(PROTO_C_SRCS:.c=.o)

TARGET = vangoghflow_wayland

all: $(TARGET)

boilerplate/xdg-shell-client-protocol.h: $(WAYLAND_PROTOCOLS_DIR)/stable/xdg-shell/xdg-shell.xml
	wayland-scanner client-header $< $@

boilerplate/xdg-shell-protocol.c: $(WAYLAND_PROTOCOLS_DIR)/stable/xdg-shell/xdg-shell.xml
	wayland-scanner private-code $< $@

boilerplate/wlr-layer-shell-unstable-v1-client-protocol.h: boilerplate/wlr-layer-shell-unstable-v1.xml
	wayland-scanner client-header $< $@

boilerplate/wlr-layer-shell-unstable-v1-protocol.c: boilerplate/wlr-layer-shell-unstable-v1.xml
	wayland-scanner private-code $< $@

boilerplate/%.o: boilerplate/%.c boilerplate/wlr-layer-shell-unstable-v1-client-protocol.h boilerplate/xdg-shell-client-protocol.h
	$(CC) $(CFLAGS) $(PKG_CFLAGS) -c $< -o $@

plugins/%.o: plugins/%.cpp plugin.hpp
	$(CXX) $(CXXFLAGS) $(PKG_CFLAGS) -c $< -o $@

%.o: %.cpp plugin.hpp boilerplate/wlr-layer-shell-unstable-v1-client-protocol.h boilerplate/xdg-shell-client-protocol.h
	$(CXX) $(CXXFLAGS) $(PKG_CFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) $(PKG_LIBS) -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
