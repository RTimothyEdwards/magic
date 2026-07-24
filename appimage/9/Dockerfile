FROM almalinux:9

USER root

ARG TCL_SOURCE=tarball
ARG TCL_VERSION=9.0.4
ARG TCL_REF=core-9-0-4
ARG TCL_TARBALL_URL=https://codeload.github.com/tcltk/tcl/tar.gz/refs/tags/core-9-0-4
ARG TK_TARBALL_URL=https://codeload.github.com/tcltk/tk/tar.gz/refs/tags/core-9-0-4
ARG TCL_TARBALL_SHA256=
ARG TK_TARBALL_SHA256=

# Build Dependencies (and dump version to logging)
RUN dnf install -y python311 zlib-devel ncurses-devel readline-devel cairo-devel freeglut-devel \
	mesa-libGLU-devel mesa-libGL-devel libX11-devel libstdc++-devel gcc gcc-c++ make git tcsh \
	zip \
    && echo "### rpm -qa:" \
    && rpm -qa | sort \
    && echo ""

# LaTeX + dvips (optional).  When present, the default "make" regenerates the
# PostScript documentation from source (configure prints "LaTeX docs: yes");
# otherwise the pre-built PostScript shipped in the source tree is installed.
# Kept non-fatal so an unavailable package can never break the release image --
# the documentation is shipped either way.
RUN dnf install -y texlive-latex texlive-dvips texlive-collection-fontsrecommended \
    || echo "texlive unavailable; shipping the pre-built PostScript documentation"

# Tcl/Tk
WORKDIR /tcl
RUN set -eux; \
    if [ "$TCL_SOURCE" = "github" ]; then \
      git clone --filter=blob:none https://github.com/tcltk/tcl.git .; \
      git checkout --detach "$TCL_REF"; \
    else \
      curl -fsSL "$TCL_TARBALL_URL" -o /tmp/tcl-src.tar.gz; \
      computed_tcl_sha=$(sha256sum /tmp/tcl-src.tar.gz | cut -d' ' -f1); \
      if [ -n "$TCL_TARBALL_SHA256" ]; then \
        echo "$TCL_TARBALL_SHA256  /tmp/tcl-src.tar.gz" | sha256sum --check -; \
      else \
        echo "WARNING: no known SHA for Tcl tarball ($TCL_TARBALL_URL)"; \
        echo "WARNING: actual Tcl tarball SHA256 is $computed_tcl_sha"; \
      fi; \
      tar --strip-components=1 -xzf /tmp/tcl-src.tar.gz -C .; \
    fi; \
    cd unix; \
    ./configure --prefix=/prefix; \
    make; \
    make install install-libraries install-msgs install-tzdata

WORKDIR /tk
RUN set -eux; \
    if [ "$TCL_SOURCE" = "github" ]; then \
      git clone --filter=blob:none https://github.com/tcltk/tk.git .; \
      git checkout --detach "$TCL_REF"; \
    else \
      curl -fsSL "$TK_TARBALL_URL" -o /tmp/tk-src.tar.gz; \
      computed_tk_sha=$(sha256sum /tmp/tk-src.tar.gz | cut -d' ' -f1); \
      if [ -n "$TK_TARBALL_SHA256" ]; then \
        echo "$TK_TARBALL_SHA256  /tmp/tk-src.tar.gz" | sha256sum --check -; \
      else \
        echo "WARNING: no known SHA for Tk tarball ($TK_TARBALL_URL)"; \
        echo "WARNING: actual Tk tarball SHA256 is $computed_tk_sha"; \
      fi; \
      tar --strip-components=1 -xzf /tmp/tk-src.tar.gz -C .; \
    fi; \
    cd unix; \
    ./configure --prefix=/prefix --with-tcl=/prefix/lib; \
    make; \
    make install install-libraries
RUN install -d /prefix/share/licenses/tcl /prefix/share/licenses/tk \
    && install -m 0644 /tcl/license.terms /prefix/share/licenses/tcl/license.terms \
    && install -m 0644 /tk/license.terms /prefix/share/licenses/tk/license.terms

WORKDIR /prefix/bin
RUN cp ./wish9.0 ./wish
RUN cp ./tclsh9.0 ./tclsh

# Magic
WORKDIR /magic
COPY . .

RUN ./configure \
    --prefix=/prefix \
    --with-tcl=/prefix/lib \
    --with-tk=/prefix/lib \
    && make clean \
    && make prepare \
    && make -j$(nproc) \
    && make install \
    && install -d /prefix/share/licenses/magic \
    && install -m 0644 /magic/LICENSE /prefix/share/licenses/magic/LICENSE

# Produce summary of what was created and confirm their DSOs
RUN echo "### filesystem:" \
    find /prefix -printf "%y/%M/%m %i/%n %l %u/%U %g/%G %s/%b %T+/%T@\t%p\n"; \
    ls -lR /prefix; \
    find /prefix -type f -perm /111 -exec bash -c "echo \\#\\#\\# {}; ldd -v {}" \; 2>/dev/null; \
    for name in libgcc_s libstdc++ libpng liblzma libxml2 libz libcairo libGL libGLU; do \
      find /lib64 /usr/lib64 -maxdepth 2 -name "*.so" -name "${name}*" -exec bash -c "echo \\#\\#\\# {}; ldd -v {}" \; 2>/dev/null; \
    done; \
    echo "###"

WORKDIR /
RUN tar -czf /prefix.tar.gz -C ./prefix .

CMD ["/bin/bash"]
