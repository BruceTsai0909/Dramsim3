// this is cpu.cc
#include "cpu.h"
#include <iostream>
#include <cmath>
#include <unordered_map>

namespace dramsim3 {

void RandomCPU::ClockTick() {
    memory_system_.ClockTick();
    if (get_next_) {
        last_addr_ = gen();
        last_write_ = (gen() % 3 == 0);
    }
    get_next_ = memory_system_.WillAcceptTransaction(last_addr_, last_write_);
    if (get_next_) {
        memory_system_.AddTransaction(last_addr_, last_write_);
    }
    clk_++;
}

void StreamCPU::ClockTick() {
    memory_system_.ClockTick();
    if (offset_ >= array_size_ || clk_ == 0) {
        addr_a_ = gen();
        addr_b_ = gen();
        addr_c_ = gen();
        offset_ = 0;
    }

    if (!inserted_a_ && memory_system_.WillAcceptTransaction(addr_a_ + offset_, false)) {
        memory_system_.AddTransaction(addr_a_ + offset_, false);
        inserted_a_ = true;
    }
    if (!inserted_b_ && memory_system_.WillAcceptTransaction(addr_b_ + offset_, false)) {
        memory_system_.AddTransaction(addr_b_ + offset_, false);
        inserted_b_ = true;
    }
    if (!inserted_c_ && memory_system_.WillAcceptTransaction(addr_c_ + offset_, true)) {
        memory_system_.AddTransaction(addr_c_ + offset_, true);
        inserted_c_ = true;
    }
    if (inserted_a_ && inserted_b_ && inserted_c_) {
        offset_ += stride_;
        inserted_a_ = false;
        inserted_b_ = false;
        inserted_c_ = false;
    }
    clk_++;
}

TraceBasedCPU::TraceBasedCPU(const std::string& config_file, const std::string& output_dir, const std::string& trace_file)
    : CPU(config_file, output_dir), trace_file_(trace_file) {
    trace_file_.open(trace_file);
    if (trace_file_.fail()) {
        std::cerr << "Trace file does not exist" << std::endl;
        AbruptExit(__FILE__, __LINE__);
    }
}

void TraceBasedCPU::ClockTick() {
    memory_system_.ClockTick();
    if (!trace_file_.eof()) {
        if (get_next_) {
            get_next_ = false;
            trace_file_ >> trans_;
        }
        if (trans_.added_cycle <= clk_) {
            get_next_ = memory_system_.WillAcceptTransaction(trans_.addr, trans_.is_write);
            if (get_next_) {
                memory_system_.AddTransaction(trans_.addr, trans_.is_write);
            }
        }
    }
    clk_++;
}

NMP_Core::NMP_Core(const std::string& config_file, const std::string& output_dir,
                   uint64_t inputBase1, uint64_t inputBase2, uint64_t outputBase,
                   uint64_t nodeDim, uint64_t count, int addition_op_cycle)
    : CPU(config_file, output_dir), inputBase1_(inputBase1), inputBase2_(inputBase2),
      outputBase_(outputBase), nodeDim_(nodeDim), count_(count), tid_(0),
      start_cycle_(0), end_cycle_(0), addition_op_cycle_(addition_op_cycle) {}

void NMP_Core::ClockTick() {


    // put done transactions into input_SRAM
    std::pair<uint64_t, int> done_trans = memory_system_.ReturnDoneTrans(clk_);
    if (done_trans.first != static_cast<uint64_t>(-1)) {
        if (done_trans.second == 0) { // If the transaction is a read
            input_sram.push(done_trans);
        } else if (done_trans.second == 1) { // If the transaction is a write
            Embedding_sum_operation++;
        }
    }

    // nmp core addition operation
    std::cout << "RW_queue_.size(): " << RW_queue_.size() << std::endl;
    
    if (RW_queue_.size() < 1) {
        for (uint64_t i = 0; i < count_; ++i) {
            uint64_t addressA = inputBase1_ + i * nodeDim_ + tid_;
            uint64_t addressB = inputBase2_ + i * nodeDim_ + tid_;

            uint64_t A = Read64B(addressA);
            uint64_t B = Read64B(addressB);

            // uint64_t C = ElementWiseOperation(A, B);
            // Write64B(outputBase_ + i * nodeDim_ + tid_, C);
        }
        
    }

    PrintQueue(RW_queue_);
    ProcessQueue(RW_queue_);
    /////////////
    PrintInputSramQueue(input_sram);
    /////////////
    ProcessInputSram();
    PrintOutputSramQueue(output_sram);

    MoveOutputSramToRWQueue();



    // memory clock tick!
    memory_system_.ClockTick();

    // update clock count
    std::cout << "Number of DRAM cycles: " << clk_ + 1 << " cycles" << std::endl;
    std::cout << "Embedding_sum_operation: " << Embedding_sum_operation << std::endl;
    //std::cout << "addition operation times : " << addition_op_cycle_  << " cycles" << std::endl;
    
    tid_++;
    clk_++;
}

void NMP_Core::ProcessQueue(std::queue<std::pair<uint64_t, bool>>& transaction_queue) {
    //PrintTransactionQueue(transaction_queue);
    while (!transaction_queue.empty()) {
        const auto& transaction = transaction_queue.front();
        uint64_t address = transaction.first;
        bool ReadOrWrite = transaction.second;
        if (memory_system_.WillAcceptTransaction(address, ReadOrWrite)) {
            memory_system_.AddTransaction(address, ReadOrWrite);
            transaction_queue.pop();
        } else {
            std::cout << "MEMORY CAN NOT TAKE MORE TRASACTION" << std::endl;
            break; // can not take more transaction
        }
    }
}

uint64_t NMP_Core::Read64B(uint64_t address) {
    RW_queue_.emplace(address, false);
    return 0;  
}

void NMP_Core::Write64B(uint64_t address, uint64_t data) {
    RW_queue_.emplace(address, true);
}

uint64_t NMP_Core::ElementWiseOperation(uint64_t A, uint64_t B) {
    addition_op_cycle_++;
    return A + B;  
}

void NMP_Core::PrintQueue(const std::queue<std::pair<uint64_t, bool>>& q) const {
    std::queue<std::pair<uint64_t, bool>> temp = q;
    std::cout << "RW_queue_: ";
    while (!temp.empty()) {
        const auto& transaction = temp.front();
        std::cout << "[" << transaction.first << ", " << (transaction.second ? "Write" : "Read") << "] ";
        temp.pop();
    }
    std::cout << std::endl;
}

void NMP_Core::PrintTransactionQueue(const std::queue<std::pair<uint64_t, bool>>& transaction_queue) const {
    std::queue<std::pair<uint64_t, bool>> temp_queue = transaction_queue; 
    std::cout << "Transaction Queue Contents:" << std::endl;
    while (!temp_queue.empty()) {
        const auto& transaction = temp_queue.front();
        uint64_t address = transaction.first;
        bool ReadOrWrite = transaction.second;
        std::cout << "Address: " << address << ", Type: " << (ReadOrWrite ? "WRITE" : "READ") << std::endl;
        temp_queue.pop();
    }
}

void NMP_Core::PrintInputSramQueue(const std::queue<std::pair<uint64_t, bool>>& q) {
    std::queue<std::pair<uint64_t, bool>> copy = q;
    std::cout << "InputSRAM: [";
    while (!copy.empty()) {
        std::cout << "(" << copy.front().first << ", " << (copy.front().second ? "Write" : "Read") << ")";
        copy.pop();
        if (!copy.empty()) {
            std::cout << ", ";
        }
    }
    std::cout << "]" << std::endl;
}

void NMP_Core::PrintOutputSramQueue(const std::queue<std::pair<uint64_t, bool>>& q) {
    std::queue<std::pair<uint64_t, bool>> copy = q;
    std::cout << "OutputSRAM: [";
    while (!copy.empty()) {
        std::cout << "(" << copy.front().first << ", " << (copy.front().second ? "Write" : "Read") << ")";
        copy.pop();
        if (!copy.empty()) {
            std::cout << ", ";
        }
    }
    std::cout << "]" << std::endl;
}


void NMP_Core::ProcessInputSram() {
    if (input_sram.size() >= 2) {
        // delete two datas
        auto data1 = input_sram.front();
        input_sram.pop();
        auto data2 = input_sram.front();
        input_sram.pop();

        // add write request data on output sram
        uint64_t write_addr = outputBase_ + tid_; // address
        output_sram.emplace(write_addr, true);

        std::cout << "Processed Input SRAM: (" << data1.first << ", " << (data1.second ? "Write" : "Read")
                  << "), (" << data2.first << ", " << (data2.second ? "Write" : "Read") << ") -> Output SRAM: ("
                  << write_addr << ", Write)" << std::endl;
    }
}

void NMP_Core::MoveOutputSramToRWQueue() {
    if (!output_sram.empty()) {
        auto transaction = output_sram.front();
        RW_queue_.push(transaction);
        output_sram.pop();
        std::cout << "Moved transaction to RW_queue_: [" << transaction.first << ", " << (transaction.second ? "Write" : "Read") << "]" << std::endl;
    }
}

// std::cout << "1st Read Address: " << (inputBase1_ + i * nodeDim_ + tid_) << std::endl;
// std::cout << "2nd Read Address: " << (inputBase2_ + i * nodeDim_ + tid_) << std::endl;
// std::cout << "---Write Address: " << (outputBase_ + i * nodeDim_ + tid_) << std::endl;
// std::cout << "MEMORY CAN NOT TAKE MORE TRASACTION" << std::endl;
// std::cout << "poped" << std::endl;

}  // namespace dramsim3