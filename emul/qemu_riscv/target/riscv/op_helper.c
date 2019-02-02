/*
 * RISC-V Emulation Helpers for QEMU.
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
 * Copyright (c) 2017-2018 SiFive, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "cpu.h"
#include "qemu/main-loop.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"

#ifdef CONFIG_ESESC
#include "esesc_qemu.h"
#include "../libemulint/InstOpcode.h"

#define AtomicAdd(ptr,val) __sync_fetch_and_add(ptr, val)
#define AtomicSub(ptr,val) __sync_fetch_and_sub(ptr, val)

volatile long long int icount = 0;
volatile long long int tcount = 0;

uint64_t esesc_mem_read(uint64_t addr);
uint64_t esesc_mem_read(uint64_t addr) {
  uint64_t buffer;

  CPUState *other_cs = first_cpu;

  CPU_FOREACH(other_cs) {
#ifdef CONFIG_USER_ONLY
    cpu_memory_rw_debug(other_cs, addr, &buffer, 8, 0);
#else
FIXME_NOT_DONE
    // FIXME: pass fid to mem_read and potentially call the system 
    // cpu_physical_memory_read(memaddr, myaddr, length);
#endif

    return buffer;
  }
}

void helper_esesc_dump();
void helper_esesc_dump() {
    CPUState *other_cs = first_cpu;

    CPU_FOREACH(other_cs) {
#ifdef CONFIG_ESESC
        printf("cpuid=%d halted=%d icount=%lld tcount=%lld\n",other_cs->fid, other_cs->halted,icount, tcount);
#endif
    }
}

void helper_esesc_load(CPURISCVState *env, uint64_t pc, uint64_t target, uint64_t data, uint64_t reg) {
  if (icount>0) {
    AtomicSub(&icount,1);
    return;
  }

  CPUState *cpu       = ENV_GET_CPU(env);

  int src1 = reg & 0xFF;
  reg      = reg >> 8;
  reg      = reg >> 8;
  int dest = reg & 0xFF;
#ifdef CONFIG_ESESC
  AtomicAdd(&icount,QEMUReader_queue_load(pc, target, data, cpu->fid, src1, dest));
#endif
}

void helper_esesc_ctrl(CPURISCVState *env, uint64_t pc, uint64_t target, uint64_t op, uint64_t reg) {
  if (icount>0) {
    AtomicSub(&icount,1);
    return;
  }

#ifdef CONFIG_ESESC
  CPUState *cpu       = ENV_GET_CPU(env);

  if (pc == target) {
    printf("jump to itself (terminate) pc:%llx\n",pc);
    QEMUReader_finish(cpu->fid);
    return;
  }

  int src1 = reg & 0xFF;
  reg      = reg >> 8;
  int src2 = reg & 0xFF;
  reg      = reg >> 8;
  int dest = reg & 0xFF;

  AtomicAdd(&icount,QEMUReader_queue_inst(pc, target, cpu->fid, op, src1, src2, dest));
#endif
}

void helper_esesc_alu(CPURISCVState *env, uint64_t pc, uint64_t op, uint64_t reg) {
  if (icount>0) {
    AtomicSub(&icount,1);
    return;
  }

  CPUState *cpu       = ENV_GET_CPU(env);

  int src1 = reg & 0xFF;
  reg      = reg >> 8;
  int src2 = reg & 0xFF;
  reg      = reg >> 8;
  int dest = reg & 0xFF;

#ifdef CONFIG_ESESC
  AtomicAdd(&icount,QEMUReader_queue_inst(pc, 0, cpu->fid, op, src1, src2, dest));
#endif
}
#endif

#ifdef CONFIG_ESESC
void helper_esesc0(CPURISCVState *env)
{
    CPUState *cs = CPU(riscv_env_get_cpu(env));

    icount = 0;
#ifdef CONFIG_ESESC
    QEMUReader_start_roi(cs->fid);
#endif
}
#endif


/* Exceptions processing helpers */
void QEMU_NORETURN riscv_raise_exception(CPURISCVState *env,
                                          uint32_t exception, uintptr_t pc)
{
    CPUState *cs = CPU(riscv_env_get_cpu(env));
    qemu_log_mask(CPU_LOG_INT, "%s: %d\n", __func__, exception);
    cs->exception_index = exception;
    cpu_loop_exit_restore(cs, pc);
}

