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