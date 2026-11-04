CXX ?= c++
CXXFLAGS ?= -O3 -Wall -Wextra -Wpedantic
SOURCES = src/main.cpp src/compiler.cpp src/runtime.cpp src/artifact.cpp src/native.cpp
TARGET ?= tacqor

.PHONY: all test
all: $(TARGET)

$(TARGET): $(SOURCES) src/tacqor.hpp src/values.hpp src/embedded_runtime.inc
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -std=c++17 -pthread $(SOURCES) $(LDFLAGS) -o $@

# The native backend embeds the runtime headers into the tacqor executable as string chunks.
# The generated file is committed so that build-windows.cmd works without this rule.
src/embedded_runtime.inc: src/tacqor.hpp src/values.hpp
	rm -rf .embed && mkdir .embed
	{ grep -v '#pragma once' src/tacqor.hpp; grep -v -e '#include "tacqor.hpp"' -e '#pragma once' src/values.hpp; } > .embed/all.txt
	split -b 12000 .embed/all.txt .embed/chunk_
	for f in .embed/chunk_*; do printf 'R"tacqor_runtime('; cat "$$f"; printf ')tacqor_runtime",\n'; done > $@
	rm -rf .embed

test: $(TARGET)
	sh tests/smoke.sh ./$(TARGET)
