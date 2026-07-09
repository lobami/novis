CXX = clang++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -Isrc

# All non-main modules are header-only — only main.cpp is compiled. This is
# the whole point of the header-only layout: zero link-time work, and edits
# inside a .h trigger a single recompilation of main.cpp.
#
# zynta_runtime.cpp is the one exception: it provides C-linkage shims
# (extern "C" functions) the evaluator calls into. The .h is still
# header-only for the high-level helpers, but the shims must be
# compiled because `inline` + `extern "C"` doesn't emit a strong symbol.
SRC = src/main.cpp
ifeq ($(wildcard ../zynta/include/zynta_http.h),)
ZYNTA_SRC =
else
ZYNTA_SRC = src/zynta_runtime.cpp \
            ../zynta/src/zynta_json.cpp \
            ../zynta/src/zynta_db.cpp
CXXFLAGS += -I../zynta/include -DNOVIS_HAS_ZYNTA
# SQLite is the default fallback (no extra flags if pkg-config finds it).
ZYNTA_LIBS := $(shell pkg-config --libs sqlite3 2>/dev/null || echo "-lsqlite3")
# Postgres and MySQL are added when their headers are present.
ifeq ($(wildcard /opt/homebrew/opt/libpq/include/libpq-fe.h),)
else
ZYNTA_LIBS += -L/opt/homebrew/opt/libpq/lib -lpq
CXXFLAGS += -I/opt/homebrew/opt/libpq/include -DZYNTA_HAS_POSTGRES
endif
ifeq ($(shell which mysql_config 2>/dev/null),)
else
ZYNTA_LIBS += $(shell mysql_config --libs)
CXXFLAGS += $(shell mysql_config --cflags) -DZYNTA_HAS_MYSQL
endif
endif
OBJ = $(SRC:.cpp=.o) $(ZYNTA_SRC:.cpp=.o)
HDR = $(wildcard src/*.h)
TARGET = novis

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(ZYNTA_LIBS)

$(OBJ): $(HDR)

clean:
	rm -f $(OBJ) $(TARGET)

test: $(TARGET)
	bash tests/run_tests.sh

.PHONY: all clean test
