/* $$$!!Warning: Huawei key information asset. No spread without permission.$$$ */
/* CODEMARK:RKeR1B8WMAfemkt1tTDGp4eOEddgxKn4NOPmdw0w+6Q3n1pxgDEX+kGBiRV20e1NKuLwOh60qWwx
7DOUvTqsDpJdC/G6ahMCQuRlwWqc+IGKquH6vaaGAGe1zSmcLn5FMd2VBk0upEP5xKZPTVuBjKnw
SvZMzBtMrQ+w1lxbG5+EFWux51V2bvtZUTAAA+en/pM7ZB5Cy3u0JTs1VqxXwg9JKb2NmsjcFl2t
UANBFF41iJl1A4UI0lzvbI1C4R4GWOfr75zesPsAD2SGQsAfjGNddesuiUgd8ZvMkoaPKZWZRQoN
QIPpNiAcMuw9fns8# */
/* $$$!!Warning: Deleting or modifying the preceding information is prohibited.$$$ */
#include <stdio.h>
#include "test.h"
#include <list>
#include <time.h>
#include <bitset>
#include <random>
#include <set>
#include <string>
#include <cmath>
#include <fstream>
#include <iomanip>

using namespace LPDDRSim;

LPMemorySystemTop *mem;
vector <vector <hha_command>> CommandQueue;
vector <hha_command> CommandQueueTest;
typedef std::list<int> LISTINT;
std::vector <wdata> write_task;
std::vector <std::map <uint64_t, uint32_t>> OutstandingQueue;
std::map <uint64_t, uint32_t> OutstandingQueueTest;
unsigned cmd_rr_channel = 0;
std::vector <vector <fastwakeup>> FastWakeupQueue;

uint32_t data_cnt = 0;
uint64_t task_cnt = 0;          // 物理事务总数 (拆分后)
uint64_t logical_task_cnt = 0;  // 逻辑事务总数 (拆分前，用于对比 MATCH_TASK_LIMIT)
uint64_t r_task = 0;
uint64_t cnt = 0;
uint64_t total_bytes = 0;
uint64_t trace_send_cnt = 0;
uint32_t pre_channel = 0;
uint64_t read_cmd_send_cnt = 0;
uint64_t write_cmd_send_cnt = 0;
uint64_t read_data_resp_cnt = 0;
uint64_t write_resp_cnt = 0;
uint64_t match_task_limit = 10000;
unsigned sim_random_seed = 1;

// ========================================================
// 🔥 新增：连续写 Burst 定制化控制参数
// ========================================================
unsigned SEQ_WR_RATIO = 0; 
unsigned SEQ_WR_LEN = 8;   

// ========================================================
// 🔥 新增：物理地址绕过直通模式参数
// ========================================================
bool BYPASS_ADDR_MAP_EN = false; 
unsigned ROTA_ROW_HITS = 2;
unsigned ROW_HIT_GAP = 15;
unsigned ROTA_BANKS = 14;        
unsigned ROTA_GROUPS = 4;        

// ========================================================
// 🔥 新增：Trace 导出与动态命名参数
// ========================================================
std::ofstream dump_trace_file; 
bool DUMP_TRACE_EN = false;    
std::string dmc_rate_str = "Unknown";
std::string wr_ratio_str = "Unknown";
std::string data_size_str = "Unknown";
// ========================================================

std::vector<std::set<uint64_t>> SentReadTasks;
std::vector<std::set<uint64_t>> SentWriteTasks;
std::vector<std::set<uint64_t>> CompletedReadTasks;
std::vector<std::set<uint64_t>> CompletedWriteTasks;

bool all_command_queues_empty() {
    if (!CommandQueueTest.empty()) return false;
    for (auto &q : CommandQueue) {
        if (!q.empty()) return false;
    }
    return true;
}

bool outstanding_empty() {
    if (!OutstandingQueueTest.empty()) return false;
    for (auto &q : OutstandingQueue) {
        if (!q.empty()) return false;
    }
    return true;
}

bool memory_pending_empty() {
    return mem == NULL || !mem->hasPendingWork();
}

bool memory_accepts_transaction() {
    for (auto channel : mem->channels) {
        if (channel->WillAcceptTransaction()) return true;
    }
    return false;
}

void flush_write_merge_buffers() {
    mem->flushWriteMergeBuffers();
}

ifstream file;

float calc_effi() {
    if (IS_G3D) {
        return (float(100 * total_bytes * 8) / cnt / 2 / JEDEC_DATA_BUS_BITS
                / WCK2DFI_RATIO / PAM_RATIO / float(NUM_GROUPS) / float(NUM_CHANS));
    } else if (IS_LP6) {
        return (float(100 * total_bytes * 8) / cnt / 2 / JEDEC_DATA_BUS_BITS
                / WCK2DFI_RATIO / PAM_RATIO * 9 / 8 / float(NUM_CHANS));
    } else {
        return (float(100 * total_bytes * 8) / cnt / 2 / JEDEC_DATA_BUS_BITS
                / WCK2DFI_RATIO / PAM_RATIO / float(NUM_CHANS));
    }
}

