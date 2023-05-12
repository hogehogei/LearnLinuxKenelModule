#!/bin/bash

sudo bash -c 'echo i2c_bme280 0x76 > /sys/bus/i2c/devices/i2c-1/new_device'