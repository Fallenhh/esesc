/*
  USAGE:
    1. In GProcessor.h uncomment: #define WAVESNAP_EN

    2. In TaskHandler.cpp, in TaskHandler::unplug() specify the format and the file where you
       want to dump the traces. In case wavedrom format is picked, copy and paste generated
       file into Wavedrom edittor to visualize. 
*/

#ifndef _WAVESNAP_
#define _WAVESNAP_
//functional defines
//#define REMOVE_REDUNDANT_EDGES

//general wavesnap defines
#define SINGLE_WINDOW     false
#define WITH_SAMPLING     true
//signature defines
#define REGISTER_NUM 32
#define HASH_SIZE    4*65536

//instruction window defines
#define MAX_NODE_NUM            1000
#define MAX_EDGE_NUM            1000000
#define MAX_MOVING_GRAPH_NODES  200

//ipc calculation defines
#define COUNT_ALLOW      0
#define INSTRUCTION_GAP  100

//dump path
#define DUMP_PATH "dump.txt"

//encode instructions
#define ENCODING "0123456789abcdefghijklmnopqrstuvwxyzABCDEFHIJKLMNPQRSTUVWXYZ"

#include <vector>
#include <stdint.h>
#include <stdio.h>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <map>
#include "InstOpcode.h"
#include "EmuSampler.h"
#include "DInst.h"
/*
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/serialization/map.hpp> 
#include <boost/serialization/string.hpp> 
#include <boost/serialization/vector.hpp>
*/
class instruction_info {
  public:
    uint64_t fetched_time;
    uint64_t renamed_time;
    uint64_t issued_time;
    uint64_t executed_time;
    uint64_t committed_time;
    uint8_t  opcode;
    uint64_t id;

    instruction_info() {
      this->fetched_time   = 0;
      this->renamed_time   = 0;
      this->issued_time    = 0;
      this->executed_time  = 0;
      this->committed_time = 0;
      this->opcode         = 0;
      this->id             = 0;
    }
};

class wavesnap {
  private:

    class dependence_info {
      public:
        uint32_t id;
        uint32_t dst;
        uint32_t src1;
        uint32_t src2;
        uint32_t parent1_id;
        uint32_t parent2_id;
        uint32_t depth;
        bool valid;
        std::string opcode_name;
        uint32_t opcode;
        uint64_t time_stamp;

        dependence_info() {
          this->id          = 0;
          this->dst         = 0;
          this->src1        = 0;
          this->src2        = 0;
          this->parent1_id  = 0;
          this->parent2_id  = 0;
          this->depth       = 0;
          this->valid       = false;
          this->time_stamp  = 0;
        }

        std::string dump() {
          std::stringstream result;

          result << "(";
          result << id << ", ";
          result << dst << ", ";
          result << src1 << ", ";
          result << src2 << ", ";
          result << parent1_id << ", ";
          result << parent2_id << ", ";
          result << depth << ")";
    
          return result.str();
        }
    };

    class pipeline_info {
      private:
        /*
        friend class boost::serialization::access;
        template<class Archive>
        void serialize(Archive & ar, const unsigned int version) {
          ar & this->wait_cycles;
          ar & this->rename_cycles;
          ar & this->issue_cycles;
          ar & this->execute_cycles;
          ar & this->commit_cycles;
          ar & encode;
          ar & count;
        }
        */
      public:
        std::vector<uint32_t> wait_cycles;
        std::vector<uint32_t> rename_cycles;
        std::vector<uint32_t> issue_cycles;
        std::vector<uint32_t> execute_cycles;
        std::vector<uint32_t> commit_cycles;
        std::string encode;
        uint32_t count;

        pipeline_info() {
          this->count  = 0;
          this->encode = "";
          this->wait_cycles.clear();
          this->rename_cycles.clear();
          this->issue_cycles.clear();
          this->execute_cycles.clear();
        }
  
        void clear() {
          this->count  = 1;
          this->encode = "";
          this->wait_cycles.clear();
          this->rename_cycles.clear();
          this->issue_cycles.clear();
          this->execute_cycles.clear();
        }
    };

    //private methods and member variables
    void record_pipe(pipeline_info* next);
    void add_pipeline_info(pipeline_info* pipe_info, instruction_info* dinst, uint64_t committed);

  public:
    wavesnap();
    ~wavesnap();

    //many windows
    void update_window(DInst* dinst, uint64_t committed);
    void add_instruction(DInst* dinst); 
    bool first_window_completed;
    uint64_t curr_min_time;
    uint64_t min_time_id;
    uint64_t last_removed_id;
    uint64_t update_count;
    std::vector<uint64_t> wait_buffer; 
    std::vector<bool> completed;
    pipeline_info working_window;
    std::map<uint64_t, instruction_info> dinst_info;
    uint64_t window_pointer;

    //single huge window, good for debeging
    void update_single_window(DInst* dinst, uint64_t committed);
    std::map<uint64_t, uint32_t> full_fetch_ipc;
    std::map<uint64_t, uint32_t> full_rename_ipc;
    std::map<uint64_t, uint32_t> full_issue_ipc;
    std::map<uint64_t, uint32_t> full_execute_ipc;
    std::map<uint64_t, uint32_t> full_commit_ipc;

    //stats methods
    void calculate_single_window_ipc();
    void calculate_ipc();
    void test_uncompleted();
    void window_frequency();
    
    //other
    instruction_info extract_inst_info(DInst* dinst);
    void add_to_RAT(DInst* dinst);
    void merge();
    std::map<std::string, pipeline_info> window_sign_info;
    uint64_t signature_count;
    std::map<uint64_t, dependence_info> RAT;

    /*
    //dumping and reading
    void save();
    void load();
    */
};
#endif