/* callback functors */
bool some_object::read_data(unsigned channel, uint64_t task, double readDataEnterDmcTime,
        double reqAddToDmcTime, double reqEnterDmcBufTime) {
    total_bytes += DMC_DATA_BUS_BITS / 8;
    read_data_resp_cnt ++;
    if (LATENCY_MODE) {
        float latency = 0;
        float efficiency = 0;
        static uint32_t loadlatency_cnt = 0;
        static unsigned loadlatency_cycle = 0;
        static float total_latency = 0;

        if (task >= 0xF000000000000000) {
            auto it = OutstandingQueueTest.find(task);
            if (it != OutstandingQueueTest.end()) {
                loadlatency_cnt ++;
                total_latency += ceil((readDataEnterDmcTime - reqEnterDmcBufTime) / tDFI);
                if (loadlatency_cnt == T_NUM) {
                    loadlatency_cycle = total_latency * tDFI / 0.3195;
                    DEBUG("Core 0 exit after "<<loadlatency_cycle<<" cycles, "
                        <<loadlatency_cnt<<" instructions, 16921 uops, 0.0011 ipc");
                    latency = float(total_latency) / loadlatency_cnt;
                    efficiency = calc_effi();
                    DEBUG("Done, time: "<<cnt<<", efficiency: "<<fixed<<setprecision(2)<<efficiency
                            <<"%, average latency: "<<latency<<", tDFI: "<<tDFI<<"ns");
                    DEBUG("Power Consumption: "<<fixed<<mem->channels[0]->memoryController->calc_power());
                    mem->~LPMemorySystemTop();
                    exit(0);
                }
                OutstandingQueueTest.erase(task);
            }
        }
    }
    if (task < 0xF000000000000000 && channel < OutstandingQueue.size()) {
        if (SentReadTasks[channel].find(task) == SentReadTasks[channel].end()) {
            ERROR(setw(10)<<cnt<<" -- read data response task was not sent. task="<<task<<", channel="<<channel);
            assert(0);
        }
        auto it = OutstandingQueue[channel].find(task);
        if (it == OutstandingQueue[channel].end()) {
            ERROR(setw(10)<<cnt<<" -- read data response task is not outstanding. task="<<task<<", channel="<<channel);
            assert(0);
        }
        it->second --;
        if (it->second == 0) {
            OutstandingQueue[channel].erase(it);
            CompletedReadTasks[channel].insert(task);
        }
    }
    return true;
}

bool some_object::write_response(unsigned channel, uint64_t task, double readDataEnterDmcTime_,
        double reqAddToDmcTime_, double reqEnterDmcBufTime_) {
    write_resp_cnt ++;
    for (auto w : write_task) {
        if (task == w.task) {
            ERROR(setw(10)<<cnt<<" -- task="<<task<<", Wresp receive before all wdata send out!");
            assert(0);
        }
    }
    if (task < 0xF000000000000000 && channel < OutstandingQueue.size()) {
        if (SentWriteTasks[channel].find(task) == SentWriteTasks[channel].end()) {
            ERROR(setw(10)<<cnt<<" -- write response task was not sent. task="<<task<<", channel="<<channel);
            assert(0);
        }
        auto it = OutstandingQueue[channel].find(task);
        if (it == OutstandingQueue[channel].end()) {
            ERROR(setw(10)<<cnt<<" -- write response task is not outstanding. task="<<task<<", channel="<<channel);
            assert(0);
        }
        OutstandingQueue[channel].erase(it);
        CompletedWriteTasks[channel].insert(task);
    }
    return true;
}

bool some_object::read_response(unsigned channel, uint64_t task, double readDataEnterDmcTime,
        double reqAddToDmcTime, double reqEnterDmcBufTime) {
    return true;
}

bool some_object::cmd_response(unsigned channel, uint64_t task, double readDataEnterDmcTime,
        double reqAddToDmcTime, double reqEnterDmcBufTime) {
    return true;
}

void get_line() {
    string line;
    uint64_t f_address = 0;
    double f_time = 0;
    string f_type;
    uint32_t f_data_length, f_burst_len, f_mid, f_ch_num, f_qos, f_gid, f_merge_flag;
    static double start_time = 0;
    uint32_t line_number = 0;

    while (getline(file, line)) {
        if (TRACE_CMD_NUM != 0 && line_number > TRACE_CMD_NUM) break;
        
        // 1. 跳过空行和以 '#' 开头的表头注释行
        if (line.empty() || line[0] == '#') continue;

        // 2. 使用 istringstream 自动按空白字符（空格/Tab）智能分割
        istringstream iss(line);
        
        // 按照 trace 的 10 个字段依次解析
        // #type, address, trans_size, burst_len, mid, ATIME, ch_num, qos, gid, merge_flag
        if (!(iss >> f_type >> dec >> f_address >> f_data_length >> f_burst_len 
                  >> f_mid >> f_time >> f_ch_num >> f_qos >> f_gid >> f_merge_flag)) {
            // 如果某一行字段不够，直接跳过
            continue; 
        }

        if (line_number == 0) start_time = f_time;
        f_time = f_time - start_time;

        // lp5 adjust adress to 32B aligned
        f_address = f_address & (~(static_cast<uint64_t>(DMC_DATA_BUS_BITS)/8 - 1));
        
        bool valid_command = false;
        if (TRACE_EN) {
            if (f_ch_num <= VLD_CH_NUM) valid_command = true;
        } else {
            // ... (保持原有的交叉交织过滤逻辑不变) ...
            unsigned intlv_ch_num = 0;
            if (CH_XOR_EN) {
                if (CHINTLV_BIT == 2) { 
                    intlv_ch_num = bitset<64>(f_address).test(7) ^ bitset<64>(f_address).test(9)
                            ^ bitset<64>(f_address).test(11) ^ bitset<64>(f_address).test(13);
                    intlv_ch_num |= (bitset<64>(f_address).test(8) ^ bitset<64>(f_address).test(10)
                            ^ bitset<64>(f_address).test(12) ^ bitset<64>(f_address).test(14)) << 1;
                } else if (CHINTLV_BIT == 3) { 
                    intlv_ch_num = bitset<64>(f_address).test(7) ^ bitset<64>(f_address).test(10)
                            ^ bitset<64>(f_address).test(13);
                    intlv_ch_num |= (bitset<64>(f_address).test(8) ^ bitset<64>(f_address).test(11)
                            ^ bitset<64>(f_address).test(14)) << 1;
                    intlv_ch_num |= (bitset<64>(f_address).test(9) ^ bitset<64>(f_address).test(12)) << 2;
                } else {
                    ERROR("CHINTLV_BIT must be 2 or 3, now is "<<CHINTLV_BIT);
                    assert(0);
                }
            } else {
                for (size_t i = CHINTLV_START; i < (CHINTLV_START + CHINTLV_BIT); i ++) {
                    intlv_ch_num = (intlv_ch_num << 1) | bitset<64>(f_address).test(i);
                }
            }
            if (intlv_ch_num <= VLD_CH_NUM) valid_command = true;
        }

        if (valid_command) {
            if (task_cnt % TRACE_Q_MAX_CNT == 0) {
                CommandQueue.resize(task_cnt / TRACE_Q_MAX_CNT + 1);
            }
            if (r_task % TRACE_Q_MAX_CNT == 0) {
                FastWakeupQueue.resize(r_task / TRACE_Q_MAX_CNT + 1);
            }
            hha_command transaction;
            if (TRACE_EN) {
                transaction.address = f_address;
            } else {
                unsigned start_bit = 0;
                if (CH_XOR_EN) start_bit = 7;
                else start_bit = CHINTLV_START;
                unsigned addr_mask_low = 0;
                for (size_t i = 0; i < start_bit; i ++) {
                    addr_mask_low = (addr_mask_low << 1) | 1;
                }
                transaction.address = (f_address & addr_mask_low) |
                        ((f_address>>(start_bit + CHINTLV_BIT))<<start_bit);
            }
            
            transaction.burst_length = ceil(float(f_data_length) * 8 / DMC_DATA_BUS_BITS) - 1;
            transaction.task = task_cnt;
            transaction.id = task_cnt % 1000000;
            
            // 3. 兼容新的 Type 判断 (0=读, 1=写)
            if (f_type == "1" || f_type == "nw" || f_type == "naw") {
                transaction.type = 1;
            } else if (f_type == "0" || f_type == "nr" || f_type == "wr") {
                transaction.type = 0;
                transaction.wrap_cmd = (f_type == "wr") ? true : false;
            } else {
                ERROR("Error command type: "<<f_type);
                assert(0);
            }
            
            transaction.qos = f_qos;
            transaction.mid = f_mid;
            transaction.channel = f_ch_num;
            
            if(RMW_ENABLE && transaction.type==1) {
                transaction.mask_wcmd = false;
            }
            transaction.pri = 0x0;
            transaction.cmd_rt_type = false;
            transaction.reqEnterDmcBufTime = double(ceil(f_time * GEAR_RATIO / tDFI));
            transaction.pf_type = rand() % 4;
            transaction.sub_pftype = rand() % 13;
            transaction.sub_src = rand() % 4;
            
            CommandQueue[task_cnt / TRACE_Q_MAX_CNT].push_back(transaction);
            OutstandingQueue[transaction.channel][transaction.task] = transaction.type == DATA_READ ?
                    (transaction.burst_length + 1) : 1;
            task_cnt ++;
            
            if (FASTWAKEUP_CYCLE > 0 && transaction.type == 0) {
                fastwakeup fw;
                fw.rank = bitset<64>(MATRIX_RA0 & transaction.address).count() & 1;
                if ((unsigned)transaction.reqEnterDmcBufTime <= FASTWAKEUP_CYCLE) {
                    fw.send_time = 0;
                } else {
                    fw.send_time = ((unsigned)transaction.reqEnterDmcBufTime-FASTWAKEUP_CYCLE);
                }
                FastWakeupQueue[r_task / TRACE_Q_MAX_CNT].push_back(fw);
                r_task ++;
            }
        }
        line_number ++;
    }
}

