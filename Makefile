CXX = clang++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -Isrc

# All non-main modules are header-only — only main.cpp is compiled. This is
# the whole point of the header-only layout: zero link-time work, and edits
# inside a .h trigger a single recompilation of main.cpp.
SRC = src/main.cpp
HDR = $(wildcard src/*.h)
OBJ = $(SRC:.cpp=.o)
TARGET = novis

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(OBJ): $(HDR)

clean:
	rm -f $(OBJ) $(TARGET)

test: $(TARGET)
	bash tests/run_tests.sh

.PHONY: all clean test