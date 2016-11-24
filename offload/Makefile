#
##//===----------------------------------------------------------------------===//
#//
#//                     The LLVM Compiler Infrastructure
#//
#// This file is dual licensed under the MIT and the University of Illinois Open
#// Source Licenses. See LICENSE.txt for details.
#//
#//===----------------------------------------------------------------------===//
#

# MAKEFILE PARAMETERS
#
# root_dir - path to root directory of liboffload
# build_dir - path to build directory
# mpss_dir - path to root directory of mpss
# coi_header_dir - path to COI headers
# coi_target_dir - path to libcoi_device.so
# coi_host_dir - path to libcoi_host.so
# myo_header_dir - path to MYO headers
# myo_target_dir - path to libmyo-service.so
# libiomp_host_dir - path to host libiomp directory (unnecessary if compiler_host is icc)
# libiomp_target_dir - path to target libiomp directory (unnecesarry if compiler_target is icc)
# omp_header_dir - path to omp.h (unnecessary if compiler_host and compiler_target are icc)
# os_host - host operating system
# os_target - target operating system
# compiler_host - host compiler
# compiler_target - target compiler
# options_host - additional options for host compiler
# options_target - additional options for target compiler
#

# Directories
root_dir?=.
build_dir?=$(root_dir)/build
build_host_dir=$(build_dir)/host
build_target_dir=$(build_dir)/target
obj_host_dir=$(build_dir)/obj_host
obj_target_dir=$(build_dir)/obj_target
source_dir=$(root_dir)/src
imported_dir=$(source_dir)/imported
install_dir?=$(build_dir)

# OS
os_host?=linux
os_target?=linux
ifneq ($(os_host)_$(os_target), linux_linux)
  $(error "Only linux is supported")
endif

# Compilers
compiler_host?=gcc
compiler_target?=gcc

# MPSS
mpss_dir?=/opt/mpss/3.5
mpss_present=$(shell if test -d $(mpss_dir); then echo OK; else echo KO; fi)
ifneq ($(mpss_present), OK)
  $(error "Cannot find MPSS directory $(mpss_dir)")
endif

# COI
coi_header_dir?=$(mpss_dir)/sysroots/k1om-mpss-linux/usr/include/intel-coi
coi_header_dir_present=$(shell if test -d $(coi_header_dir); then echo OK; else echo KO; fi)
ifneq ($(coi_header_dir_present), OK)
  $(error "Cannot find COI include directory $(coi_header_dir)")
endif

coi_target_dir?=$(mpss_dir)/sysroots/k1om-mpss-linux/usr/lib64
coi_target_dir_present=$(shell if test -e $(coi_target_dir)/libcoi_device.so; then echo OK; else echo KO; fi)
ifneq ($(coi_target_dir_present), OK)
  $(error "Cannot find $(coi_target_dir)/libcoi_device.so")
endif

coi_host_dir?=/opt/intel/mic/coi/host-linux-release/lib
coi_host_dir_present=$(shell if test -e $(coi_host_dir)/libcoi_host.so; then echo OK; else echo KO; fi)
ifneq ($(coi_host_dir_present), OK)
  $(error "Cannot find $(coi_host_dir)/libcoi_host.so")
endif

# MYO
myo_header_dir?=$(mpss_dir)/sysroots/k1om-mpss-linux/usr/include
myo_header_dir_present=$(shell if test -d $(myo_header_dir); then echo OK; else echo KO; fi)
ifneq ($(myo_header_dir_present), OK)
  $(error "Cannot find MYO include directory $(myo_header_dir)")
endif

myo_target_dir?=$(mpss_dir)/sysroots/k1om-mpss-linux/usr/lib64
myo_target_dir_present=$(shell if test -e $(myo_target_dir)/libmyo-service.so; then echo OK; else echo KO; fi)
ifneq ($(myo_target_dir_present), OK)
  $(error "Cannot find $(myo_target_dir)/libmyo-service.so")
endif

# Sources
src_liboffload_common=dv_util.cpp liboffload_error.c liboffload_msg.c offload_common.cpp offload_table.cpp offload_trace.cpp offload_util.cpp

src_liboffload_host=$(src_liboffload_common) cean_util.cpp coi/coi_client.cpp compiler_if_host.cpp offload_engine.cpp offload_env.cpp offload_host.cpp offload_omp_host.cpp offload_timer_host.cpp offload_orsl.cpp orsl-lite/lib/orsl-lite.c offload_myo_host.cpp  offload_extract.cpp
src_liboffload_host:=$(foreach file,$(src_liboffload_host),$(source_dir)/$(file))
src_liboffload_host+=ompt_host.cpp ompt_buffer_host.cpp

