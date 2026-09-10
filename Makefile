CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -g
SANFLAGS := -fsanitize=address

.PHONY: all sanitize clean

all: server/server client/client

server/server: server/main.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

client/client: client/main.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

sanitize: CXXFLAGS += $(SANFLAGS)
sanitize: clean all

clean:
	rm -f server/server client/client