// =========================================================================
// 🔥 彻底重构的发包核心: rand_command 
// =========================================================================
void rand_command(LPMemorySystemTop *ddrc, bool is_test_cmd) {
    // ✅ 修复 1：将轮转计数器和状态记忆矩阵放在函数最外层，保证全局可见且持久化！
    static unsigned rota_idx = 0;
    
    // 用于记忆每个 Bank 当前处于哪个 Epoch，以及当前的 Row 是多少
    static unsigned bank_row_epoch[32]; 
    static unsigned bank_current_row[32];
    static bool init_flag = false;
    if (!init_flag) {
        for(int i = 0; i < 32; i++) bank_row_epoch[i] = 0xFFFFFFFF; // 初始化为无效 Epoch
        init_flag = true;
    }

    hha_command transaction;
    unsigned data_size = (SEQ_DATA_SIZE == 0) ? DATA_SIZE : SEQ_DATA_SIZE;
    unsigned align = log2(data_size * SEQ_NUM);
    if(align < log2(DATA_SIZE)) align = log2(DATA_SIZE);

    static uint64_t r_address = 0x0;
    static uint64_t w_address = 0x00000400;
    static unsigned seq_cnt = 0;
    static bool is_read = true;
    bool is_rank0 = (unsigned(rand()) % 100 >= RK_SW_RATIO);

    if (is_test_cmd) {
        transaction.burst_length = DATA_SIZE * 8 / DMC_DATA_BUS_BITS - 1;
        transaction.id = task_cnt % 1000000;
        transaction.wrap_cmd = false;
        transaction.pri = 0x0;
        transaction.cmd_rt_type = false;
        transaction.pf_type = rand() % 4;
        transaction.sub_pftype = rand() % 13;
        transaction.sub_src = rand() % 4;
        transaction.task = task_cnt | 0xF000000000000000;
        transaction.type = DATA_READ;
        transaction.qos = 3;
        transaction.reqEnterDmcBufTime = double(cnt + BKD_DLY);
        if (OutstandingQueueTest.size() < T_OSTD) {
            CommandQueueTest.push_back(transaction);
            OutstandingQueueTest[transaction.task] += (transaction.burst_length + 1);
            task_cnt ++;
            logical_task_cnt ++;
        }
        return;
    }

    static uint64_t total_reads_gen = 0;
    static uint64_t total_writes_gen = 0;
    static uint64_t seq_writes = 0;
    static unsigned seq_wr_remaining = 0;
    static uint64_t last_w_address = 0;
    static unsigned last_w_channel = 0;
    bool start_new_burst = false;

    transaction.bypass_addrmapping = false; 

    // =========================================================================
    // 🚀 模式 C: 物理地址直接注入轮转 (支持 IECC 逆向，支持任意 Bank 数与 Row Hit 控制)
    // =========================================================================
    // =========================================================================
    // 🚀 模式 C: 物理地址直接注入轮转 (丢番图方程强制前端解耦版)
    // =========================================================================
    // =========================================================================
    // 🚀 模式 C: 物理地址直接注入轮转 (支持跨 Group 交织，突破 tCCD_L 瓶颈)
    // =========================================================================
    if (BYPASS_ADDR_MAP_EN) {
            transaction.bypass_addrmapping = true;
            transaction.sc = 0;
            transaction.sid = 0;
            transaction.channel = 0; 
            
            // ==========================================
            // 🎯 步骤 1：定义完美打散的物理目标 Bank 序列 (支持动态可配 Row Hit 间隔)
            // ==========================================
            // 引入新参数: ROW_HIT_GAP (同一 Bank 两次连续命中之间的命令间隔)
            // 安全限制：纯轮转不插气泡的情况下，间隔最大不能超过参与轮转的 Bank 总数
            unsigned gap = (ROW_HIT_GAP > 0) ? ROW_HIT_GAP : ROTA_BANKS; 
            if (gap > ROTA_BANKS) gap = ROTA_BANKS; 

            // 数学降维：将 ROTA_BANKS 切分为多个 chunk，最后不足 gap 的 chunk 不回绕
            unsigned num_chunks = (ROTA_BANKS + gap - 1) / gap;
            if (num_chunks == 0) num_chunks = 1;
            unsigned chunk_size_list[64];
            unsigned commands_per_epoch = 0;
            for (unsigned i = 0; i < num_chunks; i++) {
                unsigned chunk_start = i * gap;
                unsigned remain_banks = ROTA_BANKS - chunk_start;
                chunk_size_list[i] = remain_banks < gap ? remain_banks : gap;
                commands_per_epoch += chunk_size_list[i] * ROTA_ROW_HITS;
            }
            if (commands_per_epoch == 0) commands_per_epoch = 1;

            unsigned current_epoch = rota_idx / commands_per_epoch;
            unsigned idx_in_epoch = rota_idx % commands_per_epoch;
            unsigned current_chunk = 0;
            while (current_chunk + 1 < num_chunks && idx_in_epoch >= chunk_size_list[current_chunk] * ROTA_ROW_HITS) {
                idx_in_epoch -= chunk_size_list[current_chunk] * ROTA_ROW_HITS;
                current_chunk++;
            }
            unsigned chunk_size = chunk_size_list[current_chunk];
            if (chunk_size == 0) chunk_size = 1;
            unsigned idx_in_chunk = idx_in_epoch % chunk_size;

            // 映射到全局的逻辑 Bank 序号 (0 ~ ROTA_BANKS-1)，不再用 % ROTA_BANKS 回绕
            unsigned seq_idx = current_chunk * gap + idx_in_chunk;

            // 生成物理 Group 和 Bank 映射 (保持你原有的最优交织打散逻辑)
            unsigned target_group = seq_idx % 4;
            unsigned target_bank  = seq_idx / 4;
            unsigned target_bankIndex = target_group * 4 + target_bank; // 生成 0,4,8,12,1,5,9...

            // 判定并更新当前 Epoch 的 Row 状态
            if (bank_row_epoch[target_bankIndex] != current_epoch) {
                bank_current_row[target_bankIndex] = rand() % 65536;
                bank_row_epoch[target_bankIndex] = current_epoch;
            }
            unsigned target_row = bank_current_row[target_bankIndex];
            unsigned target_rank  = 0;                              
            
            unsigned capacity_ratio = 960; 
            unsigned target_col = (rand() % capacity_ratio) & ~0x3F;

        // ==========================================
        // 🧮 步骤 2：逆向解 IECC 方程
        // ==========================================
        uint64_t linear_addr = (uint64_t(target_row) * 16 + target_bankIndex) * capacity_ratio + target_col;

        unsigned initial_col = linear_addr % 1024;
        unsigned initial_bankIndex = (linear_addr / 1024) % 16;
        unsigned initial_row = (linear_addr / 1024) / 16;

        // ==========================================
        // 🧩 步骤 3：给 Transaction 喂入伪装前端地址
        // ==========================================
        transaction.rank      = target_rank;
        transaction.group     = initial_bankIndex / 4;
        transaction.bank      = initial_bankIndex % 4;
        //transaction.bankIndex = initial_bankIndex;  // ✅ 就是漏了这致命的一行！！！
        transaction.row       = initial_row;
        transaction.col       = initial_col;

        transaction.address = (uint64_t(target_rank)  << 32) | 
                              (uint64_t(target_row)   << 16) | 
                              (uint64_t(target_bankIndex % 4) << 12) | 
                              (uint64_t(target_bankIndex / 4) << 10) | 
                              target_col;

        is_read = (unsigned(rand() % 100) >= WR_RATIO);
    }
    // =========================================================================
    // 💥 模式 A: 自定义写连续 Burst (SEQ_WR_RATIO > 0)
    // =========================================================================
    else if (SEQ_WR_RATIO > 0) {
        align = log2(DATA_SIZE); 
        
        if (seq_wr_remaining > 0) {
            is_read = false;
            last_w_address += DATA_SIZE;
            transaction.address = last_w_address;
            transaction.channel = last_w_channel;
        } else {
            uint64_t total_cmds = total_reads_gen + total_writes_gen;
            unsigned current_wr_ratio = (total_cmds == 0) ? 0 : (total_writes_gen * 100 / total_cmds);
            
            if (current_wr_ratio > WR_RATIO) is_read = true;
            else if (current_wr_ratio < WR_RATIO) is_read = false;
            else is_read = (unsigned(rand() % 100) >= WR_RATIO);

            uint64_t addr_rand = ((uint64_t(rand()) << 45 | uint64_t(rand()) << 30 | uint64_t(rand()) << 15 | uint64_t(rand()))
                    & (0xFFFFFFFFFFFFFFFF << align)) % uint64_t(uint64_t(DRAM_CAPACITY) * 1024 * 1024 * 1024 / 8);

            if (RK_SW_RATIO > 0) {
                if (is_rank0) transaction.address = addr_rand & ~MATRIX_RA0;
                else { 
                    transaction.address = addr_rand | MATRIX_RA0;
                    if (IS_LP6 && EM_ENABLE && EM_MODE==2) transaction.address &= ~MATRIX_ROW15;
                }
            } else {
                transaction.address = addr_rand & ~MATRIX_RA0;
            }

            if (NUM_CHANS > 1) transaction.channel = rand() % NUM_CHANS;
            else transaction.channel = 0; 

            if (!is_read) {
                unsigned current_seq_ratio = (total_writes_gen == 0) ? 0 : (seq_writes * 100 / total_writes_gen);
                if (current_seq_ratio < SEQ_WR_RATIO) start_new_burst = true;
            }
        }
    } 
    // =========================================================================
    // 💤 模式 B: 原有兜底逻辑
    // =========================================================================
    else {
        if (SEQ_NUM == 0 && !is_test_cmd) {
            is_read = (unsigned(rand() % 100) >= WR_RATIO);
            if (is_read) transaction.address = r_address + data_size;
            else transaction.address = w_address + data_size;
        } else {
            if (seq_cnt == 0) {
                is_read = (unsigned(rand() % 100) >= WR_RATIO);
                uint64_t addr_rand = ((uint64_t(rand()) << 45 | uint64_t(rand()) << 30 | uint64_t(rand()) << 15 | uint64_t(rand()))
                        & (0xFFFFFFFFFFFFFFFF << align)) % uint64_t(uint64_t(DRAM_CAPACITY) * 1024 * 1024 * 1024 / 8);
                if (RK_SW_RATIO > 0) {
                    if (is_rank0) {
                        transaction.address = addr_rand & ~MATRIX_RA0;
                    } else { 
                        transaction.address = addr_rand | MATRIX_RA0;
                        if (IS_LP6 && EM_ENABLE && EM_MODE==2) {
                            transaction.address = transaction.address & ~MATRIX_ROW15;
                        }
                    }
                } else {
                    transaction.address = addr_rand & ~MATRIX_RA0;
                }
            } else {
                if (is_read) transaction.address = r_address + DATA_SIZE;
                else transaction.address = w_address + DATA_SIZE;
            }
        }
        if(NUM_CHANS>1) transaction.channel = rand() % NUM_CHANS;
        else transaction.channel = 0; 
    }

    if (SAME_ADDR_MODE && !BYPASS_ADDR_MAP_EN) transaction.address = 0;

    // =========================================================================
    // 🔥 终极防弹版：真实物理下发 256B -> 两个 128B 的拆分识别逻辑 
    // =========================================================================
    transaction.type = is_read ? DATA_READ : DATA_WRITE;
    
    // 无视底层参数解析冲突，强制采用你命令行中的 data_size_str 或 DATA_SIZE 进行联合兜底判定
    bool need_split = false;
    if (MATCH_MODE && DUMP_TRACE_EN && !is_read) {
        if (DATA_SIZE == 256 || data_size_str == "256" || data_size == 256) {
            need_split = true;
        }
    }

    // 动态调整 burst_length：只要触发拆分，物理指令必定变成 128B 容量的拍数
    if (need_split) {
        transaction.burst_length = (128 * 8 / DMC_DATA_BUS_BITS) - 1;
    } else {
        transaction.burst_length = (DATA_SIZE * 8 / DMC_DATA_BUS_BITS) - 1;
    }

    transaction.wrap_cmd = false;
    transaction.pri = 0x0;
    transaction.cmd_rt_type = false;
    transaction.pf_type = rand() % 4;
    transaction.sub_pftype = rand() % 13;
    transaction.sub_src = rand() % 4;
    transaction.qos = is_read ? 3 : 0;
    
    if (!is_read) {
        transaction.mask_wcmd = (unsigned(rand()) % 100 < MASK_WR_RATIO);
    }
    transaction.reqEnterDmcBufTime = double(cnt + T_DLY);
    
    bool chan_credit = OutstandingQueue[transaction.channel].size() < BKD_OSTD;
    if (!chan_credit && NUM_CHANS > 1 && seq_wr_remaining == 0 && !BYPASS_ADDR_MAP_EN) {
        for (size_t offset = 1; offset < OutstandingQueue.size(); offset++) {
            unsigned alt_channel = (transaction.channel + offset) % OutstandingQueue.size();
            if (OutstandingQueue[alt_channel].size() < BKD_OSTD) {
                transaction.channel = alt_channel;
                chan_credit = true;
                break;
            }
        }
    }
    
    if (chan_credit) {
        if (seq_wr_remaining == 0 && transaction.channel == pre_channel && VLD_CH_NUM > 0 && NUM_CHANS == 2 && !BYPASS_ADDR_MAP_EN) {
            transaction.channel = transaction.channel ^ 0x00000001; 
        }
        pre_channel = transaction.channel;
        
        // 🔥 控制器真实物理下发：如果拆分，循环两次向硬件推两笔指令！
        int num_pkts = need_split ? 2 : 1;

        for (int p = 0; p < num_pkts; ++p) {
            hha_command current_trans = transaction;
            
            // 若为拆分出来的第二笔，物理地址自动向后偏移 128 字节
            if (p == 1) {
                current_trans.address += 128;
            }
            
            current_trans.task = task_cnt;
            current_trans.id = task_cnt % 1000000;
            
            // 真实物理队列下发！(这里会被执行两次)
            CommandQueue[current_trans.channel].push_back(current_trans);
            OutstandingQueue[current_trans.channel][current_trans.task] = is_read ? (current_trans.burst_length + 1) : 1;
            task_cnt ++;
            
            // 🔥 同步进行 Trace 记录写入
            if (DUMP_TRACE_EN && dump_trace_file.is_open()) {
                unsigned trans_size = (current_trans.burst_length + 1) * (DMC_DATA_BUS_BITS / 8);
                std::string type_str = (current_trans.type == DATA_READ) ? "nr" : "nw";
                
                double current_time_ns = current_trans.reqEnterDmcBufTime * tDFI;
                static double last_time_ns = 0;
                double delay_ns = current_time_ns - last_time_ns;
                
                // 确保两笔拆分的激励算在同一时刻并行触发：后一笔的相对 delay 强制设为 0
                if (p == 1) delay_ns = 0.000;
                else last_time_ns = current_time_ns; 

                dump_trace_file << type_str << " " 
                                << std::hex << current_trans.address << std::dec << " "
                                << trans_size << " "
                                << current_trans.burst_length << " "
                                << "0" << " "
                                << std::fixed << std::setprecision(3) << delay_ns << " "
                                << current_time_ns << " "
                                << current_trans.channel << " "
                                << current_trans.qos << " "
                                << "0" << " "
                                << dmc_rate_str << "\n";
            }
        }
        
        // ---- 逻辑状态推进 ----
        logical_task_cnt++; // 每次进入 chan_credit 只增加一次逻辑任务数
        
        // ✅ 修复 3：只有成功进入队列，轮转指针才允许往下走！
        if (BYPASS_ADDR_MAP_EN) {
            rota_idx++;
        }
        else if (SEQ_WR_RATIO > 0) {
            if (is_read) {
                total_reads_gen++;
                r_address = transaction.address;
                r_task ++;
            } else {
                total_writes_gen++;
                if (seq_wr_remaining > 0) {
                    seq_wr_remaining--;
                    last_w_address = need_split ? (transaction.address + 128) : transaction.address;
                } else if (start_new_burst) {
                    seq_wr_remaining = SEQ_WR_LEN - 1;
                    seq_writes += SEQ_WR_LEN;
                    last_w_address = need_split ? (transaction.address + 128) : transaction.address;
                    last_w_channel = transaction.channel; 
                }
                w_address = need_split ? (transaction.address + 128) : transaction.address;
            }
        } else {
            seq_cnt ++;
            if (seq_cnt == SEQ_NUM) seq_cnt = 0;
            if (is_read) {
                r_address = transaction.address;
                r_task ++;
            } else {
                w_address = need_split ? (transaction.address + 128) : transaction.address;
            }
        }
    }
}

