# SPDX-License-Identifier: GPL-2.0
# Copyright(c) 1999 - 2018 Intel Corporation.
#
# Makefile for the Cadence VP6 XRP driver
#
ccflags-y += -I$(srctree)/drivers/xrp/include
ccflags-y += -I$(srctree)/drivers/xrp
#ccflags-y += -DDEBUG
ccflags-y += -Wunused-variable
#ccflags-$(CONFIG_XRP_DEBUG) += -DDEBUG

obj-$(CONFIG_XRP) += xrp.o
#obj-$(CONFIG_XRP_HW_SIMPLE) += xrp_hw_simple.o

xrp-y := xvp_main.o xrp_hw_vic.o xrp_address_map.o xrp_alloc.o xrp_firmware.o
#xrp-$(CONFIG_OF) += xrp_firmware.o
xrp-$(CONFIG_CMA) += xrp_cma_alloc.o
