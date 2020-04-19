
//                Smruti Sarangi
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

#ifndef BPRED_H
#define BPRED_H

/*
 * The original Branch predictor code was inspired in simplescalar-3.0, but
 * heavily modified to make it more OOO. Now, it supports more branch predictors
 * that the standard simplescalar distribution.
 *
 * Supported branch predictors models:
 *
 * Oracle, NotTaken, Taken, 2bit, 2Level, 2BCgSkew
 *
 */

#include <iostream>
#include <deque>
#include <map>
#include <vector>
#include <algorithm>

#include "estl.h"
#include "nanassert.h"

#include "CacheCore.h"
#include "DInst.h"
#include "DOLC.h"
#include "GStats.h"
#include "SCTable.h"

#define RAP_T_NT_ONLY 1
#define DOC_SIZE 128
enum PredType { CorrectPrediction = 0, NoPrediction, NoBTBPrediction, MissPrediction };
enum BrOpType { BEQ = 0, BNE = 1, BLT = 4, BGE = 5, BLTU = 6, BGEU = 7, ILLEGAL_BR = 8};

class MemObj;

class BPred {
public:
  typedef int64_t HistoryType;
  class Hash4HistoryType {
  public:
    size_t operator()(const HistoryType &addr) const {
      return ((addr) ^ (addr >> 16));
    }
  };

  HistoryType calcHist(AddrType pc) const {
    HistoryType cid = pc >> 2; // psudo-PC works, no need lower 2 bit

    // Remove used bits (restrict predictions per cycle)
    cid = cid >> addrShift;
    // randomize it
    cid = (cid >> 17) ^ (cid);

    return cid;
  }

protected:
  const int32_t id;

  GStatsCntr nHit;  // N.B. predictors should not update these counters directly
  GStatsCntr nMiss; // in their predict() function.

  int32_t addrShift;
  int32_t maxCores;

public:
  BPred(int32_t i, const char *section, const char *sname, const char *name);
  virtual ~BPred();

  virtual PredType predict(DInst *dinst, bool doUpdate, bool doStats) = 0;
  virtual void     fetchBoundaryBegin(DInst *dinst); // If the branch predictor support fetch boundary model, do it
  virtual void     fetchBoundaryEnd();               // If the branch predictor support fetch boundary model, do it

  PredType doPredict(DInst *dinst, bool doStats = true) {
    PredType pred = predict(dinst, true, doStats);
    if(pred == NoPrediction)
      return pred;

    if(dinst->getInst()->isJump())
      return pred;

    nHit.inc(pred == CorrectPrediction && dinst->getStatsFlag() && doStats);
    nMiss.inc(pred == MissPrediction && dinst->getStatsFlag() && doStats);

    return pred;
  }

  void update(DInst *dinst) {
    predict(dinst, true, false);
  }
};

class BPRas : public BPred {
private:
  const uint16_t RasSize;
  const uint16_t rasPrefetch;

  AddrType *stack;
  int32_t   index;

protected:
public:
  BPRas(int32_t i, const char *section, const char *sname);
  ~BPRas();
  PredType predict(DInst *dinst, bool doUpdate, bool doStats);

  void tryPrefetch(MemObj *il1, bool doStats, int degree);
};

class BPBTB : public BPred {
private:
  GStatsCntr nHitLabel; // hits to the icache label (ibtb)
  DOLC *     dolc;
  bool       btbicache;
  uint64_t   btbHistorySize;

  class BTBState : public StateGeneric<AddrType> {
  public:
    BTBState(int32_t lineSize) {
      inst = 0;
    }

    AddrType inst;

    bool operator==(BTBState s) const {
      return inst == s.inst;
    }
  };

  typedef CacheGeneric<BTBState, AddrType> BTBCache;

  BTBCache *data;

protected:
public:
  BPBTB(int32_t i, const char *section, const char *sname, const char *name = 0);
  ~BPBTB();

  PredType predict(DInst *dinst, bool doUpdate, bool doStats);
  void     updateOnly(DInst *dinst);
};

