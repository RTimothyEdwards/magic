FROM donnio/python3:centos6

# Tcl/Tk 
WORKDIR /tcl
RUN curl -L https://prdownloads.sourceforge.net/tcl/tcl8.6.12-src.tar.gz | tar --strip-components=1 -xzC . \
    && cd unix \
    && ./configure --prefix=/prefix \
    && make -j$(nproc) \
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

# Cairo
RUN yum install -y cairo-devel

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

WORKDIR /
RUN tar -czf /prefix.tar.gz -C ./prefix .

CMD ["/bin/bash"]