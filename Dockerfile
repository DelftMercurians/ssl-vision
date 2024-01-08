FROM dorowu/ubuntu-desktop-lxde-vnc

ENV DEBIAN_FRONTEND noninteractive
WORKDIR /root/vision

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
  libv4l-0 \
  socat \
  unzip

COPY lib/* /usr/lib/
COPY ./GxGigeIPConfig /root/

# Build
COPY . .
RUN cmake -B build -DUSE_DAHENG=true && make -j 8 && make install_test_data

# Change nginx config to listen on 6080 instead of 80
RUN sed -i 's/80/6080/g' /etc/nginx/sites-enabled/default

# Add socat udp tunnel to supervisord.conf
# Receive UDP packets from the multicast address 224.5.23.2:10006 and forward them to the local TCP port 6081
RUN printf "\n\n[program:socat]\npriority=25\ncommand=socat UDP4-RECVFROM:10006,ip-add-membership=224.5.23.2:0.0.0.0 TCP:localhost:6081\n\n" >> /etc/supervisor/conf.d/supervisord.conf

