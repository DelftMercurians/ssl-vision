# Use Ubuntu 22.04 as the base image
FROM ubuntu:22.04

# Install necessary packages
ENV DEBIAN_FRONTEND noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
  xfce4 \
  xfce4-goodies \
  x11vnc \
  xvfb \
  wget \
  net-tools \
  snapd

# Set up Snapd environment
ENV PATH $PATH:/snap/bin

# Install noVNC using Snap
RUN snap install novnc

# Set up VNC
RUN mkdir ~/.vnc
# Set a VNC password here (or use a different method to set/authorize VNC connections)
RUN x11vnc -storepasswd yourpasswordhere ~/.vnc/passwd

# Expose VNC port
EXPOSE 5900

# Expose noVNC web port
EXPOSE 8080

# Set up the default command to start the VNC server, noVNC, and the desktop environment
# Adjust the geometry (screen resolution) as needed
# CMD /bin/bash -c "Xvfb :1 -screen 0 1280x720x16 & xfce4-session & x11vnc -display :1 -usepw -forever & novnc --listen 8080 --vnc localhost:5900 && tail -f /dev/null"
ENTRYPOINT [ "/bin/bash", "-c", "Xvfb :1 -screen 0 1280x720x16 & xfce4-session & x11vnc -display :1 -usepw -forever & novnc --listen 8080 --vnc localhost:5900 && tail -f /dev/null"]
