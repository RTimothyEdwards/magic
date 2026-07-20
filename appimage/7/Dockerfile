FROM centos/python-38-centos7:20210726-fad62e9

USER root

# CentOS7 went EOL on June 30, 2024 this builds out of vault.centos.org
RUN ls -l /etc/yum.repos.d/ \
    && cp /etc/yum.repos.d/CentOS-Base.repo /tmp/CentOS-Base.repo.old \
    && sed -e 's/mirror.centos.org/vault.centos.org/g' -i /etc/yum.repos.d/*.repo \
    && sed -e 's/^#.*baseurl=http/baseurl=http/g'      -i /etc/yum.repos.d/*.repo \
    && sed -e 's/^mirrorlist=http/#mirrorlist=http/g'  -i /etc/yum.repos.d/*.repo \
    && diff -u /tmp/CentOS-Base.repo.old /etc/yum.repos.d/CentOS-Base.repo; \
    yum clean all \
    && yum -y update \
    && rm -f /tmp/CentOS-Base.repo.old

ARG TCL_SOURCE=tarball
ARG TCL_VERSION=8.6.18
ARG TCL_REF=core-8-6-18
ARG TCL_TARBALL_URL=https://codeload.github.com/tcltk/tcl/tar.gz/refs/tags/core-8-6-18
ARG TK_TARBALL_URL=https://codeload.github.com/tcltk/tk/tar.gz/refs/tags/core-8-6-18
ARG TCL_TARBALL_SHA256=
ARG TK_TARBALL_SHA256=

# Build Dependencies (and dump version to logging)
# git is required to support TCL_SOURCE=github overrides from workflow_dispatch.
RUN yum install -y cairo-devel freeglut-devel gcc make git tcsh \
    && echo "### rpm -qa:" \
    && rpm -qa | sort \
    && echo ""

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
    make install

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
    make install

WORKDIR /prefix/bin
RUN cp ./wish8.6 ./wish
RUN cp ./tclsh8.6 ./tclsh

# Magic
WORKDIR /magic
COPY . .

RUN ./configure \
    --prefix=/prefix \
    --with-tcl=/prefix/lib \
    --with-tk=/prefix/lib \
    --without-opengl \
    && make clean \
    && make database/database.h \
    && make -j$(nproc) \
    && make install

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
