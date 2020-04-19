// Contributed by Jose Renau
//                Basilio Fraguela
//                Milos Prvulovic
//
// The ESESC/BSD License
//
// Copyright (c) 2005-2013, Regents of the University of California and
// the ESESC Project.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//   - Redistributions of source code must retain the above copyright notice,
//   this list of conditions and the following disclaimer.
//
//   - Redistributions in binary form must reproduce the above copyright
//   notice, this list of conditions and the following disclaimer in the
//   documentation and/or other materials provided with the distribution.
//
//   - Neither the name of the University of California, Santa Cruz nor the
//   names of its contributors may be used to endorse or promote products
//   derived from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#ifndef _OOOPROCESSOR_H_
#define _OOOPROCESSOR_H_

#include <vector>
#include <algorithm>
#include "nanassert.h"

#include "CodeProfile.h"
#include "FastQueue.h"
#include "FetchEngine.h"
#include "GOoOProcessor.h"
#include "Pipeline.h"
#include "callback.h"
#include "GStats.h"

//#define TRACK_FORWARDING 1
#define TRACK_TIMELEAK 1
//#define ENABLE_LDBP
#define LGT_SIZE 128

class OoOProcessor : public GOoOProcessor {
private:
  class RetireState {
  public:
    double committed;
    Time_t r_dinst_ID;
    Time_t dinst_ID;
    DInst *r_dinst;
    DInst *dinst;
    bool   operator==(const RetireState &a) const {
      return a.committed == committed;
    };
    RetireState() {
      committed  = 0;
      r_dinst_ID = 0;
      dinst_ID   = 0;
      r_dinst    = 0;
      dinst      = 0;
    }
  };

  const bool    MemoryReplay;
  const int32_t RetireDelay;

  FetchEngine IFID;
  PipeQueue   pipeQ;
  LSQFull     lsq;

  uint32_t serialize_level;
  uint32_t serialize;
  int32_t  serialize_for;
  uint32_t forwardProg_threshold;
  DInst *  last_serialized;
  DInst *  last_serializedST;

  int32_t spaceInInstQueue;
  DInst * RAT[LREG_MAX];
  int32_t nTotalRegs;

  DInst *  serializeRAT[LREG_MAX];
  RegType  last_serializeLogical;
  AddrType last_serializePC;

  bool   busy;
  bool   replayRecovering;
  bool   getStatsFlag;
  Time_t replayID;
  bool   flushing;

  FlowID flushing_fid;

  RetireState                                                           last_state;
  void                                                                  retire_lock_check();
  bool                                                                  scooreMemory;
  StaticCallbackMember0<OoOProcessor, &OoOProcessor::retire_lock_check> retire_lock_checkCB;

  void fetch(FlowID fid);

protected:
  ClusterManager clusterManager;

  GStatsAvg avgFetchWidth;
#ifdef TRACK_TIMELEAK
  GStatsAvg  avgPNRHitLoadSpec;
  GStatsHist avgPNRMissLoadSpec;
#endif
#ifdef TRACK_FORWARDING
  GStatsAvg  avgNumSrc;
  GStatsAvg  avgNumDep;
  Time_t     fwdDone[LREG_MAX];
  GStatsCntr fwd0done0;
  GStatsCntr fwd1done0;
  GStatsCntr fwd1done1;
  GStatsCntr fwd2done0;
  GStatsCntr fwd2done1;
  GStatsCntr fwd2done2;
#endif
  CodeProfile codeProfile;
  double      codeProfile_trigger;

  // BEGIN VIRTUAL FUNCTIONS of GProcessor
  bool       advance_clock(FlowID fid);
  StallCause addInst(DInst *dinst);
  void       retire();

  // END VIRTUAL FUNCTIONS of GProcessor
public:
  OoOProcessor(GMemorySystem *gm, CPU_t i);
  virtual ~OoOProcessor();

#ifdef ENABLE_LDBP

  void classify_ld_br_chain(DInst *dinst, RegType br_src1, int reg_flag);

