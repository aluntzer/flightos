CHECKFLAGS     += -D__sparc__

extra-$(CONFIG_ARCH_CUSTOM_BOOT_CODE) += kernel.lds

obj-$(CONFIG_ARCH_CUSTOM_BOOT_CODE) += ttable.o
obj-$(CONFIG_ARCH_CUSTOM_BOOT_CODE) += head.o
obj-$(CONFIG_ARCH_CUSTOM_BOOT_CODE) += win_uflow.o
obj-$(CONFIG_ARCH_CUSTOM_BOOT_CODE) += win_oflow.o
obj-$(CONFIG_ARCH_CUSTOM_BOOT_CODE) += flush_windows.o
obj-$(CONFIG_ARCH_CUSTOM_BOOT_CODE) += etrap.o
obj-$(CONFIG_ARCH_CUSTOM_BOOT_CODE) += rtrap.o
obj-$(CONFIG_ARCH_CUSTOM_BOOT_CODE) += irqtrap.o
obj-$(CONFIG_ARCH_CUSTOM_BOOT_CODE) += thread.o
obj-$(CONFIG_ARCH_CUSTOM_BOOT_CODE) += mna_trap.o
obj-$(CONFIG_ARCH_CUSTOM_BOOT_CODE) += unaligned_memory_access.o
obj-$(CONFIG_ARCH_CUSTOM_BOOT_CODE) += write_buffer_error_trap.o
obj-$(CONFIG_ARCH_CUSTOM_BOOT_CODE) += write_buffer_error.o


obj-y += setup.o
obj-y += init.o
obj-y += page.o
obj-y += bootmem.o
obj-y += mm.o
obj-$(CONFIG_MMU) += mmu.o
obj-y += elf.o
obj-y += module.o
obj-y += traps.o
obj-y += stacktrace.o
obj-y += stack.o
obj-y += traps/data_access_exception_trap.o
obj-y += traps/data_access_exception.o
obj-y += traps/floating_point_exception_trap.o
obj-y += traps/fpe.o
obj-y += irq.o
obj-y += smp.o
obj-y += time.o
obj-y += clockevent.o
obj-y += edac.o


