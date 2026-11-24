CC ?= gcc
PKG_CONFIG ?= pkg-config

HAVE_PC := $(shell $(PKG_CONFIG) --exists libcurl sqlite3 libcjson libcrypto 2>/dev/null && echo 1 || echo 0)

ifeq ($(HAVE_PC),1)
CPPFLAGS := $(shell $(PKG_CONFIG) --cflags libcurl sqlite3 libcjson libcrypto) -Iproto
LDFLAGS := $(shell $(PKG_CONFIG) --libs-only-L libcurl sqlite3 libcjson libcrypto)
LDLIBS := $(shell $(PKG_CONFIG) --libs-only-l libcurl sqlite3 libcjson libcrypto) -lm
else
CURL_INC := /nix/store/awayiin721c5khfrn2vqx1cnb6rjswg0-curl-8.14.1-dev/include
CURL_LIB := /nix/store/g3vj4z918zal30klpdd9papkavis01sh-curl-8.14.1/lib

SQLITE_INC := /nix/store/w1cmsd5kg0jsj76cxj1vgfrsw3gk86yh-sqlite-3.51.2-dev/include
SQLITE_LIB := /nix/store/12cm2m4pdfanj5li94hwxpdfzrfdfngj-sqlite-3.51.2/lib

CJSON_INC := /nix/store/f055ibhb3if5qdvwfdqym4g90fjrdy62-cjson-1.7.19/include
CJSON_LIB := /nix/store/f055ibhb3if5qdvwfdqym4g90fjrdy62-cjson-1.7.19/lib

OPENSSL_INC := /nix/store/i0jqva96qfgc76g8w7jbyiv6h3si07b9-openssl-3.6.2-dev/include
OPENSSL_LIB := /nix/store/l0vl4dali2mvbpi30a8da1f71jl85myg-openssl-3.6.2/lib

CPPFLAGS := -I$(CURL_INC) -I$(SQLITE_INC) -I$(CJSON_INC) -I$(OPENSSL_INC) -Iproto
LDFLAGS := -L$(CURL_LIB) -L$(SQLITE_LIB) -L$(CJSON_LIB) -L$(OPENSSL_LIB)
LDLIBS := -lcurl -lsqlite3 -lcjson -lcrypto -lm
endif

CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter

SRC := src/auth.c src/http.c src/capture.c src/main.c
OBJ := $(SRC:.c=.o)
TARGET := dpsk_capture

GROK_SRC := src/grok_ui.c src/grok_agent.c
GROK_OBJ := $(GROK_SRC:.c=.o)
GROK_TARGET := grok_ui_agent

.PHONY: all clean run

all: $(TARGET) $(GROK_TARGET)

$(TARGET): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

$(GROK_TARGET): $(GROK_OBJ)
	$(CC) -o $@ $(GROK_OBJ)

src/%.o: src/%.c src/*.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ) $(GROK_OBJ) $(TARGET) $(GROK_TARGET)

run: $(TARGET)
	./$(TARGET)
