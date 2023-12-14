FROM dorowu/ubuntu-desktop-lxde-vnc

ENV DEBIAN_FRONTEND noninteractive
WORKDIR /vision

# Install necessary packages
ENV DEBIAN_FRONTEND noninteractive
RUN curl -sSL https://dl.google.com/linux/linux_signing_key.pub | apt-key add  && \
  apt-get update && \
  apt-get install -y \
  g++ cmake \
  libeigen3-dev freeglut3-dev libopencv-dev \
  qtdeclarative5-dev libqt5multimedia5 \
  protobuf-compiler libprotobuf-dev \
  libdc1394-22 libdc1394-22-dev \
  libv4l-0

COPY lib/* /usr/lib/

# Build
COPY . .
RUN cmake -B build -DUSE_DAHENG=true && make -j 8

# Change nginx config to listen on 6080 instead of 80
RUN sed -i 's/80/6080/g' /etc/nginx/sites-enabled/default