void send_command(LPMemorySystemTop *ddrc) {
    hha_command transaction;
    bool test_cmd_time_met = true;

    if (!CommandQueueTest.empty()) { 
        transaction = CommandQueueTest.at(0);
        if (cnt >= uint64_t(transaction.reqEnterDmcBufTime)) {
            test_cmd_time_met = false;
            bool ret = ddrc->addTransaction(transaction);
            if (ret) {
                if (transaction.type == DATA_READ) {
                    SentReadTasks[transaction.channel].insert(transaction.task);
                    read_cmd_send_cnt ++;
                } else {
                    SentWriteTasks[transaction.channel].insert(transaction.task);
                    write_cmd_send_cnt ++;
                }
                if (transaction.type) {
                    for (size_t i = 0; i < transaction.burst_length + 1; i ++) {
                        wdata w_data;
                        w_data.task = transaction.task;
                        w_data.delay = cnt + WDATA_DLY;
                        w_data.ch = transaction.channel;
                        write_task.push_back(w_data);
                        data_cnt ++;
                    }
                }
                CommandQueueTest.erase(CommandQueueTest.begin());
            }
        }
    }

    unsigned size = CommandQueue.size();
    for (size_t offset = 0; offset < size; offset ++) {
        size_t i = (cmd_rr_channel + offset) % size;
        if (CommandQueue[i].size() == 0) continue;
        if (test_cmd_time_met && !CommandQueue[i].empty()) {
            if (WRITE_BUFFER_ENABLE) {
                bool bus_rempty = true;
                for (auto cmd : CommandQueue[i]) {
                    if ((cnt + 40) < uint64_t(cmd.reqEnterDmcBufTime)) break;
                    if (cmd.type == DATA_WRITE) continue;
                    bus_rempty = false;
                    break;
                }
                if (FASTWAKEUP_CYCLE <= 0) mem->noc_read_inform(0, false, false, bus_rempty);
            }
            transaction = CommandQueue[i].at(0);

            if (cnt >= uint64_t(transaction.reqEnterDmcBufTime)) {
                bool ret = ddrc->addTransaction(transaction);
                if (ret) {
                    trace_send_cnt ++;
                    if (transaction.type == DATA_READ) {
                        SentReadTasks[transaction.channel].insert(transaction.task);
                        read_cmd_send_cnt ++;
                    } else {
                        SentWriteTasks[transaction.channel].insert(transaction.task);
                        write_cmd_send_cnt ++;
                    }
                    if (transaction.type) {
                        for (size_t i = 0; i < transaction.burst_length + 1; i ++) {
                            wdata w_data;
                            w_data.task = transaction.task;
                            w_data.delay = cnt + WDATA_DLY;
                            w_data.ch = transaction.channel;
                            write_task.push_back(w_data);
                            data_cnt ++;
                        }
                    }
                    CommandQueue[i].erase(CommandQueue[i].begin());
                    cmd_rr_channel = (i + 1) % size;
                }
            }
            break;
        }
    }
}