src_liboffload_target=$(src_liboffload_common) coi/coi_server.cpp compiler_if_target.cpp offload_omp_target.cpp offload_target.cpp offload_timer_target.cpp offload_myo_target.cpp
src_liboffload_target+=ompt_target.cpp ompt_buffer_target.cpp
src_liboffload_target:=$(foreach file,$(src_liboffload_target),$(source_dir)/$(file))

src_ofld=ofldbegin.cpp ofldend.cpp
src_ofld:=$(foreach file,$(src_ofld),$(source_dir)/$(file))

headers=$(wildcard $(source_dir)/*.h) $(wildcard $(source_dir)/coi/*.h) $(wildcard $(source_dir)/orsl-lite/include/*.h)
ifneq ($(omp_header_dir), )
  headers+=$(imported_dir)/omp.h
endif

# Objects
obj_liboffload_host=$(notdir $(src_liboffload_host))
obj_liboffload_host:=$(obj_liboffload_host:.cpp=.o)
obj_liboffload_host:=$(obj_liboffload_host:.c=.o)
obj_liboffload_host:=$(foreach file,$(obj_liboffload_host),$(obj_host_dir)/$(file))

obj_liboffload_target=$(notdir $(src_liboffload_target))
obj_liboffload_target:=$(obj_liboffload_target:.cpp=.o)
obj_liboffload_target:=$(obj_liboffload_target:.c=.o)
obj_liboffload_target:=$(foreach file,$(obj_liboffload_target),$(obj_target_dir)/$(file))

obj_ofld=$(notdir $(src_ofld))
obj_ofld:=$(obj_ofld:.cpp=.o)
obj_ofld_host=$(foreach file,$(obj_ofld),$(build_host_dir)/$(file))
obj_ofld_target=$(foreach file,$(obj_ofld),$(build_target_dir)/$(file))

# Options
opts_common=-O2 -w -fpic -c -DCOI_LIBRARY_VERSION=2 -DMYO_SUPPORT -DOFFLOAD_DEBUG=1 -DSEP_SUPPORT -DTIMING_SUPPORT -I$(coi_include) -I$(myo_dir)/include -I$(source_dir) -I$(install_dir)/include
CFLAGS=-O0 -g -w -fpic -c -DCOI_LIBRARY_VERSION=2 -DMYO_SUPPORT -DOFFLOAD_DEBUG=1 -DSEP_SUPPORT -DTIMING_SUPPORT -I$(coi_header_dir) -I$(myo_header_dir) -I$(source_dir) -I$(install_dir)/include
ifneq ($(omp_header_dir), )
  CFLAGS+=-I$(imported_dir)
endif
CXXFLAGS=$(CFLAGS) -std=c++0x

opts_liboffload=-shared -Wl,-soname,liboffload.so.5 -ldl -liomp5

opts_liboffload_host=$(opts_liboffload) -L$(coi_host_dir) -lcoi_host -L$(myo_dir)/lib -lmyo-client
ifneq ($(libiomp_host_dir), )
  opts_liboffload_host+=-L$(libiomp_host_dir)
endif

opts_liboffload_target=$(opts_liboffload) -L$(coi_target_dir) -L$(myo_target_dir) -lcoi_device -lmyo-service
ifneq ($(libiomp_target_dir), )
  opts_liboffload_target+=-L$(libiomp_target_dir)
endif

options_host?=
opts_host=$(options_host) -DHOST_LIBRARY=1
ifeq ($(os_host), linux)
  opts_host+=-DLINUX
  opts_host+=-DOMPT_SUPPORT
endif

options_target?=
opts_target=$(options_target) -DHOST_LIBRARY=0
ifeq ($(os_target), linux)
  opts_target+=-DLINUX
  opts_target+=-DOMPT_SUPPORT
endif
ifeq ($(compiler_target), icc)
  opts_target+=-mmic
endif


# Make targets
.PHONY: all clean info

all: info $(build_host_dir)/liboffload.so $(build_target_dir)/liboffload.so $(obj_ofld_host) $(obj_ofld_target)


$(build_host_dir)/liboffload.so: $(build_host_dir)/liboffload.so.5 | $(build_host_dir)
	ln -f $< $@

$(build_host_dir)/liboffload.so.5: $(obj_liboffload_host) | $(build_host_dir)
	$(compiler_host) $(opts_liboffload_host) $(opts_host) $^ -o $@

$(obj_host_dir)/%.o: $(source_dir)/%.c $(headers) | $(obj_host_dir)
	$(compiler_host) $(CFLAGS) $(opts_host) $< -o $@

$(obj_host_dir)/%.o: $(source_dir)/%.cpp $(headers) | $(obj_host_dir)
	$(compiler_host) $(CXXFLAGS) $(opts_host) $< -o $@

$(obj_host_dir)/%.o: $(source_dir)/coi/%.cpp $(headers) | $(obj_host_dir)
	$(compiler_host) $(CXXFLAGS) $(opts_host) $< -o $@

$(obj_host_dir)/%.o: $(source_dir)/orsl-lite/lib/%.c $(headers) | $(obj_host_dir)
	$(compiler_host) $(CFLAGS) $(opts_host) $< -o $@


$(build_target_dir)/liboffload.so: $(build_target_dir)/liboffload.so.5 | $(build_target_dir)
	ln -f $< $@

$(build_target_dir)/liboffload.so.5: $(obj_liboffload_target) | $(build_target_dir)
	$(compiler_target) $(opts_liboffload_target) $(opts_target) $^ -o $@

$(obj_target_dir)/%.o: $(source_dir)/%.c $(headers) | $(obj_target_dir)
	$(compiler_target) $(CFLAGS) $(opts_target) $< -o $@

$(obj_target_dir)/%.o: $(source_dir)/%.cpp $(headers) | $(obj_target_dir)
	$(compiler_target) $(CXXFLAGS) $(opts_target) $< -o $@

$(obj_target_dir)/%.o: $(source_dir)/coi/%.cpp $(headers) | $(obj_target_dir)
	$(compiler_target) $(CXXFLAGS) $(opts_target) $< -o $@

$(obj_target_dir)/%.o: $(source_dir)/orsl-lite/lib/%.c $(headers) | $(obj_target_dir)
	$(compiler_target) $(CFLAGS) $(opts_target) $< -o $@


$(build_host_dir)/%.o: $(source_dir)/%.cpp $(headers) | $(build_host_dir)
	$(compiler_host) $(CXXFLAGS) $(opts_host) $< -o $@

$(build_target_dir)/%.o: $(source_dir)/%.cpp $(headers) | $(build_target_dir)
	$(compiler_target) $(CXXFLAGS) $(opts_target) $< -o $@


$(imported_dir)/omp.h: $(omp_header_dir)/omp.h | $(imported_dir)
	cp $< $@


$(build_host_dir) $(build_target_dir) $(obj_host_dir) $(obj_target_dir): | $(build_dir)
	$(shell mkdir -p $@ >/dev/null 2>/dev/null)
	@echo "Created $@ directory"

$(build_dir):
	$(shell mkdir -p $@ >/dev/null 2>/dev/null)
	@echo "Created $@ directory"

$(imported_dir):
	$(shell mkdir -p $@ >/dev/null 2>/dev/null)
	@echo "Created $@ directory"


clean:
	$(shell rm -rf $(build_dir))
	@echo "Remove $(build_dir) directory"


info:
	@echo "root_dir = $(root_dir)"
	@echo "build_dir = $(build_dir)"
	@echo "mpss_dir = $(mpss_dir)"
	@echo "coi_header_dir = $(coi_header_dir)"
	@echo "coi_target_dir = $(coi_target_dir)"
	@echo "myo_header_dir = $(myo_header_dir)"
	@echo "myo_target_dir = $(myo_target_dir)"
	@echo "libiomp_host_dir = $(libiomp_host_dir)"
	@echo "libiomp_target_dir = $(libiomp_target_dir)"
	@echo "omp_header_dir = $(omp_header_dir)"
	@echo "os_host = $(os_host)"
	@echo "os_target = $(os_target)"
	@echo "compiler_host = $(compiler_host)"
	@echo "compiler_target = $(compiler_target)"
	@echo "options_host = $(options_host)"
	@echo "options_target = $(options_target)"

install: $(build_host_dir)/liboffload.so $(build_host_dir)/liboffload.so.5 $(build_target_dir)/liboffload.so $(build_target_dir)/liboffload.so.5
	mkdir -p $(install_dir)/lib/intel64
	mkdir -p $(install_dir)/lib/mic
	cp $(build_target_dir)/liboffload.so $(install_dir)/lib/mic
	cp $(build_target_dir)/liboffload.so.5 $(install_dir)/lib/mic
	cp $(build_host_dir)/liboffload.so $(install_dir)/lib/intel64
	cp $(build_host_dir)/liboffload.so.5 $(install_dir)/lib/intel64