class BPOracle : public BPred {
private:
  BPBTB btb;

protected:
public:
  BPOracle(int32_t i, const char *section, const char *sname)
      : BPred(i, section, sname, "Oracle")
      , btb(i, section, sname) {
  }

  PredType predict(DInst *dinst, bool doUpdate, bool doStats);
};

class BPNotTaken : public BPred {
private:
  BPBTB btb;

protected:
public:
  BPNotTaken(int32_t i, const char *section, const char *sname)
      : BPred(i, section, sname, "NotTaken")
      , btb(i, section, sname) {
    // Done
  }

  PredType predict(DInst *dinst, bool doUpdate, bool doStats);
};

class BPMiss : public BPred {
private:
protected:
public:
  BPMiss(int32_t i, const char *section, const char *sname)
      : BPred(i, section, sname, "Miss") {
    // Done
  }

  PredType predict(DInst *dinst, bool doUpdate, bool doStats);
};

class BPNotTakenEnhanced : public BPred {
private:
  BPBTB btb;

protected:
public:
  BPNotTakenEnhanced(int32_t i, const char *section, const char *sname)
      : BPred(i, section, sname, "NotTakenEnhanced")
      , btb(i, section, sname) {
    // Done
  }

  PredType predict(DInst *dinst, bool doUpdate, bool doStats);
};

class BPTaken : public BPred {
private:
  BPBTB btb;

protected:
public:
  BPTaken(int32_t i, const char *section, const char *sname)
      : BPred(i, section, sname, "Taken")
      , btb(i, section, sname) {
    // Done
  }

  PredType predict(DInst *dinst, bool doUpdate, bool doStats);
};

class BP2bit : public BPred {
private:
  BPBTB btb;

  SCTable table;

protected:
public:
  BP2bit(int32_t i, const char *section, const char *sname);

  PredType predict(DInst *dinst, bool doUpdate, bool doStats);
};

class IMLIBest;

class BPIMLI : public BPred {
private:
  BPBTB btb;

  IMLIBest *imli;

  const bool FetchPredict;
  bool       dataHistory;

protected:
public:
  BPIMLI(int32_t i, const char *section, const char *sname);

  void     fetchBoundaryBegin(DInst *dinst);
  void     fetchBoundaryEnd();
  PredType predict(DInst *dinst, bool doUpdate, bool doStats);
};

class BP2level : public BPred {
private:
  BPBTB btb;

  const uint16_t l1Size;
  const uint32_t l1SizeMask;

  const uint16_t    historySize;
  const HistoryType historyMask;

  SCTable globalTable;

  DOLC         dolc;
  HistoryType *historyTable; // LHR
  bool         useDolc;

protected:
public:
  BP2level(int32_t i, const char *section, const char *sname);
  ~BP2level();

  PredType predict(DInst *dinst, bool doUpdate, bool doStats);
};

class BPHybrid : public BPred {
private:
  BPBTB btb;

  const uint16_t    historySize;
  const HistoryType historyMask;

  SCTable globalTable;

  HistoryType ghr; // Global History Register

  SCTable localTable;
  SCTable metaTable;

protected:
public:
  BPHybrid(int32_t i, const char *section, const char *sname);
  ~BPHybrid();

  PredType predict(DInst *dinst, bool doUpdate, bool doStats);
};

class BP2BcgSkew : public BPred {
private:
  BPBTB btb;

  SCTable BIM;

  SCTable           G0;
  const uint16_t    G0HistorySize;
  const HistoryType G0HistoryMask;

  SCTable           G1;
  const uint16_t    G1HistorySize;
  const HistoryType G1HistoryMask;

  SCTable           metaTable;
  const uint16_t    MetaHistorySize;
  const HistoryType MetaHistoryMask;

  HistoryType history;

protected:
public:
  BP2BcgSkew(int32_t i, const char *section, const char *sname);
  ~BP2BcgSkew();

  PredType predict(DInst *dinst, bool doUpdate, bool doStats);
};

