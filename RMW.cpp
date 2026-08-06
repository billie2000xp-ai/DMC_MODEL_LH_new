/* $$$!!Warning: Huawei key information asset. No spread without permission.$$$ */
/* CODEMARK:RKeR1B8WMAfemkt1tTDGp4eOEddgxKn4NOPmdw0w+6Q3n1pxgDEX+kGBiRV20e1NKuLwOh60qWwx
7DOUvTqsDpJdC/G6ahMCQuRlwWqc+IGKquH6vaaGAGe1zSmcLn5FMd2VBk0upEP5xKZPTVuBjKnw
SvZMzBtMrQ+w1lxbG5+EFWux51V2bvtZUTAAA+en/pM7ZB5Cy3u0JTs1VqxXwvHeWENzuwdeZTg/
gCsQ5S8dHISlBFtDwNvcQ3yROkU5soXPnjDcr9UgAbT0+Kzk6UeRneIqMID/n+nQ8uUQOxnPDZfC
6kq5d1VA1iplFUrO# */
/* $$$!!Warning: Deleting or modifying the preceding information is prohibited.$$$ */
/*
* Copyright @ Huawei Technologies Co., Ltd. 2024-2034. All rights reserved.
* Description: Rmw.cpp
* Author: z00831990
* Create: 2024-6-12
*/

#include "Rmw.h"
//#include "LPMemorySystemTop.h"
#include "MemorySystem.h"
#include "MemoryController.h"
#include <assert.h>
#include <iomanip>
#include <algorithm>
#include <sstream>
using namespace std;