void send_wdata(LPMemorySystemTop *ddrc) {
    if (data_cnt != 0) {
        uint64_t task = write_task[0].task;
        unsigned ch = write_task[0].ch;
        if (cnt >= write_task[0].delay) {
            bool ret = ddrc->addData(NULL,ch,task);
            if (ret) {
                write_task.erase(write_task.begin());
                data_cnt --;
                total_bytes += DMC_DATA_BUS_BITS / 8;
            }
        }
    }
}

void parameter_check() {
    unsigned mode_cnt = 0;
    if (LATENCY_MODE) mode_cnt ++;
    if (MATCH_MODE) mode_cnt ++;
    if (TRACE_EN) mode_cnt ++;
    if (DOU_TRACE_EN) mode_cnt ++;
    if (mode_cnt > 1) {
        ERROR("Mode config true count great than 1!");
        ERROR("LATENCY_MODE: "<<LATENCY_MODE);
        ERROR("MATCH_MODE: "<<MATCH_MODE);
        ERROR("TRACE_EN: "<<TRACE_EN);
        ERROR("DOU_TRACE_EN: "<<DOU_TRACE_EN);
        assert(0);
    }
    if (SAME_ADDR_MODE && SAME_BA_RAND_ROW_MODE) {
        ERROR("Both SAME_ADDR_MODE & SAME_BA_RAND_ROW_MODE are true is not allowed!");
        assert(0);
    }
    if (FASTWAKEUP_CYCLE == 0) FASTWAKEUP_EN = false;
    else FASTWAKEUP_EN = true;
}

