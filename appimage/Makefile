RESOURCES = $(shell find rsc/ -type f)
ARCH = $(shell uname -m)
APPIMAGE = Magic-$(ARCH).AppImage

all: $(APPIMAGE)

.PHONY: prefix/bin/magic
prefix/bin/magic: Dockerfile Makefile
	rm -rf prefix
	docker build -t magic_build -f ./Dockerfile ..
	id=$$(docker create magic_build) ; \
	docker cp $$id:/prefix ./prefix ; \
	docker rm -v $$id
	mkdir -p prefix/lib/tcl8.6.12
	cp -r prefix/lib/tcl8.6 prefix/lib/tcl8.6.12/library

appimagetool:
	curl -L https://github.com/AppImage/AppImageKit/releases/download/13/appimagetool-x86_64.AppImage > ./appimagetool
	chmod +x ./appimagetool

$(APPIMAGE): prefix/bin/magic appimagetool $(RESOURCES) 
	cp $(RESOURCES) ./prefix
	./appimagetool prefix

PREFIX ?= /usr/local
install:
	install $(APPIMAGE) $(PREFIX)/bin/magic

.PHONY: clean
clean:
	rm -f *.AppImage
	rm -f prefix.tar.gz
	rm -rf prefix