namespace LPDDRSim {
Rmw::WriteMergeDataRemap::WriteMergeDataRemap(uint64_t src_task_, uint64_t dst_task_, unsigned remaining_beats_)
        : src_task(src_task_), dst_task(dst_task_), remaining_beats(remaining_beats_) {}
//#define PROTECT_SUB(a) a = (a > 0) ? (a - 1) : 0;

//Rmw::Rmw(LPMemorySystemTop *_top, unsigned id, ostream &DDRSim_log_, string LogPath) :
Rmw::Rmw(MemoryController *_top, unsigned id, ostream &DDRSim_log_) :
        top(_top), 
        channel(id), 
        DDRSim_log(DDRSim_log_) { 
//        log_path(LogPath) {
    channel_ohot = 1ull << channel;
    rmw_cmd_cnt = 0;
//    bytes_per_col = JEDEC_DATA_BUS_BITS / 8;
//    push_cnt = 0;
    log_path = top->log_path;
    WdataToSend.clear();
    WdataChannel.clear();
    wdata_order_queue.clear();
    pre_req_time = 0xFFFFFFFFFFFFFFFF;
    pre_req_data_time = 0xFFFFFFFFFFFFFFFF;
    pre_cresp_time = 0xFFFFFFFFFFFFFFFF;
    start_cycle = 0;
    end_cycle = 0;
    rcmd_cnt = 0;
    wcmd_cnt = 0;
    totalReads = 0; 
    totalBypassReads = 0; 
    totalWrites = 0; 
    totalBypassWrites = 0; 
    totalFullWrites = 0; 
    totalMaskWrites = 0; 
    totalTransactions = 0; 
    merge_candidates = 0;
    merge_candidate_cmds = 0;
    merge_pairs = 0;
    merge_unpaired_flushes = 0;
    merge_remap_beats = 0;
    pdu_locality_streak = 0;
//    pre_reads = 0; 
//    pre_bypass_reads = 0; 
//    pre_bypass_writes = 0; 
//    pre_bypass_writes = 0; 
//    pre_full_writes = 0; 
//    pre_mask_writes = 0; 
//    pre_totals = 0; 
    for (uint32_t index = 0; index < RMW_QUE_DEPTH+1; index++) {
        rmw_que_cnt.push_back(0);
    }
    RmwInitOutputFiles();

    }


//void WriteBuff::rcmd_push_wcmd(Transaction * t) {
//    for (auto &w : Wbuff) {
//        if (t->bankIndex == w->bankIndex && t->row == w->row && t->col == w->col) {
//            push_cnt ++;
//            if (GrpMode.grp_mode6) { // priority is 1
//                w->pri = 1;
//            } else { // high priority
//                w->timeout = true;
//                wr_timeout_cnt ++;
//            }
//            if (DEBUG_BUS) {
//                PRINTN(setw(10)<<now()<<" -- ADD_PUSH :: task="<<t->task<<hex<<" address="<<t->address<<dec
//                        <<" rank="<<t->rank<<" bank="<<t->bankIndex<<" row="<<t->row<<" w_task="<<w->task<<endl);
//            }
//        }
//    }
//}

void Rmw::RmwInitOutputFiles() {
    if (DEBUG_BUS || DEBUG_STATE || DEBUG_RMW_STATE) {
        rmw_log_path = log_path + "/lpddr_sim_rmw" + std::to_string(channel) + ".log";
        rmw_log.open(rmw_log_path.c_str(), ios_base::out | ios_base::trunc);
        if (!rmw_log) {
            ERROR("Cannot open "<<rmw_log_path);
            assert(0);
        }
    }

    if (STATE_LOG) {
        string st_log = log_path + "/lpddr_state_rmw" + std::to_string(channel) + ".log";
        state_log.open(st_log.c_str(), ios_base::out | ios_base::trunc);
        if (!state_log) {
            ERROR("Cannot open "<<st_log);
            assert(0);
        }
    }
}



void Rmw::cmd_set_conflict(Transaction * t) {
    conf_state *c = new conf_state;
    c->task = t->task;

    for (auto &cmd : RmwQue) {
        if (cmd->task == t->task) continue;
        if (t->transactionType == DATA_READ && cmd->transactionType == DATA_READ) continue;
        bool col_conflict = address_conf(t, cmd);
        if (t->channel==cmd->channel && t->bankIndex == cmd->bankIndex && t->row == cmd->row && col_conflict) {
            c->ad_conf_cnt ++;
        }
    }
    RmwConfCnt.push_back(c);
}


void Rmw::cmd_release_conflict(Transaction *trans) {
    unsigned size = RmwQue.size();
    unsigned trans_rmwque_index = 0;
    
    for (unsigned i = 0; i < size; i ++) {
        if (trans->task == RmwQue[i]->task) {
            trans_rmwque_index = i;
            break;
        }
    }

    //index chk
    if (trans_rmwque_index >= size) {
        ERROR(setw(10)<<now()<<" -- Impossible index in Rmw Queue, index="<<trans_rmwque_index<<", task="<<trans->task);
        assert(0);
    }

//    for (unsigned i = 0; i < size; i ++) {
    for (unsigned i = trans_rmwque_index; i < size; i ++) {
        if (trans->task == RmwQue[i]->task) continue;
        if (trans->transactionType==DATA_READ && RmwQue[i]->transactionType==DATA_READ) continue;
        bool col_conflict = address_conf(trans, RmwQue[i]);
        if (trans->channel == RmwQue[i]->channel && trans->bankIndex == RmwQue[i]->bankIndex && trans->row == RmwQue[i]->row && col_conflict) {
            if (RmwConfCnt[i]->ad_conf_cnt > 0) {
                RmwConfCnt[i]->ad_conf_cnt--;
            }
        }
    }

}

bool Rmw::address_conf(Transaction *t, Transaction *cmd) {
        
        // data size cal
        unsigned t_data_size = (t->burst_length + 1) * DMC_DATA_BUS_BITS / 8;
        unsigned cmd_data_size = (cmd->burst_length + 1) * DMC_DATA_BUS_BITS / 8;
        // wrap or inc
//        bool t_wrap = ((t->address % t_data_size) == 0) ? false : true;
//        bool cmd_wrap = ((cmd->address % cmd_data_size) == 0) ? false : true;
        bool t_wrap = t->wrap_cmd;
        bool cmd_wrap = cmd->wrap_cmd;
//        if (t_wrap) {
//            DEBUG(now()<<"wrap cmd, task="<<t->task);
//        }
//        if (cmd_wrap) {
//            DEBUG(now()<<"wrap cmd in RMWQUE, task="<<cmd->task);
//        }
        // addr of trans && cmd
        unsigned t_start_addr_col = t_wrap ? ((t->addr_col / t_data_size) * t_data_size) : t->addr_col;
        unsigned t_end_addr_col = t_wrap ? ((t->addr_col / t_data_size + 1) * t_data_size) : t->addr_col + t_data_size;
        unsigned cmd_start_addr_col = cmd_wrap ? ((cmd->addr_col / cmd_data_size) * cmd_data_size) : cmd->addr_col;
        unsigned cmd_end_addr_col = cmd_wrap ? ((cmd->addr_col / cmd_data_size + 1) * cmd_data_size) : cmd->addr_col + cmd_data_size;
        // addr used for address conflict
        unsigned t_addr_left = 0;
        unsigned t_addr_right = 0;
        unsigned cmd_addr_left = 0;
        unsigned cmd_addr_right = 0;
        bool     ret = false;
         
//        if ((t_start_addr_col % RMW_CONF_SIZE == 0) && (t_end_addr_col % RMW_CONF_SIZE == 0)){       // address aligned with RMW_CONF_SIZE
        if (t_end_addr_col % RMW_CONF_SIZE == 0){       // address aligned with RMW_CONF_SIZE
            t_addr_left = t_start_addr_col / RMW_CONF_SIZE;
            t_addr_right = t_end_addr_col / RMW_CONF_SIZE;
        } else {       // address not aligned with RMW_CONF_SIZE 
            t_addr_left = t_start_addr_col / RMW_CONF_SIZE;
            t_addr_right = (t_end_addr_col / RMW_CONF_SIZE) + 1;
        }
        
//        if ((cmd_start_addr_col % RMW_CONF_SIZE == 0) && (cmd_end_addr_col % RMW_CONF_SIZE == 0)){       // address aligned with RMW_CONF_SIZE
        if (cmd_end_addr_col % RMW_CONF_SIZE == 0){       // address aligned with RMW_CONF_SIZE
            cmd_addr_left = cmd_start_addr_col / RMW_CONF_SIZE;
            cmd_addr_right = cmd_end_addr_col / RMW_CONF_SIZE;
        } else {       // address not aligned with RMW_CONF_SIZE 
            cmd_addr_left = cmd_start_addr_col / RMW_CONF_SIZE;
            cmd_addr_right = (cmd_end_addr_col / RMW_CONF_SIZE) + 1;
        }
        
        if ((t_addr_left >= cmd_addr_left && t_addr_left < cmd_addr_right) ||
                (t_addr_right > cmd_addr_left && t_addr_right <= cmd_addr_right)) {
//            DEBUG(now()<<" address conflict, transaction task="<<t->task<<" t_addr_left="<<t_addr_left<<" t_addr_right"<<t_addr_right
//                    <<" cmd in RMWQUE, task="<<cmd->task<<" cmd_addr_left"<<cmd_addr_left<<" cmd_addr_right"<<cmd_addr_right);
            ret = true;
        }
        return ret;

    
}

//void WriteBuff::trans_state_clr(Transaction * trans) {
//    trans->timeout = false;
//    trans->has_active = false;
//}

bool Rmw::is_write_merge_candidate(const Transaction *trans) const {
    if (trans == NULL || !WCMD_MERGE_EN || trans->transactionType != DATA_WRITE) return false;
    if ((!BYPASS_MERGEFLAG && !trans->mergeflag) || trans->mask_wcmd || trans->ecc_flag) return false;
    return ((trans->burst_length + 1) * DMC_DATA_BUS_BITS / 8) == 128;
}

bool Rmw::can_merge_write_pair(const Transaction *first, const Transaction *second) const {
    if (!is_write_merge_candidate(first) || !is_write_merge_candidate(second)) return false;
    uint64_t low_addr = std::min(first->address, second->address);
    uint64_t high_addr = std::max(first->address, second->address);
    return first->channel == second->channel && ((low_addr ^ high_addr) == 128) && ((low_addr & 127) == 0);
}

unsigned Rmw::transaction_slots(const Transaction *trans) const {
    return std::max(1u, (trans->data_size + 127) / 128);
}

unsigned Rmw::rmw_slot_cnt() const {
    unsigned slots = 0;
    for (const auto trans : RmwQue) slots += transaction_slots(trans);
    return slots;
}

bool Rmw::has_rmw_slots(unsigned slots) const {
    return RMW_QUE_DEPTH == 0 || rmw_slot_cnt() + slots <= RMW_QUE_DEPTH;
}

Transaction *Rmw::build_merged_write_transaction(Transaction *first, Transaction *second, uint64_t merged_task, bool mask_wcmd) {
    Transaction *lower = second != NULL && second->address < first->address ? second : first;
    Transaction *merged = new Transaction(*lower);
    merged->task = merged_task;
    merged->address = lower->address & ~uint64_t(127);
    merged->mask_wcmd = mask_wcmd;
    merged->ecc_flag = false;
    merged->burst_length = mask_wcmd ? ((first->burst_length + 1) * 2 - 1)
            : ((first->burst_length + 1) + (second->burst_length + 1) - 1);
    merged->data_size = (merged->burst_length + 1) * DMC_DATA_BUS_BITS / 8;
    merged->data_ready_cnt = 0;
    return merged;
}

bool Rmw::handle_write_merge_transaction(Transaction *trans) {
    merge_candidates++;
    for (size_t i = 0; i < RmwQue.size(); i++) {
        Transaction *first = RmwQue[i];
        if (!can_merge_write_pair(first, trans)) continue;
        unsigned merged_slots = transaction_slots(first) + transaction_slots(trans);
        if (!has_rmw_slots(merged_slots - transaction_slots(first))) return false;
        unsigned first_beats = first->burst_length + 1;
        unsigned second_ready = 0;
        auto pending = pending_write_data_cnt.find(trans->task);
        if (pending != pending_write_data_cnt.end()) {
            second_ready = pending->second;
            pending_write_data_cnt.erase(pending);
        }
        uint64_t first_task = first->task;
        uint64_t second_task = trans->task;
        Transaction *merged = build_merged_write_transaction(first, trans, second_task, false);
        merged->data_ready_cnt = first->data_ready_cnt + second_ready;
        merged->arb_time = now() + ((RMW_CONF_SIZE == 32) ? 3 : 2);
        if (RMW_CMD_MODE == 2) {
            bypassed_merged_writes.emplace(second_task, BypassedMergedWrite(first_task, second_task,
                    first_beats - first->data_ready_cnt, trans->burst_length + 1 - second_ready));
        } else if (first->data_ready_cnt < first_beats) {
            write_merge_data_remaps.push_back(WriteMergeDataRemap(first_task, second_task,
                    first_beats - first->data_ready_cnt));
        }
        write_merge_first_resp_task[second_task] = first_task;
        merge_candidate_cmds++;
        merge_pairs++;
        if (RMW_CMD_MODE == 1 && top->parentMemorySystem) {
            auto first_map = top->parentMemorySystem->write_map.find(first_task);
            if (first_map != top->parentMemorySystem->write_map.end()) {
                top->parentMemorySystem->write_map.erase(first_map);
            }
        }
        *trans = *merged;
        delete merged;
        delete first;
        RmwQue[i] = trans;
        RmwCmdState[i]->task = second_task;
        RmwCmdState[i]->rmwTimeAdded = now();
        RmwCmdState[i]->rmwState = QUE_WAITING;
        rebuild_conflict_state();
        return true;
    }
    if (!has_rmw_slots(transaction_slots(trans))) return false;
    merge_candidate_cmds++;
    auto pending = pending_write_data_cnt.find(trans->task);
    if (pending != pending_write_data_cnt.end()) {
        trans->data_ready_cnt = std::min(pending->second, trans->burst_length + 1);
        pending_write_data_cnt.erase(pending);
    }
    pre_req_time = now();
    cmd_state *state = new cmd_state;
    state->task = trans->task;
    state->rmwTimeAdded = now();
    trans->arb_time = now() + ((RMW_CONF_SIZE == 32) ? 3 : 2);
    cmd_set_conflict(trans);
    RmwQue.push_back(trans);
    RmwCmdState.push_back(state);
    rmw_cmd_cnt++;
    wcmd_cnt++;
    totalWrites++;
    totalFullWrites++;
    totalTransactions++;
    return true;
}

void Rmw::rebuild_conflict_state() {
    for (auto conf : RmwConfCnt) delete conf;
    RmwConfCnt.clear();
    for (size_t i = 0; i < RmwQue.size(); i++) {
        conf_state *conf = new conf_state;
        conf->task = RmwQue[i]->task;
        for (size_t j = 0; j < i; j++) {
            if (RmwQue[i]->channel == RmwQue[j]->channel
                    && RmwQue[i]->bankIndex == RmwQue[j]->bankIndex
                    && RmwQue[i]->row == RmwQue[j]->row
                    && !(RmwQue[i]->transactionType == DATA_READ && RmwQue[j]->transactionType == DATA_READ)
                    && address_conf(RmwQue[i], RmwQue[j])) conf->ad_conf_cnt++;
        }
        RmwConfCnt.push_back(conf);
    }
}

bool Rmw::remap_write_merge_data(uint32_t *data, uint64_t task) {
    for (auto it = bypassed_merged_writes.begin(); it != bypassed_merged_writes.end(); ++it) {
        BypassedMergedWrite &entry = it->second;
        bool first_source = task == entry.first_task && entry.first_remaining != 0;
        bool second_source = task == entry.merged_task && entry.second_remaining != 0;
        if (!first_source && !second_source) continue;
        if (has_queued_write_data(entry.merged_task)) return false;
        bool accepted = top->parentMemorySystem->submitData(data, entry.merged_task, false);
        if (!accepted && top->canReceiveWdata(entry.merged_task)
                && top->parentMemorySystem->write_map.count(entry.merged_task) == 0) {
            top->receiveFromCPU(data, entry.merged_task);
            accepted = true;
        }
        if (!accepted) {
            return false;
        }
        check_write_data(entry.merged_task);
        if (first_source) entry.first_remaining--;
        else entry.second_remaining--;
        if (entry.first_remaining == 0 && entry.second_remaining == 0) {
            bypassed_merged_writes.erase(it);
        }
        pre_req_data_time = now();
        return true;
    }
    for (size_t i = 0; i < write_merge_data_remaps.size(); i++) {
        if (write_merge_data_remaps[i].src_task != task) continue;
        uint64_t dst_task = write_merge_data_remaps[i].dst_task;
        if (RMW_CMD_MODE == 1) {
            for (auto rmwq : RmwQue) {
                if (rmwq->task != dst_task || rmwq->transactionType != DATA_WRITE
                        || rmwq->data_ready_cnt > rmwq->burst_length) continue;
                rmwq->data_ready_cnt++;
                merge_remap_beats++;
                write_merge_data_remaps[i].remaining_beats--;
                if (write_merge_data_remaps[i].remaining_beats == 0) {
                    write_merge_data_remaps.erase(write_merge_data_remaps.begin() + i);
                }
                pre_req_data_time = now();
                return true;
            }
            return false;
        }
        if (!top->parentMemorySystem->submitData(data, dst_task, false)) return false;
        check_write_data(dst_task);
        merge_remap_beats++;
        write_merge_data_remaps[i].remaining_beats--;
        if (write_merge_data_remaps[i].remaining_beats == 0) {
            write_merge_data_remaps.erase(write_merge_data_remaps.begin() + i);
        }
        pre_req_data_time = now();
        return true;
    }
    return false;
}

bool Rmw::has_queued_write_data(uint64_t task) const {
    return std::find(WdataToSend.begin(), WdataToSend.end(), task) != WdataToSend.end();
}

bool Rmw::is_unpaired_write_merge_timeout(Transaction *trans, cmd_state *state) {
    return is_write_merge_candidate(trans) && WRITE_MERGE_TIMEOUT != 0
            && now() - state->rmwTimeAdded >= WRITE_MERGE_TIMEOUT;
}

bool Rmw::is_arb_eligible(unsigned index, bool iecc_owned) {
    if (now() < RmwQue[index]->arb_time) return false;
    if (top->iecc->iecc_owner_valid && !iecc_owned) return false;
    if (RmwConfCnt[index]->ad_conf_cnt != 0 && !iecc_owned) return false;
    if (RmwCmdState[index]->rmwState == MERGE_READ && RmwQue[index]->mask_wcmd) return false;
    bool merged_command = write_merge_first_resp_task.find(RmwQue[index]->task)
            != write_merge_first_resp_task.end();
    if (is_write_merge_candidate(RmwQue[index]) && !merged_command
            && !is_unpaired_write_merge_timeout(RmwQue[index], RmwCmdState[index])) return false;
    if (RmwQue[index]->transactionType == DATA_WRITE && !RmwQue[index]->mask_wcmd
            && RMW_CMD_MODE == 1 && RmwCmdState[index]->rmwState != SEND_READY) return false;
    if (RmwQue[index]->transactionType == DATA_WRITE && !RmwQue[index]->mask_wcmd
            && RmwQue[index]->ecc_flag && RmwCmdState[index]->rmwState != SEND_READY) return false;
    return true;
}

unsigned Rmw::select_arb_candidate(const std::vector<unsigned> &eligible) {
    unsigned first = eligible.front();
    if (!IECC_ENABLE || IECC_PDU_SCHED_MODE == 0 || top->iecc->iecc_owner_valid) {
        pdu_locality_streak = 0;
        return first;
    }
    if ((PDU_REORDER_MAX_AGE != 0
            && now() - RmwCmdState[first]->rmwTimeAdded >= PDU_REORDER_MAX_AGE)
            || (PDU_LOCALITY_BUDGET != 0 && pdu_locality_streak >= PDU_LOCALITY_BUDGET)) {
        pdu_locality_streak = 0;
        return first;
    }
    if (RmwQue[first]->transactionType == DATA_WRITE
            && (top->iecc->probe_pdu(*RmwQue[first]).hit_result & 2) != 0) {
        pdu_locality_streak = 0;
        return first;
    }
    for (auto index : eligible) {
        if (RmwQue[index]->qos != RmwQue[first]->qos) continue;
        if (RmwQue[index]->transactionType != DATA_WRITE) continue;
        if ((top->iecc->probe_pdu(*RmwQue[index]).hit_result & 2) == 0) continue;
        pdu_locality_streak++;
        return index;
    }
    pdu_locality_streak = 0;
    return first;
}

bool Rmw::addData(uint32_t *data, uint64_t task) {
    if (remap_write_merge_data(data, task)) return true;
    auto fast_bypass = fast_bypass_write_data_cnt.find(task);
    if (fast_bypass != fast_bypass_write_data_cnt.end()) {
        if (has_queued_write_data(task)) return false;
        if (!top->parentMemorySystem->submitData(data, task, false)) return false;
        check_write_data(task);
        if (--fast_bypass->second == 0) fast_bypass_write_data_cnt.erase(fast_bypass);
        pre_req_data_time = now();
        return true;
    }
    bool task_match = false;
    for (auto &rmwq : RmwQue) {
        if (task == rmwq->task) {
            task_match=true;
        };
        if (rmwq->transactionType == DATA_WRITE
                && rmwq->data_ready_cnt <= rmwq->burst_length && task==rmwq->task) {
            rmwq->data_ready_cnt ++;
            pre_req_data_time = now();
            if (DEBUG_BUS) {
                 PRINTN(setw(10)<<now()<<" -- RMW_MATCH :: data_ready_cnt:"<<rmwq->data_ready_cnt
                         <<", data_size="<<rmwq->data_size<<", task="<<rmwq->task<<endl);
            }
//            return true;
            return true;
        }
    }

    if (task_match==false) {
//        bool ret = false;
//        if (EM_ENABLE) {
//            ret = top->channels[0]->addData(data ,task);
//        } else {
//            ret = top->channels[channel]->addData(data ,task);
//        }
        if (top->parentMemorySystem->write_map.count(task) != 0) {
            return top->parentMemorySystem->submitData(data, task, false);
        }
        return top->parentMemorySystem->submitData(data, task, false);
    }

//    ERROR(setw(10)<<now()<<" -- Impossible wdata, task="<<task);
//    assert(0);

    return true;
}

bool Rmw::canAcceptData(uint64_t task) const {
    if (fast_bypass_write_data_cnt.find(task) != fast_bypass_write_data_cnt.end()) {
        return !has_queued_write_data(task) && top->canReceiveWdata(task);
    }
    for (const auto &item : bypassed_merged_writes) {
        const BypassedMergedWrite &entry = item.second;
        if ((task == entry.first_task && entry.first_remaining != 0)
                || (task == entry.merged_task && entry.second_remaining != 0)) {
            return !has_queued_write_data(entry.merged_task) && top->canReceiveWdata(entry.merged_task);
        }
    }
    for (const auto &entry : write_merge_data_remaps) {
        if (entry.src_task != task) continue;
        if (RMW_CMD_MODE == 1) {
            for (auto rmwq : RmwQue) {
                if (rmwq->task == entry.dst_task && rmwq->transactionType == DATA_WRITE
                        && rmwq->data_ready_cnt <= rmwq->burst_length) return true;
            }
            return false;
        }
        return top->canReceiveWdata(entry.dst_task);
    }
    for (auto rmwq : RmwQue) {
        if (rmwq->task == task && rmwq->transactionType == DATA_WRITE
                && rmwq->data_ready_cnt <= rmwq->burst_length) {
            return true;
        }
    }
    if (top->parentMemorySystem->write_map.count(task) != 0) {
        return top->canReceiveWdata(task);
    }
    return top->canReceiveWdata(task);
}

bool Rmw::addTransaction(Transaction * trans) {

//    if (trans->data_size == 0) {
//         DEBUG(" task="<<trans->task<<" data size="<<trans->data_size);
//    }

    if ((trans->transactionType==DATA_READ)&&(trans->mask_wcmd==true)){
        ERROR(setw(10)<<now()<<" -- No mask Flag In Read, task="<<trans->task<<" type="<<trans->transactionType
               <<" address="<<hex<<trans->address<<" mask_write="<<trans->mask_wcmd);
        assert(0);
    }


//    uint8_t ch = trans->channel;
    bool rmw_que_full  = !has_rmw_slots(transaction_slots(trans));
    bool rmw_que_empty = rmw_cmd_cnt == 0;

    if (is_write_merge_candidate(trans)) {
        return handle_write_merge_transaction(trans);
    }

    if (rmw_que_full) {
        if (DEBUG_BUS) {
            PRINTN(setw(10)<<now()<<" -- RMW BP CMD :: task="<<trans->task<<" type="<<trans->transactionType<<" mask_write="<<trans->mask_wcmd
                    <<" qos="<<trans->qos<<" burst_length:"<<trans->burst_length<<" address="<<hex<<trans->address
                    <<dec<<" rank="<<trans->rank<<" bank="<<trans->bankIndex<<" row="<<trans->row<<" channel="<<trans->channel<<" (rmw_cmd_cnt:"<<rmw_cmd_cnt
                    <<")"<<endl);
        }
        return false;
    }

    if (rmw_que_empty) {
        if ((trans->transactionType==DATA_READ)||((trans->transactionType==DATA_WRITE)&&(RMW_CMD_MODE==0)&&(trans->mask_wcmd==false))) {
            if (DEBUG_BUS) {
                PRINTN(setw(10)<<now()<<" -- RMW BYPASS :: task="<<trans->task<<" type="<<trans->transactionType<<" mask_write="<<trans->mask_wcmd
                        <<" ecc_flag="<<trans->ecc_flag<<" qos="<<trans->qos<<" burst_length="<<trans->burst_length<<" address="<<hex<<trans->address
                        <<dec<<" rank="<<trans->rank<<" bank="<<trans->bankIndex<<" row="<<trans->row<<" channel="<<trans->channel<<" col="<<trans->col
                        <<" addr_col="<<trans->addr_col<<" (rmw_cmd_cnt:"<<rmw_cmd_cnt<<")"<<endl);
            }

//            bool ret = top->channels[ch]->addTransaction(trans); 
            bool ret = top->push_after_rmw(trans); 
            
            if (ret) {
                if (trans->transactionType==DATA_READ){
                    totalBypassReads++;
                } else {
                    totalBypassWrites++;
                    totalFullWrites ++;
                }
            }
            
            return ret;
        }
    }


    pre_req_time = now();
    cmd_state *c = new cmd_state;
    c->task = trans->task;
    c->rmwTimeAdded = now();
    if (trans->transactionType == DATA_READ) {
        cmd_set_conflict(trans);
        if (RMW_CONF_SIZE == 32) {
            trans->arb_time = now() + 2;
        } else {
            trans->arb_time = now() + 1;
        }

        rcmd_cnt ++;
        rmw_cmd_cnt ++;
        totalReads ++;
        totalTransactions ++;
        RmwQue.push_back(trans);
        RmwCmdState.push_back(c);


        if (DEBUG_BUS) {
            PRINTN(setw(10)<<now()<<" -- RMW_ADD ::[R]B["<<trans->burst_length<<"]"<<"QOS["<<trans->qos<<"] addr="<<hex
                    <<trans->address<<dec<<" col="<<trans->col<<" addr_col="<<trans->addr_col<<" task="<<trans->task<<" type="<<trans->transactionType
                    <<" mask_write="<<trans->mask_wcmd<<" ecc_flag="<<trans->ecc_flag<<" rank="<<trans->rank<<" group="<<trans->group<<" bank="
                    <<trans->bankIndex<<" row="<<trans->row<<" channel="<<trans->channel<<" (rmw_cmd_cnt:"<<rmw_cmd_cnt<<")"<<endl);
        }
        return true;
    } else {
        cmd_set_conflict(trans);
        auto pending = pending_write_data_cnt.find(trans->task);
        if (pending != pending_write_data_cnt.end()) {
            unsigned beats = trans->burst_length + 1;
            trans->data_ready_cnt += std::min(pending->second, beats - trans->data_ready_cnt);
            pending_write_data_cnt.erase(pending);
        }
        if (RMW_CONF_SIZE == 32) {
            trans->arb_time = now() + 3;
        } else {
            trans->arb_time = now() + 2;
        }
        if (trans->mask_wcmd == true) {
//            top->channels[ch]->memoryController->rmw_rd_finish[trans->task] = false;
            top->rmw_rd_finish[trans->task] = false;
            totalMaskWrites ++;
        } else {
            totalFullWrites ++;
        }

        rmw_cmd_cnt ++;
        wcmd_cnt ++;
        totalWrites ++;
        totalTransactions ++;
        RmwQue.push_back(trans);
        RmwCmdState.push_back(c);
        
        if (RMW_CMD_MODE==1 && !trans->ecc_flag) {
            gen_cresp(trans->task);
            RmwCmdRespCh.push_back(trans->channel);
        }

//        if (!RmwCmdResp.empty()) {
//            if (pre_cresp_time != now()) {
//                if (cmd_response(RmwCmdResp[0], 0, ch)) {
//                    if (DEBUG_BUS) {
//                        PRINTN(setw(10)<<now()<<" -- Rmw Cresp Received :: task="<<RmwCmdResp[0]<<" RMW MODE="<<RMW_CMD_MODE<<endl);
//                    }
//                    pre_cresp_time = now();
//                    RmwCmdResp.erase(RmwCmdResp.begin());
//                } else {
//                    if (DEBUG_BUS) {
//                        PRINTN(setw(10)<<now()<<" -- Rmw Cresp Back Pressure :: task="<<RmwCmdResp[0]<<" RMW MODE="<<RMW_CMD_MODE<<endl);
//                    }
//                }
//            }
//        }
        
        if (DEBUG_BUS) {
            PRINTN(setw(10)<<now()<<" -- RMW_ADD :: [W]B["<<trans->burst_length<<"]"<<"QOS["<<trans->qos<<"] addr="<<hex
                    <<trans->address<<dec<<" col="<<trans->col<<" addr_col="<<trans->addr_col<<" task="<<trans->task<<" type="<<trans->transactionType
                    <<" mask_write="<<trans->mask_wcmd<<" ecc_flag="<<trans->ecc_flag<<" rank="<<trans->rank<<" group="<<trans->group<<" bank="
                    <<trans->bankIndex<<" row="<<trans->row<<" channel="<<trans->channel<<" (rmw_cmd_cnt:"<<rmw_cmd_cnt<<")"<<endl);
        }
        return true;
    }
}

void Rmw::gen_cresp(uint64_t task) {
    RmwCmdResp.push_back(task);
}

bool Rmw::cmd_response(uint64_t task,uint64_t address, uint8_t ch) {
//     return ((*(top->channels[ch])->CmdResp)(ch,task,0,0,0));
     return ((*(top->parentMemorySystem)->CmdResp)(ch,task,0,0,0));
}

void Rmw::update_cresp() {
    // RMW cresp return to ha 
    if (!RmwCmdResp.empty()) {
        if (pre_cresp_time != now()) {
            if (cmd_response(RmwCmdResp[0], 0, RmwCmdRespCh[0])) {
                if (DEBUG_BUS) {
                    PRINTN(setw(10)<<now()<<" -- Rmw Cresp Received :: task="<<RmwCmdResp[0]<<" RMW MODE="<<RMW_CMD_MODE<<endl);
                }
                pre_cresp_time = now();
                RmwCmdResp.erase(RmwCmdResp.begin());
                RmwCmdRespCh.erase(RmwCmdRespCh.begin());
            } else {
                if (DEBUG_BUS) {
                    PRINTN(setw(10)<<now()<<" -- Rmw Cresp Back Pressure :: task="<<RmwCmdResp[0]<<" RMW MODE="<<RMW_CMD_MODE<<endl);
                }
            }
        }
    }
}

