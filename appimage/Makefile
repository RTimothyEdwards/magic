all: appimage

RESOURCES = $(shell find rsc/ -type f)
ARCH = $(shell uname -i)
APPIMAGE = Magic-$(ARCH).AppImage

prefix.tar.gz: Dockerfile
	docker build -t magic_build -f ./Dockerfile ..
	id=$$(docker create magic_build) ; \
	docker cp $$id:/prefix.tar.gz ./prefix.tar.gz ; \
	docker rm -v $$id

prefix: prefix.tar.gz Makefile
	rm -rf $@
	mkdir -p $@
	tar -xf $< -C $@ .
	mkdir -p $@/lib/tcl8.6.12
	cp -r $@/lib/tcl8.6 $@/lib/tcl8.6.12/library

appimagetool:
	curl -L https://github.com/AppImage/AppImageKit/releases/download/13/appimagetool-x86_64.AppImage > ./appimagetool
	chmod +x ./appimagetool

appimage: $(APPIMAGE)
$(APPIMAGE): prefix appimagetool $(RESOURCES) 
	cp $(RESOURCES) ./prefix
	./appimagetool prefix

PREFIX ?= /usr/local
install:
	install $(APPIMAGE) $(PREFIX)/bin/magic

.PHONY: clean
clean:
	rm -f prefix.tar.gz
	rm -rf prefix