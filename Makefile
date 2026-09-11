PREFIX ?= $(HOME)/.local

bowser: main.go bowser.c bowser.h
	go build -ldflags="-s -w" -o bowser .

install: bowser
	install -Dm755 bowser $(PREFIX)/bin/bowser
	@echo "instalado em $(PREFIX)/bin/bowser"

uninstall:
	rm -f $(PREFIX)/bin/bowser

clean:
	rm -f bowser

.PHONY: install uninstall clean