    void Rmw::func_check() {
    if (RmwQue.size() != rmw_cmd_cnt) {
        ERROR(setw(10)<<now()<<" -- RmwQue Unmatch, RmwQue="<<RmwQue.size()<<", rmw_cmd_cnt="<<rmw_cmd_cnt);
        assert(0);
    }
}

void Rmw::update() {
    update_cresp();
#if 0
    func_check();
#endif
    update_state();
//    check_timeout();
    sch_que();
    arb_node();
    send_wdata();
}

void Rmw::update_state() {
    if (!DEBUG_RMW_STATE) return;
    unsigned size = RmwQue.size();
    rmw_log << "--------------------------------------------------------------------------------------------------" << endl;
    rmw_log << "Rmw Total Status: R:" << rcmd_cnt << " W:" << wcmd_cnt << " R+W:" << rmw_cmd_cnt
            << " Merge candidates:" << merge_candidates << " pairs:" << merge_pairs
            << " unpaired flushes:" << merge_unpaired_flushes << " remap beats:" << merge_remap_beats << endl;
    for (unsigned i = 0; i < size; i ++) {
        auto t = RmwQue[i];
        auto r = RmwConfCnt[i];
        auto s = RmwCmdState[i];
        rmw_log << "Rmw time: "<<now()<<" | type="<<t->transactionType<<" | task="<<t->task<<" | bank="<<t->bankIndex<<" | rank="<<t->rank<<" | row="
                <<t->row<<" | address="<<hex<<t->address<<dec<<" | addr_col="<<t->addr_col<<" | len="<<t->burst_length<<" | channel="<<t->channel<<" | data_size="<<t->data_size
                <<" | data_ready_cnt="<<t->data_ready_cnt<<" | timeout="<<t->timeout<<" | qos="<<t->qos<<" | pri="<<t->pri
                <<" | rd_byp="<<t->has_active<<" | mask_wcmd="<<t->mask_wcmd<<" | addr_conf_cnt="<<r->ad_conf_cnt<<" | cmd_state="
                <<s->rmwState<<" | RMW MODE="<<RMW_CMD_MODE<<endl;
    }
    rmw_log << "--------------------------------------------------------------------------------------------------" << endl;
    rmw_log.flush();
}


void Rmw::sch_que() {
    if (RmwQue.empty()) return;

    // command number check
    if ((rcmd_cnt+wcmd_cnt)!=rmw_cmd_cnt) {
        ERROR(setw(10)<<now()<<" -- Cmd Number Chk Failed, No.Read="<<rcmd_cnt<<" No.Write="<<wcmd_cnt<<" No.Total="<<rmw_cmd_cnt);
        assert(0);
    }
    
    unsigned size = RmwQue.size();
    for (unsigned i = 0; i < size; i++) {

        //prevent big latency in RMW QUE
        if ((now() - RmwCmdState[i]->rmwTimeAdded) > 1000000) {
            ERROR(setw(10)<<now()<<" -- task="<<RmwQue[i]->task<<" address="<<hex<<RmwQue[i]->address<<dec
                    <<" rank="<<RmwQue[i]->rank<<" bank="<<RmwQue[i]->bankIndex<<" row="<<RmwQue[i]->row<<" type="
                    <<RmwQue[i]->transactionType<<" mask_wcmd="<<RmwQue[i]->mask_wcmd);
            ERROR(setw(10)<<now()<<" -- error, qos="<<RmwQue[i]->qos<<", pri="<<RmwQue[i]->pri);
            ERROR(setw(10)<<now()<<" -- RMW FATAL ERROR == big latency"<<", chnl:"<<RmwQue[i]->channel);
            assert(0);
        }
        //prevent wdata lost in RMW QUE
        if ((RmwQue[i]->transactionType == DATA_WRITE)&&(RmwQue[i]->mask_wcmd || (!RmwQue[i]->mask_wcmd && RMW_CMD_MODE))) {
            if (now() - RmwCmdState[i]->rmwTimeAdded > 100000 && RmwQue[i]->data_ready_cnt <= RmwQue[i]->burst_length) {
                ERROR(setw(10)<<now()<<" -- RMW_DMC["<<RmwQue[i]->channel<<"] task="<<RmwQue[i]->task<<" Wdata number miss match, EXP="
                        <<RmwQue[i]->burst_length<<", ACT="<<RmwQue[i]->data_ready_cnt);
                assert(0);
            }
        }


//        uint8_t ch = EM_ENABLE ? 0 : RmwQue[i]->channel;
        //Merge between write data and read data (Mask Write)
        if ((RmwQue[i]->transactionType==DATA_WRITE) && (RmwQue[i]->mask_wcmd==true)) {
//            auto it = top->channels[ch]->memoryController->rmw_rd_finish.find(RmwQue[i]->task);
//            if (it != top->channels[ch]->memoryController->rmw_rd_finish.end()) {
            auto it = top->rmw_rd_finish.find(RmwQue[i]->task);
            if (it != top->rmw_rd_finish.end()) {
                if((RmwQue[i]->data_ready_cnt>=(RmwQue[i]->burst_length + 1)) && (it->second==true)){
                    RmwCmdState[i]->rmwState = SEND_READY;
                    top->rmw_rd_finish.erase(it);
                    if (DEBUG_BUS) {
                        PRINTN(setw(10)<<now()<<" -- RMW WDATA RDATA MERGE ::[MaskW]B["<<RmwQue[i]->burst_length<<"]"<<"QOS["<<RmwQue[i]->qos<<"] addr="<<hex
                            <<RmwQue[i]->address<<dec<<" task="<<RmwQue[i]->task<<" type="<<RmwQue[i]->transactionType<<" mask_write="<<RmwQue[i]->mask_wcmd
                            <<" rank="<<RmwQue[i]->rank<<" group="<<RmwQue[i]->group<<" bank="
                            <<RmwQue[i]->bankIndex<<" row="<<RmwQue[i]->row<<" mode="<<RMW_CMD_MODE<<" (rmw_cmd_cnt:"<<rmw_cmd_cnt<<")"<<endl);
                    } 
                }
            }
        }

        //Full Write under non fast cmd mode: waiting for write data
        if (((RmwQue[i]->transactionType==DATA_WRITE) && (RmwQue[i]->mask_wcmd==false) && (RMW_CMD_MODE==1)) 
                || ((RmwQue[i]->transactionType==DATA_WRITE) && (RmwQue[i]->mask_wcmd==false) && (RmwQue[i]->ecc_flag==true))) {
            if(RmwQue[i]->data_ready_cnt >= (RmwQue[i]->burst_length + 1)) {
                if (DEBUG_BUS) {
                    PRINTN(setw(10)<<now()<<" -- RMW WDATA MATCH ::[FullW]B["<<RmwQue[i]->burst_length<<"]"<<"QOS["<<RmwQue[i]->qos<<"] addr="<<hex
                        <<RmwQue[i]->address<<dec<<" task="<<RmwQue[i]->task<<" type="<<RmwQue[i]->transactionType<<" mask_write="<<RmwQue[i]->mask_wcmd
                        <<" rank="<<RmwQue[i]->rank<<" group="<<RmwQue[i]->group<<" bank="
                        <<RmwQue[i]->bankIndex<<" row="<<RmwQue[i]->row<<" mode="<<RMW_CMD_MODE<<" (rmw_cmd_cnt:"<<rmw_cmd_cnt<<")"<<endl);
                } 
                RmwCmdState[i]->rmwState = SEND_READY;   
            }
        }
        if (RmwQue[i]->transactionType == DATA_WRITE && !RmwQue[i]->mask_wcmd
                && is_write_merge_candidate(RmwQue[i])
                && is_unpaired_write_merge_timeout(RmwQue[i], RmwCmdState[i])) {
            if (UNPAIRED_TO_RMW_EN) {
                RmwQue[i]->mask_wcmd = true;
                RmwQue[i]->burst_length = (RmwQue[i]->burst_length + 1) * 2 - 1;
                RmwQue[i]->data_size = (RmwQue[i]->burst_length + 1) * DMC_DATA_BUS_BITS / 8;
                top->rmw_rd_finish[RmwQue[i]->task] = false;
            } else if (RmwQue[i]->data_ready_cnt >= RmwQue[i]->burst_length + 1) {
                RmwCmdState[i]->rmwState = SEND_READY;
            }
        }
    }
}



void Rmw::arb_node() {
    if (RmwQue.empty()) return;

    if (top->iecc->iecc_owner_valid) {
        bool owner_in_queue = false;
        for (auto queued : RmwQue) {
            if (queued->task == top->iecc->iecc_owner_task) {
                owner_in_queue = true;
                break;
            }
        }
        if (!owner_in_queue && top->iecc->pdu_push_pending_trans == NULL) {
            top->iecc->iecc_owner_valid = false;
            top->iecc->ecc_model_state = TRY_HIT_ECC_BUF;
        }
    }

    unsigned size = RmwQue.size();
    std::vector<unsigned> eligible;
    for (unsigned i = 0; i < size; i++) {
        if ((RmwQue[i]->transactionType == DATA_READ) && (RmwCmdState[i]->rmwState!=QUE_WAITING)){
            ERROR(setw(10)<<now()<<" -- Read Cmd State Wrong, task="<<RmwQue[i]->task<<" type="<<RmwQue[i]->transactionType<<" channel="<<RmwQue[i]->channel
                   <<" address="<<hex<<RmwQue[i]->address<<" cmd_state="<<RmwCmdState[i]->rmwState<<" mask_write="<<RmwQue[i]->mask_wcmd
                   <<" rmw_mode="<<RMW_CMD_MODE);
            assert(0);
        }

        bool iecc_owned = top->iecc->iecc_owner_valid
                && RmwQue[i]->task == top->iecc->iecc_owner_task;
        if (is_arb_eligible(i, iecc_owned)) eligible.push_back(i);
    }
    if (eligible.empty()) return;

    unsigned selected = select_arb_candidate(eligible);
    std::vector<unsigned> arb_order;
    arb_order.push_back(selected);
    for (auto index : eligible) {
        if (index != selected) arb_order.push_back(index);
    }

    for (auto i : arb_order) {

        if (now() < RmwQue[i]->arb_time) { 
            continue;
        }
        bool iecc_owned = top->iecc->iecc_owner_valid
                && RmwQue[i]->task == top->iecc->iecc_owner_task;
        if (top->iecc->iecc_owner_valid && !iecc_owned) {
            continue;
        }
        if (RmwConfCnt[i]->ad_conf_cnt != 0 && !iecc_owned) {
            continue;
        }
        if ((RmwCmdState[i]-> rmwState == MERGE_READ)&&(RmwQue[i]->mask_wcmd==true)) {
            continue;
        }
        bool merged_command = write_merge_first_resp_task.find(RmwQue[i]->task)
                != write_merge_first_resp_task.end();
        if (is_write_merge_candidate(RmwQue[i]) && !merged_command
                && !is_unpaired_write_merge_timeout(RmwQue[i], RmwCmdState[i])) {
            continue;
        }
        if ((RmwQue[i]->transactionType == DATA_WRITE)&&(RmwQue[i]->mask_wcmd==false)&&(RMW_CMD_MODE==1)&&(RmwCmdState[i]-> rmwState!=SEND_READY)) {
            continue;
        }
        if ((RmwQue[i]->transactionType == DATA_WRITE)&&(RmwQue[i]->mask_wcmd==false)&&(RmwQue[i]->ecc_flag==true)&&(RmwCmdState[i]-> rmwState!=SEND_READY)) {
            continue;
        }

        uint8_t ch = RmwQue[i]->channel;
        
        // RMW_CMD_MODE: 0 -> fast command mode; 1 -> non fast command mode
        if ((RmwQue[i]->transactionType == DATA_WRITE)&&(RmwQue[i]->mask_wcmd==true)&&(RmwCmdState[i]->rmwState==QUE_WAITING)) {  
            Transaction *trans = new Transaction(RmwQue[i]);
            trans->transactionType = DATA_READ; 
//            if (top->channels[ch]->addTransaction(trans)) {
            if (top->push_after_rmw(trans)) {
                if (DEBUG_BUS) {
                    PRINTN(setw(10)<<now()<<" -- RMW SCH :: [MERGE_READ_CMD] task="<<RmwQue[i]->task<<" type="<<RmwQue[i]->transactionType<<" mask_write="<<RmwQue[i]->mask_wcmd<<" ecc_flag="<<RmwQue[i]->ecc_flag
                            <<" qos="<<RmwQue[i]->qos<<" burst_length="<<RmwQue[i]->burst_length<<" channel="<<RmwQue[i]->channel<<" data_ready_cnt="<<RmwQue[i]->data_ready_cnt<<" address="<<hex<<RmwQue[i]->address
                            <<dec<<" rank="<<RmwQue[i]->rank<<" bank="<<RmwQue[i]->bankIndex<<" row="<<RmwQue[i]->row<<" col="<<trans->col<<" addr_col="<<trans->addr_col<<" rmw_mode="<<RMW_CMD_MODE
                            <<" rmw_cmd_cnt="<<rmw_cmd_cnt<<endl);
                }
                RmwCmdState[i]->rmwState = MERGE_READ;
                break;
            } else {
                delete trans;
            }
        } else if ((RmwQue[i]->transactionType == DATA_READ)   // read cmd
                    || ((RmwQue[i]->transactionType == DATA_WRITE)&&(RmwQue[i]->mask_wcmd==false)&&(RMW_CMD_MODE==1)&&(RmwCmdState[i]->rmwState==SEND_READY))  // full write cmd under non fast command mode
                    || ((RmwQue[i]->transactionType == DATA_WRITE)&&(RmwQue[i]->mask_wcmd==false)&&(RmwQue[i]->ecc_flag==true)&&(RmwCmdState[i]->rmwState==SEND_READY))  // full write cmd with ecc flag
                    || ((RmwQue[i]->transactionType == DATA_WRITE)&&(RmwQue[i]->mask_wcmd==true)&&(RmwCmdState[i]->rmwState==SEND_READY))) {     // mask write cmd
//            if (top->channels[ch]->addTransaction(RmwQue[i])) {
            bool push_ret = top->push_after_rmw(RmwQue[i]);
            if (push_ret) {
                if (DEBUG_BUS) {
                    PRINTN(setw(10)<<now()<<" -- RMW SCH :: task="<<RmwQue[i]->task<<" type="<<RmwQue[i]->transactionType<<" mask_write="<<RmwQue[i]->mask_wcmd<<" ecc_flag="<<RmwQue[i]->ecc_flag 
                            <<" qos="<<RmwQue[i]->qos<<" burst_length="<<RmwQue[i]->burst_length<<" channel="<<RmwQue[i]->channel<<" data_ready_cnt="<<RmwQue[i]->data_ready_cnt<<" address="<<hex<<RmwQue[i]->address
                            <<dec<<" rank="<<RmwQue[i]->rank<<" bank="<<RmwQue[i]->bankIndex<<" row="<<RmwQue[i]->row<<" col="<<RmwQue[i]->col<<" addr_col="<<RmwQue[i]->addr_col<<" rmw_mode="<<RMW_CMD_MODE
                            <<" rmw_cmd_cnt="<<rmw_cmd_cnt<<endl);
                }

                // collect all wdata for sended write cmd
                if ((RmwQue[i]->transactionType==DATA_WRITE) && ((RmwQue[i]->mask_wcmd==true || (RmwQue[i]->mask_wcmd==false && RMW_CMD_MODE==1) || (!RmwQue[i]->mask_wcmd && RmwQue[i]->ecc_flag)))) {
                    track_write_command(RmwQue[i]->task, RmwQue[i]->burst_length + 1);
                    for (size_t j = 0; j <= RmwQue[i]->burst_length; j++) {
                        WdataToSend.push_back(RmwQue[i]->task);
                        WdataChannel.push_back(ch);
                    }
                }
                if (merged_command) {
                    uint64_t task = RmwQue[i]->task;
                    top->parentMemorySystem->markMergedWrite(
                            task, write_merge_first_resp_task[task]);
                    write_merge_first_resp_task.erase(task);
                }

                //update statistic info
                if (RmwQue[i]->transactionType == DATA_READ) {
                    rcmd_cnt --;
                } else {
                    wcmd_cnt --;
                }
                rmw_cmd_cnt --;

                //delete all states related to sended cmd this round
                delete RmwConfCnt[i];
                RmwConfCnt.erase(RmwConfCnt.begin() + i);
                delete RmwCmdState[i];
                RmwCmdState.erase(RmwCmdState.begin() + i);
                RmwQue.erase(RmwQue.begin() + i);
                rebuild_conflict_state();
                break;
            }
        } else if ((RmwQue[i]->transactionType == DATA_WRITE)&&(RmwQue[i]->mask_wcmd==false)
                && ((RMW_CMD_MODE==0) || (RMW_CMD_MODE==2 && !RmwQue[i]->ecc_flag))) {
//            if (top->channels[ch]->addTransaction(RmwQue[i])) {
            unsigned buffered_beats = RmwQue[i]->data_ready_cnt;
            bool merged_command_first = merged_command && RMW_CMD_MODE == 2;
            if (top->push_after_rmw(RmwQue[i])) {
                uint64_t task = RmwQue[i]->task;
                unsigned total_beats = RmwQue[i]->burst_length + 1;
                track_write_command(task, total_beats);
                for (unsigned j = 0; j < buffered_beats; j++) {
                    WdataToSend.push_back(task);
                    WdataChannel.push_back(ch);
                }
                if (buffered_beats < total_beats && !merged_command_first) {
                    fast_bypass_write_data_cnt[task] = total_beats - buffered_beats;
                }
                if (merged_command) {
                    top->parentMemorySystem->markMergedWrite(
                            task, write_merge_first_resp_task[task]);
                    write_merge_first_resp_task.erase(task);
                }
                auto bypassed = bypassed_merged_writes.find(task);
                if (bypassed != bypassed_merged_writes.end()) {
                    bypassed->second.dispatched = true;
                }
                if (DEBUG_BUS) {
                    PRINTN(setw(10)<<now()<<" -- RMW SCH :: task="<<RmwQue[i]->task<<" type="<<RmwQue[i]->transactionType<<" mask_write="<<RmwQue[i]->mask_wcmd<<" ecc_flag="<<RmwQue[i]->ecc_flag 
                            <<" qos="<<RmwQue[i]->qos<<" burst_length="<<RmwQue[i]->burst_length<<" channel="<<RmwQue[i]->channel<<" data_ready_cnt="<<RmwQue[i]->data_ready_cnt<<" address="<<hex<<RmwQue[i]->address
                            <<dec<<" rank="<<RmwQue[i]->rank<<" bank="<<RmwQue[i]->bankIndex<<" row="<<RmwQue[i]->row<<" col="<<RmwQue[i]->col<<" addr_col="<<RmwQue[i]->addr_col<<" rmw_mode="<<RMW_CMD_MODE
                            <<" rmw_cmd_cnt="<<rmw_cmd_cnt<<endl);
                }

                //update statistic info
                if (RmwQue[i]->transactionType == DATA_READ) {
                    rcmd_cnt --;
                } else {
                    wcmd_cnt --;
                }
                rmw_cmd_cnt --;

                //delete all states related to sended cmd this round
                delete RmwConfCnt[i];
                RmwConfCnt.erase(RmwConfCnt.begin() + i);
                delete RmwCmdState[i];
                RmwCmdState.erase(RmwCmdState.begin() + i);
                RmwQue.erase(RmwQue.begin() + i);
                rebuild_conflict_state();
                break;
            }
        } else {
            ERROR(setw(10)<<now()<<" -- Such Cmd Not Expected, task="<<RmwQue[i]->task<<" type="<<RmwQue[i]->transactionType
                   <<" address="<<hex<<RmwQue[i]->address<<" cmd_state="<<RmwCmdState[i]->rmwState<<" mask_write="<<RmwQue[i]->mask_wcmd
                   <<" rmw_mode="<<RMW_CMD_MODE);
            assert(0);
            
        }
    }

}


void Rmw::send_wdata() {
    if (!WdataToSend.empty() && !WdataChannel.empty()) {
        uint64_t task = WdataToSend[0];
        bool submit_ret = top->parentMemorySystem->submitData(NULL, task, false);
        if (submit_ret) {
            check_write_data(task);
            WdataToSend.erase(WdataToSend.begin());
            WdataChannel.erase(WdataChannel.begin());
        }
    }
}


unsigned Rmw:: GetRmwQsize() {
    return (rmw_cmd_cnt);
}

void Rmw::check_cnt() {

    uint32_t size = GetRmwQsize();
    rmw_que_cnt.at(size)++;
}

//void Rmw::register_write(uint64_t address, uint32_t data) {
//    uint32_t offset = ((address != 0) ? 4 : 0);
//    switch (offset) {
//        case 0x0:{
//            start_cycle = top->now();
//            break;
//        }
//        case 0x4:{
//            end_cycle = top->now();
//            if (start_cycle != end_cycle && STATE_LOG == true) {
//                statistics();
//            }
//            break;
//        }
//        default: break;
//    }
//}


void Rmw::statistics() {
    if (!STATE_LOG || !state_log) return;
    state_log << "======================================== RMW ==========================================" << endl;
    state_log << "Channel                 : " << channel << endl;
    state_log << "Read commands            : " << totalReads << endl;
    state_log << "Write commands           : " << totalWrites << endl;
    state_log << "Full writes              : " << totalFullWrites << endl;
    state_log << "Mask writes              : " << totalMaskWrites << endl;
    state_log << "Bypass reads             : " << totalBypassReads << endl;
    state_log << "Bypass writes            : " << totalBypassWrites << endl;
    state_log << "Total transactions       : " << totalTransactions << endl;
    state_log << "WCMD merge candidates    : " << merge_candidates << endl;
    state_log << "WCMD merge candidate cmds: " << merge_candidate_cmds << endl;
    state_log << "WCMD merge pairs         : " << merge_pairs << endl;
    state_log << "WCMD unpaired flushes    : " << merge_unpaired_flushes << endl;
    state_log << "WCMD merge remap beats   : " << merge_remap_beats << endl;
    state_log << "WCMD merge ratio         : " << fixed << setprecision(2)
              << (merge_candidate_cmds == 0 ? 0.0 : static_cast<double>(merge_pairs * 2) * 100.0 / merge_candidate_cmds)
              << "%" << endl;
    state_log << "=======================================================================================" << endl;
    state_log.flush();
}

/*
//    unsigned size = 0;
//    STATE_PRINTN(setiosflags(ios::left));
//    STATE_PRINTN("======================================== START ========================================\n");
//    STATE_PRINTN("-------------------- Base Message -----------------------------------------------------\n");
//    STATE_PRINTN(DDR_TYPE<<" "<<DMC_RATE<<"Mbps, x"<<JEDEC_DATA_BUS_BITS<<", DMC Data Width: "
//            <<DMC_DATA_BUS_BITS<<", CKR: "<<setprecision(1)<<WCK2DFI_RATIO<<endl);
//    STATE_PRINTN("Current time: "<<fixed<<now()<<", tDFI: "<<setprecision(4)<<tDFI<<endl);
//
//    unsigned reads = totalReads;
//    unsigned bypass_reads = totalBypassReads;
//    unsigned writes = totalWrites;
//    unsigned bypass_writes = totalBypassWrites;
//    unsigned full_writes = totalFullWrites;
//    unsigned mask_writes = totalMaskWrites;
//    unsigned totals = totalTransactions;
////    unsigned address_conf_cnt = addrconf_cnt;
////    unsigned read_cnt = read_cnt;
////    unsigned write_cnt = write_cnt;
////    unsigned mwrite_cnt = mwrite_cnt;
//
//    STATE_PRINTN("-------------------- Task Statistics (DMC Command Number) -----------------------------\n");
//    
//    STATE_PRINTN("Read            : "<<setw(8)<<reads-pre_reads);
//    STATE_PRINTN(" | Total reads       : "<<setw(8)<<reads);
//    STATE_PRINTN(" | Bypass Read            : "<<setw(8)<<bypass_reads-pre_bypass_reads);
//    STATE_PRINTN(" | Total bypass reads       : "<<setw(8)<<bypass_reads<<" | "<<endl);
//    STATE_PRINTN("Write           : "<<setw(8)<<writes-pre_writes);
//    STATE_PRINTN(" | Total writes      : "<<setw(8)<<writes);
//    STATE_PRINTN(" | Full Write             : "<<setw(8)<<full_writes-pre_full_writes);
//    STATE_PRINTN(" | Total full writes        : "<<setw(8)<<full_writes<<" | "<<endl);
//    STATE_PRINTN("Mask Write      : "<<setw(8)<<mask_writes-pre_mask_writes);
//    STATE_PRINTN(" | Total mask writes : "<<setw(8)<<mask_writes);
//    STATE_PRINTN(" | Bypass Write           : "<<setw(8)<<bypass_writes-pre_bypass_writes);
//    STATE_PRINTN(" | Total bypass writes      : "<<setw(8)<<bypass_writes<<" | "<<endl);
//    STATE_PRINTN("Total           : "<<setw(8)<<totals-pre_totals);
//    STATE_PRINTN(" | Total commands    : "<<setw(8)<<totals<<" | "<<endl);
//
//
//
////    STATE_PRINTN("-------------------- Confilct Statistics (DDR Command Number) -------------------------\n");
////    STATE_PRINTN(setw(36)<<"Address conflict"<<" : "<<setw(12)<<address_conf_cnt - pre_address_conf_cnt);
////    STATE_PRINTN(" | "<<setw(36)<<"Total address conf cnt"<<" : "<<address_conf_cnt<<endl);
//
//
////    STATE_PRINTN("-------------------- RMW Pressure Statistics (Percentage/Cycle) -----------------------\n");
////    float ratio = float(task_cnt) * 100 / STATE_TIME;
////    STATE_PRINTN(setw(15)<<"Cmd valid"<<" : "<<setw(10)<<task_cnt<<" | ");
////    STATE_PRINTN(setw(15)<<"Ratio"<<" : "<<setw(10)<<ratio<<" | ");
////    ratio = float(total_task_cnt) * 100 / now();
////    STATE_PRINTN(setw(15)<<"Total cmd valid"<<" : "<<setw(10)<<total_task_cnt<<" | ");
////    STATE_PRINTN(setw(15)<<"Ratio"<<" : "<<setw(10)<<ratio<<" | "<<endl);
////    ratio = float(bp_cnt) * 100 / (bp_cnt + access_cnt);
////    STATE_PRINTN(setw(15)<<"DMC access"<<" : "<<setw(10)<<access_cnt<<" | ");
////    STATE_PRINTN(setw(15)<<"Command bp"<<" : "<<setw(10)<<bp_cnt<<" | ");
////    STATE_PRINTN(setw(15)<<"Bp ratio"<<" : "<<setw(10)<<ratio<<" | "<<endl);
////    ratio = float(total_bp_cnt) * 100 / (total_bp_cnt + total_access_cnt);
////    STATE_PRINTN(setw(15)<<"Total access"<<" : "<<setw(10)<<total_access_cnt<<" | ");
////    STATE_PRINTN(setw(15)<<"Total bp"<<" : "<<setw(10)<<total_bp_cnt<<" | ");
////    STATE_PRINTN(setw(15)<<"Total bp ratio"<<" : "<<setw(10)<<ratio<<" | "<<endl);
//
//    STATE_PRINTN("-------------------- RMW: Queue Statistics (Percentage/Cycle) --------------------------\n");
//    uint32_t total = 0;
//    size = rmw_que_cnt.size();
//
//    for (uint32_t index = 0; index <= size; index ++) {
//        STATE_PRINTN("--------");
//    }
//    STATE_PRINTN(endl);
//    STATE_PRINTN(setw(7)<<"Qnum"<<"|");
//    for (uint32_t index = 0; index < size; index++) {
//        total += rmw_que_cnt.at(index);
//        STATE_PRINTN(setw(7)<<index<<"|");
//    }
//    STATE_PRINTN(endl);
//    STATE_PRINTN(setw(7)<<"Per"<<"|");
//    for (uint32_t index = 0; index < size; index ++) {
//        float cnt_dist_ratio = (float(rmw_que_cnt.at(index)) * 100) / total;
//        STATE_PRINTN(setw(7)<<fixed<<setprecision(3)<<cnt_dist_ratio<<"|");
//    }
//    STATE_PRINTN(endl);
//    STATE_PRINTN(setw(7)<<"Cycle"<<"|");
//    for (uint32_t index = 0; index < size; index ++) {
//        STATE_PRINTN(setw(7)<<fixed<<setprecision(3)<<rmw_que_cnt.at(index)<<"|");
//    }
//    STATE_PRINTN(endl);
//    for (uint32_t index = 0; index <= size; index ++) {
//        STATE_PRINTN("--------");
//    }
//    STATE_PRINTN(endl);
//
//    for (uint32_t index = 0; index < size; index++) {
//        rmw_que_cnt.at(index) = 0;
//    }
//
////    task_cnt = 0;
////    access_cnt = 0;
////    bp_cnt = 0;
//
//    //save the value of last time stataes
//    pre_reads = reads;
//    pre_bypass_reads = bypass_reads;
//    pre_writes = writes;
//    pre_bypass_writes = bypass_writes;
//    pre_full_writes = full_writes;
//    pre_mask_writes = mask_writes;
//    pre_totals = totals;
////    pre_address_conf_cnt = address_conf_cnt;
////    pre_read_cnt = read_cnt;
////    pre_write_cnt = write_cnt;
////    pre_mwrite_cnt = mwrite_cnt;
//
//
//    //clear statistics
//    STATE_PRINTN("========================================= END =========================================\n");
//    STATE_PRINTN("\n");
*/

void Rmw::track_write_command(uint64_t task, unsigned beats) {
    if (beats != 0) wdata_order_queue.push_back(WdataOrderEntry(task, beats));
}

void Rmw::check_write_data(uint64_t task) {
    if (wdata_order_queue.empty() || wdata_order_queue.front().task != task) {
        ERROR(setw(10)<<now()<<" -- RMW WDATA ORDER ERROR :: actual_task="<<task
                <<" expected_task="<<(wdata_order_queue.empty() ? 0xffffffffffffffffull : wdata_order_queue.front().task));
        assert(0);
    }
    if (--wdata_order_queue.front().remaining_beats == 0) wdata_order_queue.erase(wdata_order_queue.begin());
}

bool Rmw::hasPendingWork() const {
    return !WdataToSend.empty() || !RmwQue.empty() || !pending_write_data_cnt.empty()
            || !write_merge_data_remaps.empty() || !fast_bypass_write_data_cnt.empty()
            || !bypassed_merged_writes.empty() || !wdata_order_queue.empty();
}

bool Rmw::flushWriteMergeBuffer() {
    for (size_t i = 0; i < RmwQue.size(); i++) {
        if (is_write_merge_candidate(RmwQue[i])) {
            RmwCmdState[i]->rmwState = SEND_READY;
            merge_unpaired_flushes++;
            return true;
        }
    }
    return false;
}

}