  struct classify_table_entry { //classifies LD-BR chain(32 entries; index -> Dest register)
    classify_table_entry(){
      dest_reg   = LREG_R0;
      ldpc       = 0;
      ld_addr    = 0;
      dep_depth  = 0;
      ldbr_type  = 0;
      ldbr_set   = false;
      simple     = false;
      is_li      = false;
      valid      = false;
      complex_br_set = false;
    }

    RegType dest_reg;
    AddrType ldpc;
    AddrType ld_addr;
    int dep_depth;
    int ldbr_type;
    // 1->simple & trivial & R1           -> src2 == 0 && dep == 1
    // 2->simple & trivial & R2           -> src1 == 0 && dep == 1
    // 3->simple & direct & R1 = ALU      -> src2 == 0 && dep > 1
    // 4->simple & direct & R2 = ALU      -> src1 == 0 && dep > 1
    // 5->simple & direct & R1 = Li       -> src2 == 0 && dep > 1
    // 6->simple & direct & R2 = L1       -> src1 == 0 && dep > 1
    // 7->complex & 1 Li + 1 ALU & R1=Li  -> dep > 1 && R1 == is_li, R2 == ALU
    // 8->complex & 1 Li + 1 ALU & R2=Li  -> dep > 1 && R2 == is_li, R1 == ALU
    // 9->complex & 1 Li + 1 LD & R1=Li   -> dep == 1 && R1 == is_li, R2 == LD
    // 10->complex & 1 Li + 1 LD & R2=Li  -> dep == 1 && R2 == is_li, R1 == LD
    // 11->simple & indirect FIXME???
    bool ldbr_set; //is ldbr set?
    bool simple; //does BR have only one Src operand
    bool is_li; //is one Br data dependent on a Li or Lui instruction
    bool valid;
    bool complex_br_set;

    void ct_load_hit(DInst *dinst) { //add/reset entry on CT
      classify_table_entry(); // reset entries
      dest_reg  = dinst->getInst()->getDst1();
      ldpc      = dinst->getPC();
      ld_addr   = dinst->getAddr();
      dep_depth = 1;
      ldbr_type = 0;
      valid     = true;
      complex_br_set = false;
    }

    void ct_br_hit(DInst *dinst, int reg_flag) {
      simple = false;
      if(ldbr_type > 0)
        ldbr_set = true;
      if(dinst->getInst()->getSrc1() == 0 || dinst->getInst()->getSrc2() == 0) {
        simple = true;
      }
      if(simple) {
        if(dep_depth == 1) {
          ldbr_type = 1;
          if(reg_flag == 2)
            ldbr_type = 2;
        }else if(dep_depth > 1) {
          ldbr_type = 3;
          if(is_li) {
            ldbr_type = 5;
          }
          if(reg_flag == 2) {
            ldbr_type = 4;
            if(is_li)
              ldbr_type = 6;
          }
        }
      }else {
        if(reg_flag == 3) {
          if(dep_depth > 1 && is_li) {
            //ldbr_type      = 0;
            complex_br_set = false;
            return;
          }
          if(dep_depth == 1 && !is_li) {
            ldbr_type = 10;
            complex_br_set = true;
          }else if(dep_depth > 1 && !is_li) {
            ldbr_type = 8;
            complex_br_set = true;
          }
        }else if(reg_flag == 4) {
          //ldbr_type = 0;
          complex_br_set = false;
          if(dep_depth == 1 && !is_li) {
            ldbr_type = 9;
          }else if(dep_depth > 1 && !is_li) {
            ldbr_type = 7;
          }
        }
      }
    }

    void ct_alu_hit(DInst *dinst) {
      //FIXME - check if alu is Li or Lui
      dep_depth++;
    }


  };

  struct load_gen_table_entry {
    load_gen_table_entry() {
      ldpc            = 0;
      start_addr      = 0;
      end_addr        = 0;
      inf_branch      = 0;
      brpc            = 0;
      brop            = ILLEGAL_BR;
      br_data2        = 0;
      br_outcome      = false;
      ldbr_type       = 0;
      ld_delta        = 0;
      prev_delta      = 0;
      ld_conf         = 0;
    }

