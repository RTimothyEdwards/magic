FROM centos/python-38-centos7:20210726-fad62e9

USER root

# Build Dependencies
RUN yum install -y cairo-devel freeglut-devel gcc make tcsh

# Tcl/Tk 
WORKDIR /tcl
RUN curl -L https://prdownloads.sourceforge.net/tcl/tcl8.6.12-src.tar.gz | tar --strip-components=1 -xzC . \
    && cd unix \
    && ./configure --prefix=/prefix \
    && make \
    && make install

WORKDIR /tk
RUN curl -L https://prdownloads.sourceforge.net/tcl/tk8.6.12-src.tar.gz | tar --strip-components=1 -xzC . \
    && cd unix \
    && ./configure --prefix=/prefix --with-tcl=/prefix/lib\
    && make \
    && make install

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

WORKDIR /
RUN tar -czf /prefix.tar.gz -C ./prefix .

CMD ["/bin/bash"]