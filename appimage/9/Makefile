MAGIC_SRC_ROOT = ../..
RESOURCES := $(shell find ../rsc/ -type f)
ARCH := $(shell uname -m)
APPIMAGE = Magic-$(ARCH).AppImage
VERSION_MAGIC := $(shell cat $(MAGIC_SRC_ROOT)/VERSION)
VERSION_TSTAMP := $(shell git show -s "--format=%cs" | tr -d '-')
VERSION_HASH := $(shell git show -s "--format=%h")
VERSION_NUM ?= $(VERSION_MAGIC)~$(VERSION_TSTAMP)~$(VERSION_HASH)
VERSION := $(VERSION_NUM)

# Allow CI to override
APPIMAGETOOL_DOWNLOAD_URL ?= https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage

TCL_SOURCE ?= tarball
TCL_VERSION ?= 9.0.4
TCL_REF ?= core-$(subst .,-,$(TCL_VERSION))
TCL_TARBALL_URL ?= https://codeload.github.com/tcltk/tcl/tar.gz/refs/tags/core-$(subst .,-,$(TCL_VERSION))
TK_TARBALL_URL ?= https://codeload.github.com/tcltk/tk/tar.gz/refs/tags/core-$(subst .,-,$(TCL_VERSION))
TCL_TARBALL_SHA256 ?=
TK_TARBALL_SHA256 ?=
TCL_ABI_VERSION := $(shell echo "$(TCL_VERSION)" | cut -d. -f1-2)

all: $(APPIMAGE)

.PHONY: prefix/bin/magic
prefix/bin/magic: Dockerfile Makefile
	@echo "APPIMAGE=$(APPIMAGE)"
	@echo "VERSION=$(VERSION)"
	@echo "ARCH=$(ARCH)"
	@echo "RESOURCES=$(RESOURCES)"
	@echo "TCL_SOURCE=$(TCL_SOURCE)"
	@echo "TCL_VERSION=$(TCL_VERSION)"
	@echo "TCL_REF=$(TCL_REF)"
	rm -rf prefix
	docker build -t magic_build -f ./Dockerfile \
		--build-arg TCL_SOURCE="$(TCL_SOURCE)" \
		--build-arg TCL_VERSION="$(TCL_VERSION)" \
		--build-arg TCL_REF="$(TCL_REF)" \
		--build-arg TCL_TARBALL_URL="$(TCL_TARBALL_URL)" \
		--build-arg TK_TARBALL_URL="$(TK_TARBALL_URL)" \
		--build-arg TCL_TARBALL_SHA256="$(TCL_TARBALL_SHA256)" \
		--build-arg TK_TARBALL_SHA256="$(TK_TARBALL_SHA256)" \
		$(MAGIC_SRC_ROOT)
	id=$$(docker create magic_build) ; \
	docker cp $$id:/prefix ./prefix ; \
	docker rm -v $$id
	mkdir -p prefix/lib/tcl$(TCL_VERSION)
	if [ -d "prefix/lib/tcl$(TCL_ABI_VERSION)" ]; then \
		cp -r "prefix/lib/tcl$(TCL_ABI_VERSION)" "prefix/lib/tcl$(TCL_VERSION)/library"; \
	else \
		echo "Missing expected Tcl ABI directory: prefix/lib/tcl$(TCL_ABI_VERSION)" >&2; \
		exit 1; \
	fi

# Optional: embed AppImage update info (enables delta-update support via zsync).
# Set to e.g. gh-releases-zsync|owner|repo|latest|Magic-*EL9.AppImage.zsync
UPDATEINFORMATION ?=

appimagetool:
	curl -L "$(APPIMAGETOOL_DOWNLOAD_URL)" > ./appimagetool
	chmod +x ./appimagetool

$(APPIMAGE): prefix/bin/magic appimagetool $(RESOURCES)
	cp $(RESOURCES) ./prefix/
	mkdir -p prefix/usr/share/metainfo
	cp ../rsc/magic.metainfo.xml prefix/usr/share/metainfo/org.opencircuitdesign.magic.metainfo.xml
	if [ -n "$(UPDATEINFORMATION)" ]; then \
		./appimagetool --updateinformation "$(UPDATEINFORMATION)" prefix; \
		if command -v zsyncmake >/dev/null 2>&1; then \
			zsyncmake $(APPIMAGE) -u "$(UPDATEINFORMATION)"; \
		fi; \
	else \
		./appimagetool prefix; \
	fi

PREFIX ?= /usr/local
install:
	install $(APPIMAGE) $(PREFIX)/bin/magic

.PHONY: validate
validate: $(APPIMAGE)
	@echo "Validating AppImage: $(APPIMAGE)"
	@if command -v desktop-file-validate >/dev/null 2>&1; then \
		desktop-file-validate prefix/magic.desktop && echo "desktop-file-validate: OK"; \
	else \
		echo "desktop-file-validate: not installed, skipping"; \
	fi
	@if command -v appstreamcli >/dev/null 2>&1; then \
		appstreamcli validate prefix/usr/share/metainfo/org.opencircuitdesign.magic.metainfo.xml && echo "appstreamcli: OK"; \
	else \
		echo "appstreamcli: not installed, skipping"; \
	fi

.PHONY: clean
clean:
	rm -f *.AppImage
	rm -f prefix.tar.gz
	rm -rf prefix