    AddrType ldpc;
    AddrType start_addr;
    AddrType end_addr;
    uint64_t inf_branch;  //number of inflight branches
    AddrType brpc;
    BrOpType brop;
    DataType br_data2; // Br's operand which is not dependent on LD(could be src1 or src2)
    bool br_outcome;
    int ldbr_type;
    uint64_t ld_delta;
    uint64_t prev_delta;
    uint64_t ld_conf;
    classify_table_entry ct;

    void lgt_br_hit(DInst *dinst, AddrType ld_addr, int ldbr) {
      //if(!ldbr_set) {
      prev_delta = ld_delta;
      ld_delta   = ld_addr - start_addr;
      start_addr = ld_addr;
      if(ld_delta == prev_delta) {
        ld_conf++;
      }else {
        ld_conf = ld_conf / 2;
      }
      ldbr_type  = ldbr;
      lgt_update_br_fields(dinst);
      //ldbr_set = false;
    }

    void lgt_br_miss(DInst *dinst, AddrType _ldpc, AddrType ld_addr, int ldbr) {
      ldpc       = _ldpc;
      start_addr = ld_addr;
      ldbr_type  = ldbr;
      lgt_update_br_fields(dinst);
      //MSG("LGT_BR_MISS clk=%u ldpc=%llx ld_addr=%u del=%u prev_del=%u conf=%u brpc=%llx ldbr=%d", globalClock, ldpc, start_addr, ld_delta, prev_delta, ld_conf, brpc, ldbr_type);
    }

    void lgt_update_br_fields(DInst *dinst) {
      brpc            = dinst->getPC();
      inf_branch      = dinst->getInflight(); //FIXME use dinst->getInflight() instead of variable
    }

  };

  std::vector<classify_table_entry> ct_table = std::vector<classify_table_entry>(32);
  std::vector<load_gen_table_entry> lgt_table = std::vector<load_gen_table_entry>(LGT_SIZE);

  MemObj *DL1;
  AddrType ldbp_brpc;
  AddrType ldbp_ldpc;
  AddrType ldbp_curr_addr;
  AddrType ldbp_start_addr;
  AddrType ldbp_end_addr;
  uint64_t ldbp_delta;
  int brpc_count; //MATCH THIS COUNT WITH VEC_INDEX
  uint64_t inflight_branch;
  bool ldbp_reset;
  Time_t avg_mem_lat;
  Time_t total_mem_lat;
  Time_t max_mem_lat;
  Time_t last_mem_lat;
  Time_t diff_mem_lat;
  int num_mem_lat;
  std::vector<Time_t> mem_lat_vec = std::vector<Time_t>(10);

  //std::vector<int> ldbp_vector = std::vector<int>(LDBP_VEC_SIZE);

  void generate_trigger_load(DInst *dinst, RegType reg, int lgt_index);
  void ldbp_reset_field(){
    ldbp_curr_addr      = 0;
    ldbp_start_addr      = 0;
    ldbp_end_addr        = 0;
    ldbp_delta           = 0;
    ldbp_brpc            = 0;
    ldbp_ldpc            = 0;
    ldbp_reset           = true;
    brpc_count           = 0;

    //for(int i = 0; i < LDBP_VEC_SIZE; i++) {
    //  ldbp_vector[i] = 0;
    //}
  }

#endif

  void executing(DInst *dinst);
  void executed(DInst *dinst);
  LSQ *getLSQ() {
    return &lsq;
  }
  void replay(DInst *target);
  bool isFlushing() {
    return flushing;
  }
  bool isReplayRecovering() {
    return replayRecovering;
  }
  Time_t getReplayID() {
    return replayID;
  }

  void dumpROB();

  bool isSerializing() const {
    return serialize_for != 0;
  }
};

#endif
