FROM almalinux:10

USER root

# Build Dependencies (and dump version to logging)
RUN dnf install -y python3 zlib-devel ncurses-devel readline-devel cairo-devel freeglut-devel \
	mesa-libGLU-devel mesa-libGL-devel libX11-devel libstdc++-devel gcc gcc-c++ make git tcsh \
	zip \
    && echo "### rpm -qa:" \
    && rpm -qa | sort \
    && echo ""
#RUN dnf group install -y "Development Tools"

# Tcl/Tk
WORKDIR /tcl
RUN curl -L https://prdownloads.sourceforge.net/tcl/tcl9.0.1-src.tar.gz | tar --strip-components=1 -xzC . \
    && cd unix \
    && ./configure --prefix=/prefix \
    && make \
    && make install install-libraries install-msgs install-tzdata

WORKDIR /tk
RUN curl -L https://prdownloads.sourceforge.net/tcl/tk9.0.1-src.tar.gz | tar --strip-components=1 -xzC . \
    && cd unix \
    && ./configure --prefix=/prefix --with-tcl=/prefix/lib \
    && make \
    && make install install-libraries

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
    && make database/database.h \
    && make -j$(nproc) \
    && make install

# Produce summary of what was created and confirm their DSOs
RUN echo "### filesystem:" \
    find /prefix -printf "%y/%M/%m %i/%n %l %u/%U %g/%G %s/%b %T+/%T@\t%p\n"; \
    ls -lR /prefix; \
    find /prefix -type f -perm /111 -exec bash -c "echo \#\#\# {}; ldd -v {}" \; 2>/dev/null; \
    for name in libgcc_s libstdc++ libpng liblzma libxml2 libz libcairo libGL libGLU; do \
      find /lib64 /usr/lib64 -maxdepth 2 -name "*.so" -name "${name}*" -exec bash -c "echo \#\#\# {}; ldd -v {}" \; 2>/dev/null; \
    done; \
    echo "###"

WORKDIR /
RUN tar -czf /prefix.tar.gz -C ./prefix .

CMD ["/bin/bash"]