void helper_raise_exception(CPURISCVState *env, uint32_t exception)
{
    riscv_raise_exception(env, exception, 0);
}

target_ulong helper_csrrw(CPURISCVState *env, target_ulong src,
        target_ulong csr)
{
    target_ulong val = 0;
    if (riscv_csrrw(env, csr, &val, src, -1) < 0) {
#ifdef CONFIG_ESESC
        val = 0;
#else
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
#endif
    }
    return val;
}

target_ulong helper_csrrs(CPURISCVState *env, target_ulong src,
        target_ulong csr, target_ulong rs1_pass)
{
    target_ulong val = 0;
    if (riscv_csrrw(env, csr, &val, -1, rs1_pass ? src : 0) < 0) {
#ifdef CONFIG_ESESC
        val = 0;
#else
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
#endif
    }
    return val;
}

target_ulong helper_csrrc(CPURISCVState *env, target_ulong src,
        target_ulong csr, target_ulong rs1_pass)
{
    target_ulong val = 0;
    if (riscv_csrrw(env, csr, &val, 0, rs1_pass ? src : 0) < 0) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
    }
    return val;
}

#ifndef CONFIG_USER_ONLY

target_ulong helper_sret(CPURISCVState *env, target_ulong cpu_pc_deb)
{
    if (!(env->priv >= PRV_S)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
    }

    target_ulong retpc = env->sepc;
    if (!riscv_has_ext(env, RVC) && (retpc & 0x3)) {
        riscv_raise_exception(env, RISCV_EXCP_INST_ADDR_MIS, GETPC());
    }

    if (env->priv_ver >= PRIV_VERSION_1_10_0 &&
        get_field(env->mstatus, MSTATUS_TSR)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
    }

    target_ulong mstatus = env->mstatus;
    target_ulong prev_priv = get_field(mstatus, MSTATUS_SPP);
    mstatus = set_field(mstatus,
        env->priv_ver >= PRIV_VERSION_1_10_0 ?
        MSTATUS_SIE : MSTATUS_UIE << prev_priv,
        get_field(mstatus, MSTATUS_SPIE));
    mstatus = set_field(mstatus, MSTATUS_SPIE, 0);
    mstatus = set_field(mstatus, MSTATUS_SPP, PRV_U);
    riscv_cpu_set_mode(env, prev_priv);
    env->mstatus = mstatus;

    return retpc;
}

target_ulong helper_mret(CPURISCVState *env, target_ulong cpu_pc_deb)
{
    if (!(env->priv >= PRV_M)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
    }

    target_ulong retpc = env->mepc;
    if (!riscv_has_ext(env, RVC) && (retpc & 0x3)) {
        riscv_raise_exception(env, RISCV_EXCP_INST_ADDR_MIS, GETPC());
    }

    target_ulong mstatus = env->mstatus;
    target_ulong prev_priv = get_field(mstatus, MSTATUS_MPP);
    mstatus = set_field(mstatus,
        env->priv_ver >= PRIV_VERSION_1_10_0 ?
        MSTATUS_MIE : MSTATUS_UIE << prev_priv,
        get_field(mstatus, MSTATUS_MPIE));
    mstatus = set_field(mstatus, MSTATUS_MPIE, 0);
    mstatus = set_field(mstatus, MSTATUS_MPP, PRV_U);
    riscv_cpu_set_mode(env, prev_priv);
    env->mstatus = mstatus;

    return retpc;
}

void helper_wfi(CPURISCVState *env)
{
    CPUState *cs = CPU(riscv_env_get_cpu(env));

    if (env->priv == PRV_S &&
        env->priv_ver >= PRIV_VERSION_1_10_0 &&
        get_field(env->mstatus, MSTATUS_TW)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
    } else {
        cs->halted = 1;
        cs->exception_index = EXCP_HLT;
        cpu_loop_exit(cs);
    }
}

void helper_tlb_flush(CPURISCVState *env)
{
    RISCVCPU *cpu = riscv_env_get_cpu(env);
    CPUState *cs = CPU(cpu);
    if (env->priv == PRV_S &&
        env->priv_ver >= PRIV_VERSION_1_10_0 &&
        get_field(env->mstatus, MSTATUS_TVM)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
    } else {
        tlb_flush(cs);
    }
}

#endif /* !CONFIG_USER_ONLY */