class BPyags : public BPred {
private:
  BPBTB btb;

  const uint16_t    historySize;
  const HistoryType historyMask;

  SCTable table;
  SCTable ctableTaken;
  SCTable ctableNotTaken;

  HistoryType ghr; // Global History Register

  uint8_t *   CacheTaken;
  HistoryType CacheTakenMask;
  HistoryType CacheTakenTagMask;

  uint8_t *   CacheNotTaken;
  HistoryType CacheNotTakenMask;
  HistoryType CacheNotTakenTagMask;

protected:
public:
  BPyags(int32_t i, const char *section, const char *sname);
  ~BPyags();

  PredType predict(DInst *dinst, bool doUpdate, bool doStats);
};

class BPOgehl : public BPred {
private:
  BPBTB btb;

  const int32_t mtables;
  const int32_t glength;
  const int32_t nentry;
  const int32_t addwidth;
  int32_t       logpred;

  int32_t THETA;
  int32_t MAXTHETA;
  int32_t THETAUP;
  int32_t PREDUP;

  int64_t *ghist;
  int32_t *histLength;
  int32_t *usedHistLength;

  int32_t *T;
  int32_t  AC;
  uint8_t  miniTag;
  uint8_t *MINITAG;

  uint8_t genMiniTag(const DInst *dinst) const {
    AddrType t = dinst->getPC() >> 2;
    return (uint8_t)((t ^ (t >> 3)) & 3);
  }

  char ** pred;
  int32_t TC;

protected:
  int32_t geoidx(uint64_t Add, int64_t *histo, int32_t m, int32_t funct);

public:
  BPOgehl(int32_t i, const char *section, const char *sname);
  ~BPOgehl();

  PredType predict(DInst *dinst, bool doUpdate, bool doStats);
};

class LoopPredictor {
private:
  struct LoopEntry {
    uint64_t tag;
    uint32_t iterCounter;
    uint32_t currCounter;
    int32_t  confidence;
    uint8_t  dir;

    LoopEntry() {
      tag         = 0;
      confidence  = 0;
      iterCounter = 0;
      currCounter = 0;
    }
  };

  const uint32_t nentries;
  LoopEntry *    table; // TODO: add assoc, now just BIG

public:
  LoopPredictor(int size);
  void update(uint64_t key, uint64_t tag, bool taken);

  bool     isLoop(uint64_t key, uint64_t tag) const;
  bool     isTaken(uint64_t key, uint64_t tag, bool taken);
  uint32_t getLoopIter(uint64_t key, uint64_t tag) const;
};


class BPTData : public BPred {
private:
  BPBTB btb;

  SCTable tDataTable;

  struct tDataTableEntry {
    tDataTableEntry() {
      tag = 0;
      ctr = 0;
    }

    AddrType tag;
    int8_t   ctr;
  };

  HASH_MAP<AddrType, tDataTableEntry> tTable;

protected:
public:
  BPTData(int32_t i, const char *section, const char *sname);
  ~BPTData() {
  }

  PredType predict(DInst *dinst, bool doUpdate, bool doStats);
};

/*LOAD BRANCH PREDICTOR (LDBP)*/
class BPLdbp : public BPred {
  private:
    BPBTB btb;
    SCTable ldbp_table;
    std::map<uint64_t, bool> ldbp_map;

    struct ldbp_table{
      ldbp_table(){
        tag = 0;
        ctr = 0;
      }

      AddrType tag;
      int8_t ctr;
    };

    
  public:
    BPLdbp(int32_t i, const char *section, const char *sname);
    ~BPLdbp(){}

    PredType predict(DInst *dinst, bool doUpdate, bool doStats);
    bool outcome_calculator(BrOpType br_op, uint64_t br_data1, uint64_t br_data2);
    BrOpType branch_type(AddrType brpc);

    struct data_outcome_correlator {
      
      data_outcome_correlator() {
        tag    = 0;
        taken  = 0;
        ntaken = 0;
      }
      AddrType tag;
      int taken;  //taken counter
      int ntaken; // not taken counter
      