void print_message() {
    DEBUG("---------------------- Main Config Message ----------------------");
    DEBUG("LATENCY_MODE                     : "<<boolalpha<<LATENCY_MODE);
    DEBUG("MATCH_MODE                       : "<<boolalpha<<MATCH_MODE);
    DEBUG("BKD_OSTD                         : "<<boolalpha<<BKD_OSTD);
    DEBUG("TRACE_EN                         : "<<boolalpha<<TRACE_EN);
    DEBUG("DOU_TRACE_EN                     : "<<boolalpha<<DOU_TRACE_EN);
    DEBUG("TRACE                            : "<<boolalpha<<TRACE);
    DEBUG("GEAR_RATIO                       : "<<boolalpha<<GEAR_RATIO);
    DEBUG("SEQ_NUM                          : "<<boolalpha<<SEQ_NUM);
    DEBUG("DATA_SIZE                        : "<<boolalpha<<DATA_SIZE);
    DEBUG("RK_SW_RATIO                      : "<<boolalpha<<RK_SW_RATIO);
    DEBUG("WR_RATIO                         : "<<boolalpha<<WR_RATIO);
    
    DEBUG("SEQ_WR_RATIO (%)                 : "<<SEQ_WR_RATIO);
    DEBUG("SEQ_WR_LEN (Pkt)                 : "<<SEQ_WR_LEN);
    DEBUG("BYPASS_ADDR_MAP_EN               : "<<boolalpha<<BYPASS_ADDR_MAP_EN);
    DEBUG("ROTA_ROW_HITS                    : "<<ROTA_ROW_HITS);
    DEBUG("ROW_HIT_GAP                      : "<<ROW_HIT_GAP);
    DEBUG("ROTA_BANKS                       : "<<ROTA_BANKS);
    DEBUG("ROTA_GROUPS                      : "<<ROTA_GROUPS);
    DEBUG("DUMP_TRACE_EN                    : "<<boolalpha<<DUMP_TRACE_EN);
    
    DEBUG("------------------------ Now Build DDRC ------------------------");
}

