#!/bin/sh
set -eu

echo "DRM:"
ls -l /dev/dri 2>/dev/null || true

echo
echo "Input:"
ls -l /dev/input 2>/dev/null || true

echo
echo "Audio:"
ls -l /dev/snd 2>/dev/null || true

echo
echo "GPIO:"
ls -l /dev/gpio* /dev/gpiomem 2>/dev/null || true

echo
echo "I2C/SPI:"
ls -l /dev/i2c-* /dev/spidev* 2>/dev/null || true

echo
echo "Relevant groups:"
for group in cardputer-zero input video audio render gpio spi i2c; do
  getent group "$group" || true
done