      int update_doc(AddrType _tag, bool outcome) { 
        int max_counter = 8;
        //extract current outcome here
        int conf_pred = 0;
        
        if(tag != 0) {
          tag = _tag;
          conf_pred = doc_compute();
        }
        //this if loop updates DOC counters
        if(outcome == true) {
          if(taken < max_counter)
            taken++;
          else if(ntaken > 0)
            ntaken--;
        }else {
          if(ntaken < max_counter)
            ntaken++;
          else if(taken > 0)
            taken--;
        }
        return conf_pred;
      }

      int doc_compute() {
        double m = 0;
        m        = (double)(1 + std::min(taken, ntaken)) / (double)(2 + taken + ntaken);
        if(m <= 1/3) {
          if(taken > ntaken)
            return 2; //predict taken
          return 1; //predict not taken
        }
        return 0; // no prediction - not confident enough
      }
    };

    //std::vector<data_outcome_correlator> doc_table = std::vector<data_outcome_correlator>(DOC_SIZE);
    std::vector<data_outcome_correlator> doc_table;
    
    int outcome_doc(AddrType _tag, bool outcome) {
      int empty_slot = -1;
      for(int i = 0; i < DOC_SIZE; i++) {
        if(doc_table[i].tag == _tag) {
          MSG("DOC hit");
          return doc_table[i].update_doc(_tag, outcome);
        }
        //if(doc_table[i].tag == 0 && empty_slot == -1)
        //  empty_slot = i;
      }
      //if no tag hit, update table and return NoPrediction
      doc_table.erase(doc_table.begin());
      doc_table.push_back(data_outcome_correlator());
      doc_table[DOC_SIZE - 1].tag = _tag;
      return doc_table[DOC_SIZE - 1].update_doc(0, outcome);
    }

};

class BPredictor {
private:
  const int32_t id;
  const bool    SMTcopy;
  MemObj *      il1; // For prefetch

  BPRas *ras;
  BPred *pred1;
  BPred *pred2;
  BPred *pred3;
  BPred *meta;

  int32_t FetchWidth;
  int32_t bpredDelay1;
  int32_t bpredDelay2;
  int32_t bpredDelay3;

  bool       Miss_Pred_Bool;
  GStatsCntr nBTAC;

  GStatsCntr nBranches;
  GStatsCntr nTaken;
  GStatsCntr nMiss; // hits == nBranches - nMiss

  GStatsCntr nBranches2;
  GStatsCntr nTaken2;
  GStatsCntr nMiss2; // hits == nBranches - nMiss

  GStatsCntr nBranches3;
  GStatsCntr nTaken3;
  GStatsCntr nMiss3; // hits == nBranches - nMiss

  GStatsCntr nFixes1;
  GStatsCntr nFixes2;
  GStatsCntr nFixes3;
  GStatsCntr nUnFixes;
  GStatsCntr nAgree3;

protected:
  PredType predict1(DInst *dinst);
  PredType predict2(DInst *dinst);
  PredType predict3(DInst *dinst);

public:
  BPredictor(int32_t i, MemObj *il1, BPredictor *bpred = 0);
  ~BPredictor();

  static BPred *getBPred(int32_t id, const char *sname, const char *sec);

  void        fetchBoundaryBegin(DInst *dinst);
  void        fetchBoundaryEnd();
  TimeDelta_t predict(DInst *dinst, bool *fastfix);
  bool        Miss_Prediction(DInst *dinst);
  void        dump(const char *str) const;

  void set_Miss_Pred_Bool() {
    Miss_Pred_Bool = 1; // Correct_Prediction==0 in enum before
  }
  void unset_Miss_Pred_Bool() {
    Miss_Pred_Bool = 0; // Correct_Prediction==0 in enum before
  }

  bool get_Miss_Pred_Bool_Val() {
    return Miss_Pred_Bool;
  }
};

#endif // BPRED_H
