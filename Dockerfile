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
  unzip \
  wget

# Install Rust
RUN curl https://sh.rustup.rs -sSf | sh -s -- -y

# Build Rust bridge and add it to supervisor
COPY bridge ./bridge
RUN cd bridge && ~/.cargo/bin/cargo build --release && cp target/release/bridge /usr/bin/bridge && \
  printf "\n\n[program:bridge]\npriority=25\ncommand=/usr/bin/bridge\n\n" >> /etc/supervisor/conf.d/supervisord.conf

# Install GxGigeSDK
COPY lib/* /usr/lib/
COPY ./GxGigeIPConfig /root/

# Build
COPY . .
RUN cmake -B build -DUSE_DAHENG=true && make -j 8 && make install_test_data

# Change nginx config to listen on 6080 instead of 80
RUN sed -i 's/80/6080/g' /etc/nginx/sites-enabled/default

