# Automatically generated by make.py from ['rules.py']

_out/pty.cpp.o: pty.cpp
	mkdir -p _out
	g++ -Wall -Wextra -g -pthread -fdiagnostics-color=always -Os -o _out/pty.cpp.o -c pty.cpp -MD -MP

-include _out/pty.cpp.d

_out/util.cpp.o: util.cpp
	mkdir -p _out
	g++ -Wall -Wextra -g -pthread -fdiagnostics-color=always -Os -o _out/util.cpp.o -c util.cpp -MD -MP

-include _out/util.cpp.d

_out/protocol.cpp.o: protocol.cpp
	mkdir -p _out
	g++ -Wall -Wextra -g -pthread -fdiagnostics-color=always -Os -o _out/protocol.cpp.o -c protocol.cpp -MD -MP

-include _out/protocol.cpp.d

_out/base64.c.o: base64.c
	mkdir -p _out
	g++ -Wall -Wextra -g -pthread -fdiagnostics-color=always -Os -o _out/base64.c.o -c base64.c -MD -MP

-include _out/base64.c.d

_out/master.cpp.o: master.cpp
	mkdir -p _out
	g++ -Wall -Wextra -g -pthread -fdiagnostics-color=always -Os -o _out/master.cpp.o -c master.cpp -MD -MP

-include _out/master.cpp.d

_out/slave.cpp.o: slave.cpp
	mkdir -p _out
	g++ -Wall -Wextra -g -pthread -fdiagnostics-color=always -Os -o _out/slave.cpp.o -c slave.cpp -MD -MP

-include _out/slave.cpp.d

_out/doctest.cpp.o: doctest.cpp
	mkdir -p _out
	g++ -Wall -Wextra -g -pthread -fdiagnostics-color=always -Os -o _out/doctest.cpp.o -c doctest.cpp -MD -MP

-include _out/doctest.cpp.d

_out/test_base64.cpp.o: test_base64.cpp
	mkdir -p _out
	g++ -Wall -Wextra -g -pthread -fdiagnostics-color=always -Os -o _out/test_base64.cpp.o -c test_base64.cpp -MD -MP

-include _out/test_base64.cpp.d

pty_proxy_master: _out/pty.cpp.o _out/util.cpp.o _out/protocol.cpp.o _out/base64.c.o _out/master.cpp.o
	g++ -s -pthread -o pty_proxy_master _out/pty.cpp.o _out/util.cpp.o _out/protocol.cpp.o _out/base64.c.o _out/master.cpp.o

pty_proxy_slave: _out/pty.cpp.o _out/util.cpp.o _out/protocol.cpp.o _out/base64.c.o _out/slave.cpp.o
	g++ -s -pthread -o pty_proxy_slave _out/pty.cpp.o _out/util.cpp.o _out/protocol.cpp.o _out/base64.c.o _out/slave.cpp.o

test_base64: _out/test_base64.cpp.o _out/base64.c.o _out/doctest.cpp.o
	g++ -s -pthread -o test_base64 _out/test_base64.cpp.o _out/base64.c.o _out/doctest.cpp.o

all: pty_proxy_master pty_proxy_slave
	true

