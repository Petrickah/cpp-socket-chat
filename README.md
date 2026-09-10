# cpp-socket-chat

## Overview

A console chat server and client for Linux, written in C++17 on raw POSIX
sockets. The server runs a single-threaded event loop (`select()`/`poll()`,
not thread-per-client) and speaks a real subset of the IRC protocol
(`NICK`/`USER`/`JOIN`/`PART`/`PRIVMSG`/`QUIT`/`PING`/`PONG`, plus the core
numeric replies), validated against a real IRC client (irssi/hexchat/weechat)
rather than only against its own bundled client.

This is currently a scaffold: the build, project layout, and toolchain are in
place, but `server/main.cpp` and `client/main.cpp` are placeholders — no
socket code has been written yet.

## Setup

Requires `g++` (C++17) and `make`. No external dependencies.

```bash
make            # builds server/server and client/client
make sanitize   # rebuilds both with AddressSanitizer (-fsanitize=address)
make clean
```

## Usage

```bash
./server/server
./client/client
```

(Currently prints a placeholder message — real usage lands with the TCP
echo/broadcast implementation.)

