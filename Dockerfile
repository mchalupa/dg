FROM ubuntu:20.04

RUN set -e

# Setup time-zone so that the build does not hang
# on configuring the tzdata package.
# I work in Brno, that is basically Vienna-North :)
# (definitely its closer than Prague)
ENV TZ=Europe/Vienna
RUN ln -snf "/usr/share/zoneinfo/$TZ" /etc/localtime
RUN echo "$TZ" > /etc/timezone

# Install packages
RUN apt-get update
RUN apt-get install -y git cmake make llvm zlib1g-dev clang g++ python3

WORKDIR /opt
RUN git clone https://github.com/mchalupa/dg
WORKDIR /opt/dg
RUN cmake . -DCMAKE_INSTALL_PREFIX=/usr
RUN make -j2
RUN make check -j2
RUN make install
