# Major.minor version of Lua
LUA_VERSION ?= 5.4

# Dir containing lua.h if not in a standard system path
LUA_DIR ?= .

CC= gcc -std=gnu99
INCLUDE = -I$(LUA_DIR) -I$(LUA_DIR)/include -I$(LUA_DIR)/src
CFLAGS = -O2 -Wall -Wextra $(INCLUDE)

INSTALL_PREFIX = /usr/local
INSTALL_BIN_DIR = $(INSTALL_PREFIX)/bin
INSTALL_CMOD_DIR = $(INSTALL_PREFIX)/lib/lua/$(LUA_VERSION)

LFV_SRC = lfv.c
LFVLUA_SRC = lfvlua.c

LFV_DEPS = $(LFV_SRC) lfv.h lfvreader.h
LFVLUA_DEPS = $(LFVLUA_SRC) lfvlua.h lfvreader.h

all: lfv.so lfvutil

clean:
	$(RM) lfv*.o lfv.so lfvutil

install: install_cmodule install_lfvutil

uninstall: uninstall_cmodule uninstall_lfvutil

# Individual (un)install targets
install_cmodule: lfv.so
	install -t $(INSTALL_CMOD_DIR) lfv.so

uninstall_cmodule:
	$(RM) $(INSTALL_CMOD_DIR)/lfv.so

install_lfvutil: lfvutil
	install -t $(INSTALL_BIN_DIR) lfvutil

uninstall_lfvutil:
	$(RM) $(INSTALL_BIN_DIR)/lfvutil

# Dynamic library (to be loaded by Lua)
lfv.so: lfvpic.o lfvluapic.o
	$(CC) -shared -o lfv.so lfvpic.o lfvluapic.o

lfvpic.o: $(LFV_DEPS)
	$(CC) $(CFLAGS) -o lfvpic.o -c -fPIC $(LFV_SRC)

lfvluapic.o: $(LFVLUA_DEPS)
	$(CC) $(CFLAGS) -o lfvluapic.o -c -fPIC $(LFVLUA_SRC)

# Executable utility
lfvutil: lfvutil.o lfv.o
lfvutil.o: lfvutil.c lfv.h
lfv.o: $(LFV_DEPS)
