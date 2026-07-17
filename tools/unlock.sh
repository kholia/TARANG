#!/bin/sh
openocd \
	-f /usr/share/openocd/scripts/interface/stlink.cfg \
	-f /usr/share/openocd/scripts/target/stm32f1x.cfg \
	-c "init" \
	-c "reset halt" \
	-c "stm32f1x unlock 0" \
	-c "reset halt" \
	-c "exit" \
	$1
