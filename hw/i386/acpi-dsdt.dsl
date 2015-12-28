/*
 * Bochs/QEMU ACPI DSDT ASL definition
 *
 * Copyright (c) 2006 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

ACPI_EXTRACT_ALL_CODE AcpiDsdtAmlCode

DefinitionBlock (
    "acpi-dsdt.aml",    // Output Filename
    "DSDT",             // Signature
    0x01,               // DSDT Compliance Revision
    "BXPC",             // OEMID
    "BXDSDT",           // TABLE ID
    0x1                 // OEM Revision
    )
{

    Scope(\_SB) {
        Device(PCI0) {
            Name(_HID, EisaId("PNP0A03"))
            Name(_ADR, 0x00)
            Name(_UID, 1)
//#define PX13 S0B_
//            External(PX13, DeviceObj)
        }
    }

/****************************************************************
 * PCI hotplug
 ****************************************************************/

    Scope(\_SB.PCI0) {
        OperationRegion(PCST, SystemIO, 0xae00, 0x08)
        Field(PCST, DWordAcc, NoLock, WriteAsZeros) {
            PCIU, 32,
            PCID, 32,
        }

        OperationRegion(SEJ, SystemIO, 0xae08, 0x04)
        Field(SEJ, DWordAcc, NoLock, WriteAsZeros) {
            B0EJ, 32,
        }

        OperationRegion(BNMR, SystemIO, 0xae10, 0x04)
        Field(BNMR, DWordAcc, NoLock, WriteAsZeros) {
            BNUM, 32,
        }

        /* Lock to protect access to fields above. */
        Mutex(BLCK, 0)

        /* Methods called by bulk generated PCI devices below */

        /* Methods called by hotplug devices */
        Method(PCEJ, 2, NotSerialized) {
            // _EJ0 method - eject callback
            Acquire(BLCK, 0xFFFF)
            Store(Arg0, BNUM)
            Store(ShiftLeft(1, Arg1), B0EJ)
            Release(BLCK)
            Return (0x0)
        }

        /* Hotplug notification method supplied by SSDT */
        External(\_SB.PCI0.PCNT, MethodObj)
    }


/****************************************************************
 * PCI IRQs
 ****************************************************************/

    Scope(\_SB) {
        Scope(PCI0) {
            Method (_PRT, 0) {
                Store(Package(128) {}, Local0)
                Store(Zero, Local1)
                While(LLess(Local1, 128)) {
                    // slot = pin >> 2
                    Store(ShiftRight(Local1, 2), Local2)

                    // lnk = (slot + pin) & 3
                    Store(And(Add(Local1, Local2), 3), Local3)
                    If (LEqual(Local3, 0)) {
                        Store(Package(4) { Zero, Zero, LNKD, Zero }, Local4)
                    }
                    If (LEqual(Local3, 1)) {
                        // device 1 is the power-management device, needs SCI
                        If (LEqual(Local1, 4)) {
                            Store(Package(4) { Zero, Zero, LNKS, Zero }, Local4)
                        } Else {
                            Store(Package(4) { Zero, Zero, LNKA, Zero }, Local4)
                        }
                    }
                    If (LEqual(Local3, 2)) {
                        Store(Package(4) { Zero, Zero, LNKB, Zero }, Local4)
                    }
                    If (LEqual(Local3, 3)) {
                        Store(Package(4) { Zero, Zero, LNKC, Zero }, Local4)
                    }

                    // Complete the interrupt routing entry:
                    //    Package(4) { 0x[slot]FFFF, [pin], [link], 0) }

                    Store(Or(ShiftLeft(Local2, 16), 0xFFFF), Index(Local4, 0))
                    Store(And(Local1, 3),                    Index(Local4, 1))
                    Store(Local4,                            Index(Local0, Local1))

                    Increment(Local1)
                }

                Return(Local0)
            }
        }


        External(PRQ0, FieldUnitObj)
        External(PRQ1, FieldUnitObj)
        External(PRQ2, FieldUnitObj)
        External(PRQ3, FieldUnitObj)

        Method(IQST, 1, NotSerialized) {
            // _STA method - get status
            If (And(0x80, Arg0)) {
                Return (0x09)
            }
            Return (0x0B)
        }
        Method(IQCR, 1, Serialized) {
            // _CRS method - get current settings
            Name(PRR0, ResourceTemplate() {
                Interrupt(, Level, ActiveHigh, Shared) { 0 }
            })
            CreateDWordField(PRR0, 0x05, PRRI)
            If (LLess(Arg0, 0x80)) {
                Store(Arg0, PRRI)
            }
            Return (PRR0)
        }

#define define_link(link, uid, reg)                             \
        Device(link) {                                          \
            Name(_HID, EISAID("PNP0C0F"))                       \
            Name(_UID, uid)                                     \
            Name(_PRS, ResourceTemplate() {                     \
                Interrupt(, Level, ActiveHigh, Shared) {        \
                    5, 10, 11                                   \
                }                                               \
            })                                                  \
            Method(_STA, 0, NotSerialized) {                    \
                Return (IQST(reg))                              \
            }                                                   \
            Method(_DIS, 0, NotSerialized) {                    \
                Or(reg, 0x80, reg)                              \
            }                                                   \
            Method(_CRS, 0, NotSerialized) {                    \
                Return (IQCR(reg))                              \
            }                                                   \
            Method(_SRS, 1, NotSerialized) {                    \
                CreateDWordField(Arg0, 0x05, PRRI)              \
                Store(PRRI, reg)                                \
            }                                                   \
        }

        define_link(LNKA, 0, PRQ0)
        define_link(LNKB, 1, PRQ1)
        define_link(LNKC, 2, PRQ2)
        define_link(LNKD, 3, PRQ3)

        Device(LNKS) {
            Name(_HID, EISAID("PNP0C0F"))
            Name(_UID, 4)
            Name(_PRS, ResourceTemplate() {
                Interrupt(, Level, ActiveHigh, Shared) { 9 }
            })

            // The SCI cannot be disabled and is always attached to GSI 9,
            // so these are no-ops.  We only need this link to override the
            // polarity to active high and match the content of the MADT.
            Method(_STA, 0, NotSerialized) { Return (0x0b) }
            Method(_DIS, 0, NotSerialized) { }
            Method(_CRS, 0, NotSerialized) { Return (_PRS) }
            Method(_SRS, 1, NotSerialized) { }
        }
    }
}
