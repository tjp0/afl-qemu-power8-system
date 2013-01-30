/*
 * QEMU S/390 CPU
 *
 * Copyright (c) 2009 Ulrich Hecht
 * Copyright (c) 2011 Alexander Graf
 * Copyright (c) 2012 SUSE LINUX Products GmbH
 * Copyright (c) 2012 IBM Corp.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 * Contributions after 2012-12-11 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "cpu.h"
#include "qemu-common.h"
#include "qemu/timer.h"
#ifndef CONFIG_USER_ONLY
#include "hw/hw.h"
#include "sysemu/arch_init.h"
#endif

#define CR0_RESET       0xE0UL
#define CR14_RESET      0xC2000000UL;

/* generate CPU information for cpu -? */
void s390_cpu_list(FILE *f, fprintf_function cpu_fprintf)
{
#ifdef CONFIG_KVM
    (*cpu_fprintf)(f, "s390 %16s\n", "host");
#endif
}

#ifndef CONFIG_USER_ONLY
CpuDefinitionInfoList *arch_query_cpu_definitions(Error **errp)
{
    CpuDefinitionInfoList *entry;
    CpuDefinitionInfo *info;

    info = g_malloc0(sizeof(*info));
    info->name = g_strdup("host");

    entry = g_malloc0(sizeof(*entry));
    entry->value = info;

    return entry;
}
#endif

/* CPUClass::reset() */
static void s390_cpu_reset(CPUState *s)
{
    S390CPU *cpu = S390_CPU(s);
    S390CPUClass *scc = S390_CPU_GET_CLASS(cpu);
    CPUS390XState *env = &cpu->env;

    if (qemu_loglevel_mask(CPU_LOG_RESET)) {
        qemu_log("CPU Reset (CPU %d)\n", s->cpu_index);
        log_cpu_state(env, 0);
    }

    s390_del_running_cpu(cpu);

    scc->parent_reset(s);

    memset(env, 0, offsetof(CPUS390XState, breakpoints));

    /* architectured initial values for CR 0 and 14 */
    env->cregs[0] = CR0_RESET;
    env->cregs[14] = CR14_RESET;
    /* set halted to 1 to make sure we can add the cpu in
     * s390_ipl_cpu code, where env->halted is set back to 0
     * after incrementing the cpu counter */
#if !defined(CONFIG_USER_ONLY)
    env->halted = 1;
#endif
    tlb_flush(env, 1);
}

#if !defined(CONFIG_USER_ONLY)
static void s390_cpu_machine_reset_cb(void *opaque)
{
    S390CPU *cpu = opaque;

    cpu_reset(CPU(cpu));
}
#endif

static void s390_cpu_initfn(Object *obj)
{
    S390CPU *cpu = S390_CPU(obj);
    CPUS390XState *env = &cpu->env;
    static int cpu_num = 0;
#if !defined(CONFIG_USER_ONLY)
    struct tm tm;
#endif

    cpu_exec_init(env);
#if !defined(CONFIG_USER_ONLY)
    qemu_register_reset(s390_cpu_machine_reset_cb, cpu);
    qemu_get_timedate(&tm, 0);
    env->tod_offset = TOD_UNIX_EPOCH +
                      (time2tod(mktimegm(&tm)) * 1000000000ULL);
    env->tod_basetime = 0;
    env->tod_timer = qemu_new_timer_ns(vm_clock, s390x_tod_timer, cpu);
    env->cpu_timer = qemu_new_timer_ns(vm_clock, s390x_cpu_timer, cpu);
    /* set env->halted state to 1 to avoid decrementing the running
     * cpu counter in s390_cpu_reset to a negative number at
     * initial ipl */
    env->halted = 1;
#endif
    env->cpu_num = cpu_num++;
    env->ext_index = -1;

    cpu_reset(CPU(cpu));
}

static void s390_cpu_finalize(Object *obj)
{
#if !defined(CONFIG_USER_ONLY)
    S390CPU *cpu = S390_CPU(obj);

    qemu_unregister_reset(s390_cpu_machine_reset_cb, cpu);
#endif
}

static void s390_cpu_class_init(ObjectClass *oc, void *data)
{
    S390CPUClass *scc = S390_CPU_CLASS(oc);
    CPUClass *cc = CPU_CLASS(scc);

    scc->parent_reset = cc->reset;
    cc->reset = s390_cpu_reset;
}

static const TypeInfo s390_cpu_type_info = {
    .name = TYPE_S390_CPU,
    .parent = TYPE_CPU,
    .instance_size = sizeof(S390CPU),
    .instance_init = s390_cpu_initfn,
    .instance_finalize = s390_cpu_finalize,
    .abstract = false,
    .class_size = sizeof(S390CPUClass),
    .class_init = s390_cpu_class_init,
};

static void s390_cpu_register_types(void)
{
    type_register_static(&s390_cpu_type_info);
}

type_init(s390_cpu_register_types)
