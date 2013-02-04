/*
 * QEMU Alpha PCI support functions.
 *
 * Some of this isn't very Alpha specific at all.
 *
 * ??? Sparse memory access not implemented.
 */

#include "config.h"
#include "hw/alpha_sys.h"
#include "qemu/log.h"
#include "sysemu/sysemu.h"


/* PCI IO reads/writes, to byte-word addressable memory.  */
/* ??? Doesn't handle multiple PCI busses.  */

static uint64_t bw_io_read(void *opaque, hwaddr addr, unsigned size)
{
    switch (size) {
    case 1:
        return cpu_inb(addr);
    case 2:
        return cpu_inw(addr);
    case 4:
        return cpu_inl(addr);
    }
    abort();
}

static void bw_io_write(void *opaque, hwaddr addr,
                        uint64_t val, unsigned size)
{
    switch (size) {
    case 1:
        cpu_outb(addr, val);
        break;
    case 2:
        cpu_outw(addr, val);
        break;
    case 4:
        cpu_outl(addr, val);
        break;
    default:
        abort();
    }
}

const MemoryRegionOps alpha_pci_bw_io_ops = {
    .read = bw_io_read,
    .write = bw_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

/* PCI config space reads/writes, to byte-word addressable memory.  */
static uint64_t bw_conf1_read(void *opaque, hwaddr addr,
                              unsigned size)
{
    PCIBus *b = opaque;
    return pci_data_read(b, addr, size);
}

static void bw_conf1_write(void *opaque, hwaddr addr,
                           uint64_t val, unsigned size)
{
    PCIBus *b = opaque;
    pci_data_write(b, addr, val, size);
}

const MemoryRegionOps alpha_pci_conf1_ops = {
    .read = bw_conf1_read,
    .write = bw_conf1_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

/* PCI/EISA Interrupt Acknowledge Cycle.  */

static uint64_t iack_read(void *opaque, hwaddr addr, unsigned size)
{
    return pic_read_irq(isa_pic);
}

static void special_write(void *opaque, hwaddr addr,
                          uint64_t val, unsigned size)
{
    qemu_log("pci: special write cycle");
}

const MemoryRegionOps alpha_pci_iack_ops = {
    .read = iack_read,
    .write = special_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};