void print_pass() {
    DEBUG("-----------------------------------------------------------------");
    DEBUG("Simulation Status: pass, Keyword and Place:\"PASSED\"");
    DEBUG("-----------------------------------------------------------------");
}

int main(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        std::string arg(argv[i]);
        size_t pos = arg.find('=');
        if (pos == std::string::npos) continue;
        std::string key = arg.substr(0, pos);
        std::string value = arg.substr(pos + 1);
        if (key == "SIM_RANDOM_SEED") sim_random_seed = unsigned(std::stoul(value));
        else if (key == "MATCH_TASK_LIMIT") match_task_limit = std::stoull(value);
        
        // 捕获动态命名的关键变量
        else if (key == "DMC_RATE") dmc_rate_str = value;
        else if (key == "WR_RATIO") wr_ratio_str = value;
        else if (key == "DATA_SIZE") data_size_str = value;
        
        else if (key == "SEQ_WR_RATIO") SEQ_WR_RATIO = unsigned(std::stoul(value));
        else if (key == "SEQ_WR_LEN") SEQ_WR_LEN = unsigned(std::stoul(value));
        else if (key == "BYPASS_ADDR_MAP_EN") BYPASS_ADDR_MAP_EN = (value == "true");
        else if (key == "ROTA_ROW_HITS") ROTA_ROW_HITS = unsigned(std::stoul(value));
        else if (key == "ROW_HIT_GAP") ROW_HIT_GAP = unsigned(std::stoul(value));
        else if (key == "ROTA_BANKS") ROTA_BANKS = unsigned(std::stoul(value));
        else if (key == "ROTA_GROUPS") ROTA_GROUPS = unsigned(std::stoul(value));
        else if (key == "DUMP_TRACE_EN") DUMP_TRACE_EN = (value == "true");
    }
    
    srand(sim_random_seed);
    get_param_path(argc, argv);
    build_cfg();
    update_cfg(argc, argv);
    print_message();

    // =========================================================================
    // 🔥 根据参数动态生成 .log 文件名并写入带有新格式化字段的表头
    // =========================================================================
    if (DUMP_TRACE_EN) {
        std::string filename = "trace_rate" + dmc_rate_str + "_ds" + data_size_str + "_wr" + wr_ratio_str + ".log";
        dump_trace_file.open(filename, std::ios::out);
        if (dump_trace_file.is_open()) {
            dump_trace_file << "type,address,trans_size,burst_len,mid,delay(ns),ATIME,ch_num,qos,gid,freq\n";
            DEBUG("DUMP_TRACE_EN is ON. Generated commands will be saved to '" << filename << "'");
        } else {
            ERROR("Failed to open trace dump file: " << filename);
        }
    }
    // =========================================================================

    some_object obj;
    TransactionCompleteCB * rdata_cb;
    TransactionCompleteCB * write_cb;
    TransactionCompleteCB * read_cb;
    TransactionCompleteCB * cmd_cb;
    rdata_cb = new LPDDRSim::Callback<some_object, bool, unsigned, uint64_t,
            double, double, double>(&obj,&some_object::read_data);
    write_cb = new LPDDRSim::Callback<some_object, bool, unsigned, uint64_t,
            double, double, double>(&obj,&some_object::write_response);
    read_cb = new LPDDRSim::Callback<some_object, bool, unsigned, uint64_t,
            double, double, double>(&obj,&some_object::read_response);
    cmd_cb = new LPDDRSim::Callback<some_object, bool, unsigned, uint64_t,
            double, double, double>(&obj,&some_object::cmd_response);

    mem = new LPMemorySystemTop(0, PARAM_PATH, LOG_PATH, argc, argv);
    mem->RegisterCallbacks(rdata_cb, write_cb, read_cb, cmd_cb);
    parameter_check();
    CommandQueue.resize(NUM_CHANS);
    OutstandingQueue.resize(NUM_CHANS);
    SentReadTasks.resize(NUM_CHANS);
    SentWriteTasks.resize(NUM_CHANS);
    CompletedReadTasks.resize(NUM_CHANS);
    CompletedWriteTasks.resize(NUM_CHANS);

    if (TRACE_EN || DOU_TRACE_EN) {
        DEBUG("Read from trace file...");
        string filename = TRACE;
        file.open(filename.c_str());
        if (!file.is_open()) {
            ERROR("Error open log file! path="<<filename);
            assert(0);
        }
        get_line();
        DEBUG("Read trace file over!");
    } else {
        printf("DDRSim_test main()\n");
        DEBUG("Write ratio is ["<<WR_RATIO<<"%]. Now start the simulation...");
    }

    if (SAME_BA_RAND_ROW_MODE) {
        MATRIX_CH = 0x0;
        MATRIX_RA0 = 0x0;
        MATRIX_BA4 = 0x0;
        MATRIX_BA3 = 0x0;
        MATRIX_BA2 = 0x0;
        MATRIX_BA1 = 0x0;
        MATRIX_BA0 = 0x0;
        MATRIX_BG4 = 0x0;
        MATRIX_BG3 = 0x0;
        MATRIX_BG2 = 0x0;
        MATRIX_BG1 = 0x0;
        MATRIX_BG0 = 0x0;
        if (IS_GD2) {
            MATRIX_ROW1 = 0x0;
            MATRIX_ROW0 = 0x0;
        }
    }

    while (1) {
        if (STOP_WITH_STATETIME && cnt >= STOP_WINDOW * STATE_TIME) {
            if (STATE_TIME == 0) {
                ERROR("STATE_TIME=0 is not allowed with STOP mode.");
                assert(0);
            }
            if (STOP_WINDOW == 0) {
                ERROR("STOP_WINDOW=0 is not allowed with STOP mode.");
                assert(0);
            }
            float efficiency = calc_effi();
            DEBUG("Done, time: "<<cnt<<", efficiency: "<<fixed<<setprecision(2)<<efficiency<<"%");
            print_pass();
            delete mem;
            if (dump_trace_file.is_open()) dump_trace_file.close(); 
            exit(0);
        }
        if (TRACE_EN || DOU_TRACE_EN) {
            unsigned fw_que_size = FastWakeupQueue.size();
            for (size_t i = 0; i < fw_que_size; i ++) {
                if (FastWakeupQueue[i].size() == 0) continue;
                while (!FastWakeupQueue[i].empty() && cnt >= FastWakeupQueue[i][0].send_time) {
                    bool rank0 = FastWakeupQueue[i][0].rank == 0 ? true : false;
                    bool rank1 = !rank0;
                    mem->noc_read_inform(0, rank0, rank1, false);
                    FastWakeupQueue[i].erase(FastWakeupQueue[i].begin());
                }
                break;
            }
            if (trace_send_cnt >= task_cnt) {
                float efficiency = 0;
                efficiency = calc_effi();
                DEBUG("Done, time: "<<cnt<<", total command cnt: "<<task_cnt<<", efficiency: "<<fixed<<setprecision(2)<<efficiency<<"%");
                DEBUG("Power Consumption: "<<fixed<<mem->channels[0]->memoryController->calc_power());
                delete mem;
                print_pass();
                if (dump_trace_file.is_open()) dump_trace_file.close(); 
                exit(0);
            }
        } else {
            if (LATENCY_MODE) rand_command(mem, true);
            if (PRINT_IDLE_LAT && cnt >= 1000) exit(0);
            // 🔥 使用 logical_task_cnt 来控制 MATCH_TASK_LIMIT 限制
            if (!MATCH_MODE || logical_task_cnt < match_task_limit) {
                if (!PRINT_IDLE_LAT || (cnt % 1000 == 0)) rand_command(mem, false);
            }
        }
        send_command(mem);
        send_wdata(mem);
        if (MATCH_MODE && logical_task_cnt >= match_task_limit && trace_send_cnt >= task_cnt && all_command_queues_empty() && write_task.empty() && data_cnt == 0) {
            // flush_write_merge_buffers();
        }
        mem->update();
        if (MATCH_MODE) {
            uint64_t expected_read_data_resp = read_cmd_send_cnt * (DATA_SIZE / (DMC_DATA_BUS_BITS / 8));
            // 🔥 排空检查也使用 logical_task_cnt
            bool sent_enough = logical_task_cnt >= match_task_limit && (trace_send_cnt >= task_cnt || (all_command_queues_empty() && !memory_accepts_transaction()))
                    && all_command_queues_empty() && write_task.empty() && data_cnt == 0;
            bool all_sent_tasks_completed = true;
            for (size_t i = 0; i < SentReadTasks.size(); i++) {
                all_sent_tasks_completed &= SentReadTasks[i].size() == CompletedReadTasks[i].size();
                all_sent_tasks_completed &= SentWriteTasks[i].size() == CompletedWriteTasks[i].size();
            }
            if (sent_enough
                    && outstanding_empty() && memory_pending_empty() && read_data_resp_cnt == expected_read_data_resp
                    && write_resp_cnt == write_cmd_send_cnt && all_sent_tasks_completed) {
                float efficiency = 0;
                efficiency = calc_effi();
                DEBUG("Done, time: "<<cnt<<", efficiency: "<<fixed<<setprecision(2)<<efficiency<<"%");
                DEBUG("MATCH_MODE drain checked: send R/W="<<read_cmd_send_cnt<<"/"<<write_cmd_send_cnt
                        <<", resp read_data/write="<<read_data_resp_cnt<<"/"<<write_resp_cnt);
                DEBUG("Power Consumption: "<<fixed<<mem->channels[0]->memoryController->calc_power());
                delete mem;
                print_pass();
                if (dump_trace_file.is_open()) dump_trace_file.close(); 
                exit(0);
            }
        }
        cnt ++;
    }
    return 0;
}
