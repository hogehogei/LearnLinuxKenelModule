#!/bin/bash

insmod sample_character_device_driver.ko
echo "HOGEE" > /dev/pseudo-eep-mem0
echo "HELLO" > /dev/pseudo-eep-mem0
dd if=/dev/pseudo-eep-mem0 of=test.img bs=100 count=11
hexdump -C test.img
rmmod sample_character_device_driver
