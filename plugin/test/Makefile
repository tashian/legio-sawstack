# plugin/test/Makefile — host-side unit tests for plugin-side glue.
# Reuses the firmware harness (test_assert.h) via include path. Build & run:
#   make -C plugin/test
CXX      ?= g++
CXXFLAGS  = -std=c++17 -O2 -Wall -Wextra \
            -I../src -I../../firmware/test
OUT       = build

TESTS = test_note_stack

all: $(addprefix $(OUT)/, $(TESTS))
	@for t in $(TESTS); do \
	  echo "===== $$t ====="; \
	  $(OUT)/$$t || exit 1; \
	done

$(OUT):
	mkdir -p $(OUT)

$(OUT)/test_note_stack: test_note_stack.cpp ../src/note_stack.cpp | $(OUT)
	$(CXX) $(CXXFLAGS) $^ -o $@

clean:
	rm -rf $(OUT)

.PHONY: all clean
