# we just hijack the host build system because it's the easiest way...
#
# Xentium kernels are built as executables, with library functions
# explicitly listed as object dependencies. The executables are later collected
# in scripts/link-flightos.sh and added to the embedded AR image.
#
# The address of the .text section for the first xentium kernel is determined
# by the confiuration in dsp/xentium/sysroot/lib/default.ld, all subsequent
# programs will start after the _end symbol of the previous
#
# To achieve this, list dsp kernel targets in hostprogs-y and configure targets
# so that the next target depends on the finalised previous target and take
# the value of the _end symbol as the new .text start
#
# This isn't a pretty solution, but that's what you get for not supporting
# relocations. It works for now and it is at least slightly more comfortable
# than configuring all text base addresses by hand
#


# kbuild trick to avoid linker error. Can be omitted if a module is built.
obj- := dummy.o


HOSTCC := xentium-clang
HOSTLD := xentium-ld

HOSTCFLAGS := -Wall -Wmissing-prototypes -Wstrict-prototypes -O2
HOSTCFLAGS +=-Idsp/xentium/include
HOSTCFLAGS += $(KERNELINCLUDE)

HOSTLDFLAGS += -Tdsp/xentium/sysroot/lib/default.ld
HOSTLDFLAGS += --sysroot=dsp/xentium/sysroot

xen_libs:
xen_libs-objs := lib/xen_printf.o
xen_libs-objs += lib/xen.o
xen_libs-objs += lib/dma.o
xen_libs-objs += lib/kmem.o
xen_libs-objs += ../../lib/data_proc_task.o

xen_dummy.xen : xen_libs
HOSTLOADLIBES_xen_dummy.xen :=
xen_dummy.xen-objs := kernel/dummy/xen_dummy.o $(xen_libs-objs)
hostprogs-y := xen_dummy.xen


# XXX using the .S file works, but make throws a message; needs to address that at some point
xen_rampfit.xen : xen_dummy.xen xen_libs
HOSTLOADLIBES_xen_rampfit.xen := -Ttext $$(readelf -s dsp/xentium/xen_dummy.xen|grep -w _end |awk '{print $$2}')
xen_rampfit.xen-objs := kernel/rampfit/xen_rampfit.o $(xen_libs-objs) kernel/rampfit/xen_asm_rampfit.S
hostprogs-y += xen_rampfit.xen

xen_deglitch.xen : xen_rampfit.xen xen_libs
HOSTLOADLIBES_xen_deglitch.xen := -Ttext $$(readelf -s dsp/xentium/xen_rampfit.xen|grep -w _end |awk '{print $$2}')
xen_deglitch.xen-objs := kernel/deglitch/xen_deglitch.o $(xen_libs-objs)
hostprogs-y += xen_deglitch.xen

xen_stack.xen : xen_deglitch.xen xen_libs
HOSTLOADLIBES_xen_stack.xen := -Ttext $$(readelf -s dsp/xentium/xen_deglitch.xen|grep -w _end |awk '{print $$2}')
xen_stack.xen-objs := kernel/stack/xen_stack.o $(xen_libs-objs)
hostprogs-y += xen_stack.xen




always := $(hostprogs-y)
