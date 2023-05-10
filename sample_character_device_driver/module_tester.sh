#!/bin/bash

insmod sample_character_device_driver.ko
dd if=/dev/pseudo-eep-mem0 of=test.img bs=1 count=1
rmmod sample_character_device_driver
