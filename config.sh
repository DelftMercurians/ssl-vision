#!/bin/bash

# 1. Delete all xml files in the current directory
rm -f /root/vision/*.xml

# 2. Symlink all xml files from ../vision-conf to the current directory
ln -s /root/vision-conf/*.xml /root/vision/
