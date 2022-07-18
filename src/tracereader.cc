#include "tracereader.h"

#include <cassert>
#include <cstdio>
#include <iostream>
#include <string>
#include <fstream>
#include <zlib.h>
#include <memory>

extern "C" {
#include <xed/xed-interface.h>
}

#define GZ_BUFFER_SIZE 80

using std::cout;
using std::endl;
using std::cerr;

static bool xedInitDone = false;
//static size_t line_count = 0;

tracereader::tracereader(uint8_t cpu, std::string _ts) : cpu(cpu), trace_string(_ts) {
    std::string last_dot = trace_string.substr(trace_string.find_last_of("."));

    if (trace_string.substr(0, 4) == "http") {
        // Check file exists
        char testfile_command[4096];
        sprintf(testfile_command, "wget -q --spider %s", trace_string.c_str());
        FILE *testfile = popen(testfile_command, "r");
        if (pclose(testfile)) {
            std::cerr << "TRACE FILE NOT FOUND" << std::endl;
            assert(0);
        }
        cmd_fmtstr = "wget -qO- -o /dev/null %2$s | %1$s -dc";
    } else {
        std::ifstream testfile(trace_string);
        if (!testfile.good()) {
            std::cerr << "TRACE FILE NOT FOUND" << std::endl;
            assert(0);
        }
        cmd_fmtstr = "%1$s -dc %2$s";
    }

    if (last_dot[1] == 'g') // gzip format
        decomp_program = "gzip";
    else if (last_dot[1] == 'x') // xz
        decomp_program = "xz";
    else {
        std::cout << "ChampSim does not support traces other than gz or xz compression!" << std::endl;
        assert(0);
    }

    open(trace_string);
}

tracereader::~tracereader() {
    close();
}

template<typename T>
ooo_model_instr tracereader::read_single_instr() {
    T trace_read_instr;

    while (!fread(&trace_read_instr, sizeof(T), 1, trace_file)) {
        // reached end of file for this trace
        std::cout << "*** Reached end of trace: " << trace_string << std::endl;

        // close the trace file and re-open it
        close();
        open(trace_string);
    }

    // copy the instruction into the performance model's instruction format
    ooo_model_instr retval(cpu, trace_read_instr);
    return retval;
}

void tracereader::open(std::string trace_string) {
    char gunzip_command[4096];
    sprintf(gunzip_command, cmd_fmtstr.c_str(), decomp_program.c_str(), trace_string.c_str());
    trace_file = popen(gunzip_command, "r");
    if (trace_file == NULL) {
        std::cerr << std::endl << "*** CANNOT OPEN TRACE FILE: " << trace_string << " ***" << std::endl;
        assert(0);
    }
}

void tracereader::close() {
    if (trace_file != NULL) {
        pclose(trace_file);
    }
}

class cloudsuite_tracereader : public tracereader {
    ooo_model_instr last_instr;
    bool initialized = false;

public:
    cloudsuite_tracereader(uint8_t cpu, std::string _tn) : tracereader(cpu, _tn) {}

    ooo_model_instr get() {
        ooo_model_instr trace_read_instr = read_single_instr<cloudsuite_instr>();

        if (!initialized) {
            last_instr = trace_read_instr;
            initialized = true;
        }

        last_instr.branch_target = trace_read_instr.ip;
        ooo_model_instr retval = last_instr;

        last_instr = trace_read_instr;
        return retval;
    }
};

class input_tracereader : public tracereader {
    ooo_model_instr last_instr;
    bool initialized = false;

public:
    input_tracereader(uint8_t cpu, std::string _tn) : tracereader(cpu, _tn) {}

    ooo_model_instr get() {
        ooo_model_instr trace_read_instr = read_single_instr<input_instr>();

        if (!initialized) {
            last_instr = trace_read_instr;
            initialized = true;
        }

        last_instr.branch_target = trace_read_instr.ip;
        ooo_model_instr retval = last_instr;

        last_instr = trace_read_instr;
        return retval;
    }
};

class pt_tracereader : public tracereader {
    ooo_model_instr last_instr;
    bool initialized = false;
    gzFile trace_file_pt;
public:
    explicit pt_tracereader(uint8_t cpu, std::string _tn) : tracereader() {
        this->cpu = cpu;
        this->trace_string = _tn;
        trace_file_pt = gzopen(trace_string.c_str(), "rb");
        if (trace_file_pt == NULL) {
            std::cerr << std::endl << "*** CANNOT REOPEN TRACE FILE: " << trace_string << " ***" << std::endl;
            assert(0);
        }
        if (!xedInitDone) {
            xed_tables_init();
            xedInitDone = true;
        }
    }

    ~pt_tracereader() {
        gzclose(trace_file_pt);
    }

    std::unique_ptr<xed_decoded_inst_t> makeNop(uint8_t _length) {
        // A 10-to-15-byte NOP instruction (direct XED support is only up to 9)
        static const char *nop15 =
                "\x66\x66\x66\x66\x66\x66\x2e\x0f\x1f\x84\x00\x00\x00\x00\x00";

        auto ptr = std::make_unique<xed_decoded_inst_t>();
        xed_decoded_inst_t *ins = ptr.get();
//        xed_decoded_inst_zero_set_mode(ins, &xed_state_);
        xed_decoded_inst_zero(ins);
        xed_decoded_inst_set_mode(ins, XED_MACHINE_MODE_LONG_64, XED_ADDRESS_WIDTH_64b);
        xed_error_enum_t res;

        // The reported instruction length must be 1-15 bytes
        _length &= 0xf;
        assert(_length > 0);
        if (_length > 9) {
            int offset = 15 - _length;
            const uint8_t *pos = reinterpret_cast<const uint8_t *>(nop15 + offset);
            res = xed_decode(ins, pos, 15 - offset);
        } else {
            uint8_t buf[10];
            res = xed_encode_nop(&buf[0], _length);
            if (res != XED_ERROR_NONE) {
                cerr << "XED NOP encode error: " << xed_error_enum_t2str(res);
            }
            res = xed_decode(ins, buf, sizeof(buf));
        }
        if (res != XED_ERROR_NONE) {
            cerr << "XED NOP encode error: " << xed_error_enum_t2str(res);
        }
        return ptr;
    }


    ooo_model_instr get() {
        char buffer[GZ_BUFFER_SIZE];
        pt_instr trace_read_instr_pt;
        do {
            while (gzgets(trace_file_pt, buffer, GZ_BUFFER_SIZE) == Z_NULL) {
                // reached end of file for this trace
                std::cout << "*** Reached end of trace: " << trace_string << std::endl;

                // close the trace file and re-open it
                gzclose(trace_file_pt);
                trace_file_pt = gzopen(trace_string.c_str(), "rb");
//                line_count = 0;
                if (trace_file_pt == NULL) {
                    std::cerr << std::endl << "*** CANNOT REOPEN TRACE FILE: " << trace_string << " ***" << std::endl;
                    assert(0);
                }
            }
            trace_read_instr_pt = pt_instr(buffer);
        } while (trace_read_instr_pt.pc == 0);
//        pt_instr trace_read_instr_pt(buffer);
        ooo_model_instr arch_instr;
        int num_reg_ops = 0, num_mem_ops = 0;
        arch_instr.ip = trace_read_instr_pt.pc;
        arch_instr.size = trace_read_instr_pt.size;
//        arch_instr.branch_taken = current_pt_instr.pc + current_pt_instr.size != next_pt_instr.pc;
//        if (line_count == 0 || last_instr.ip == 0 || line_count == 1166) {
//            std::cout << "Line number: " << line_count << " ";
//            std::cout << arch_instr.ip << " ";
//            for (auto a : trace_read_instr_pt.inst_bytes) {
//                std::cout << (uint32_t) a << " ";
//            }
//            std::cout << std::endl;
//            std::cout << std::endl;
//        }
//        line_count++;
        xed_decoded_inst_zero(&arch_instr.inst_pt);
        xed_decoded_inst_set_mode(&arch_instr.inst_pt, XED_MACHINE_MODE_LONG_64, XED_ADDRESS_WIDTH_64b);
        xed_error_enum_t xed_error = xed_decode(&arch_instr.inst_pt, trace_read_instr_pt.inst_bytes.data(),
                                                trace_read_instr_pt.size);
//        assert(xed_error == XED_ERROR_NONE);
        if (xed_error != XED_ERROR_NONE) {
//                    printf("%d %s\n",(int)current_pt_instr.size, xed_error_enum_t2str(xed_error));
            // TODO: Not sure how to deal with this error.
            arch_instr.inst_pt = *makeNop(arch_instr.size);
//            cerr << "Decode error!" << endl;
//            if ((last_instr.is_branch == 1) && (last_instr.branch_taken == 1)) {
//                last_instr.branch_target = arch_instr.ip;
//            }
//            auto retval = last_instr;
//            last_instr = arch_instr;
//            return retval;
        }
//                arch_instr.is_branch = xed_decoded_inst_get_category(&arch_instr.inst_pt) == XED_CATEGORY_COND_BR;

        arch_instr.asid[0] = cpu;
        arch_instr.asid[1] = cpu;

        // Find registers. Reference: zsim trace_decoder.cpp
        uint32_t numOperands = xed_decoded_inst_noperands(&arch_instr.inst_pt);
        auto opcode = (xed_iclass_enum_t) xed_decoded_inst_get_iclass(&arch_instr.inst_pt);
        uint32_t numInRegs = 0, numOutRegs = 0;
//        uint32_t numLoads = 0, numStores = 0;
        for (uint32_t op = 0; op < numOperands; op++) {
            bool read = false, write = false;
            const xed_inst_t *inst = arch_instr.inst_pt._inst;
            const xed_operand_t *o = xed_inst_operand(inst, op);

            switch (xed_decoded_inst_operand_action(&arch_instr.inst_pt, op)) {
                case XED_OPERAND_ACTION_RW:
                    read = true;
                    write = true;
                    break;
                case XED_OPERAND_ACTION_R:
                    read = true;
                    break;
                case XED_OPERAND_ACTION_W:
                    write = true;
                    break;
                case XED_OPERAND_ACTION_CR:
                    read = true;
                    break;
                case XED_OPERAND_ACTION_RCW:
                    read = true;
                    write = true;
                    break;
                case XED_OPERAND_ACTION_CRW:
                    read = true;
                    write = true;
                    break;
                case XED_OPERAND_ACTION_CW:
                    write = true;
                    break;
                default:
                    assert(0);
            }
            assert(read || write);

            if (xed_operand_is_register(xed_operand_name(o))) {
                /* Handle XED-PIN mismatch
                * PIN provides only one output register for near call instrumentations
                * and zsim can only handle one. XED lists two (which might be correct)
                * but it won't affect accuracy much. */
                if ((opcode == XED_ICLASS_CALL_NEAR) && numOutRegs > 0)
                    continue;
//                        TODO: Justify that the change is correct
//                        auto reg = xed_decoded_inst_get_reg(arch_instr.inst_pt.get(), (xed_operand_enum_t)op);
                auto reg = xed_decoded_inst_get_reg(&arch_instr.inst_pt, xed_operand_name(
                        xed_inst_operand(xed_decoded_inst_inst(&arch_instr.inst_pt), op)));
                assert(reg);  // can't be invalid
                reg = xed_get_largest_enclosing_register(reg);  // eax -> rax, etc; o/w we'd miss a bunch of deps!

//                        assert(numInRegs < 2);
//                        assert(numOutRegs < 2);
                if (read) {
                    arch_instr.source_registers[numInRegs++] = reg;
                }
                if (write) {
                    arch_instr.destination_registers[numOutRegs++] = reg;
                }
//                        TODO: does numInRegs + numOutRegs == num_reg_ops?
//                        num_reg_ops++;
            }
//            else if (xed_operand_name(o) == XED_OPERAND_MEM0) {
////                        if (write) storeOps[numStores++] = 0;
////                        if (read) loadOps[numLoads++] = 0;
//                if (write) numStores++;
//                if (read) numLoads++;
////                        TODO: does numStores + numLoads == num_mem_ops?
////                        num_mem_ops++;
//            }
//            else if (xed_operand_name(o) == XED_OPERAND_MEM1) {
////                        if (write) storeOps[numStores++] = 1;
////                        if (read) loadOps[numLoads++] = 1;
//                if (write) numStores++;
//                if (read) numLoads++;
////                        TODO: does numStores + numLoads == num_mem_ops?
////                        num_mem_ops++;
//            }
        }
//        arch_instr.num_reg_ops = (int)(numInRegs + numOutRegs);
//        arch_instr.num_mem_ops = (int)(numStores + numLoads);
//        if (num_mem_ops > 0)
//            arch_instr.is_memory = 1;

        // TODO: Another way to determine memory access. Compare the results
        uint32_t loads = 0, stores = 0;
        uint32_t n_used_mem_ops = 0;  // 'lea' doesn't actually touch memory
        uint32_t n_mem_ops = xed_decoded_inst_number_of_memory_operands(&arch_instr.inst_pt);
        if (n_mem_ops > 0) {
            // NOPs are special and don't actually cause memory accesses
            xed_category_enum_t category = xed_decoded_inst_get_category(&arch_instr.inst_pt);
            if (category != XED_CATEGORY_NOP && category != XED_CATEGORY_WIDENOP) {
                for (uint32_t i = 0; i < n_mem_ops; i++) {
                    if (xed_decoded_inst_mem_read(&arch_instr.inst_pt, i)) {
                        n_used_mem_ops++;
                        loads++;
                    }
                    if (xed_decoded_inst_mem_written(&arch_instr.inst_pt, i)) {
                        n_used_mem_ops++;
                        stores++;
                    }
                }
            }
        }
        arch_instr.num_reg_ops = (int) (numInRegs + numOutRegs);
        arch_instr.num_mem_ops = (int) (stores + loads);
        if (num_mem_ops > 0)
            arch_instr.is_memory = 1;

        // determine what kind of branch this is, if any
        arch_instr.is_branch = 1;
        auto category = xed_decoded_inst_get_category(&arch_instr.inst_pt);
        switch (category) {
            case XED_CATEGORY_COND_BR:
                arch_instr.branch_type = BRANCH_CONDITIONAL;
                break;
            case XED_CATEGORY_UNCOND_BR:
                if (xed3_operand_get_brdisp_width(&arch_instr.inst_pt))
                    arch_instr.branch_type = BRANCH_DIRECT_JUMP;
                else
                    arch_instr.branch_type = BRANCH_INDIRECT;
                break;
            case XED_CATEGORY_CALL:
                if (xed3_operand_get_brdisp_width(&arch_instr.inst_pt))
                    arch_instr.branch_type = BRANCH_DIRECT_CALL;
                else
                    arch_instr.branch_type = BRANCH_INDIRECT_CALL;
                break;
            case XED_CATEGORY_RET:
                arch_instr.branch_type = BRANCH_RETURN;
                break;
            default:
                arch_instr.branch_type = NOT_BRANCH;
                arch_instr.is_branch = NOT_BRANCH;
                break;
        }

        if (!initialized) {
            last_instr = arch_instr;
            initialized = true;
        }

//        total_branch_types[arch_instr.branch_type]++;
        last_instr.branch_taken = last_instr.ip + last_instr.size != arch_instr.ip;
        if ((last_instr.is_branch == 1) && (last_instr.branch_taken == 1)) {
            last_instr.branch_target = arch_instr.ip;
        }
        auto retval = last_instr;
        last_instr = arch_instr;
        return retval;
    }
};

tracereader *get_tracereader(std::string fname, uint8_t cpu, bool is_cloudsuite, bool is_pt) {
    if (is_cloudsuite) {
        return new cloudsuite_tracereader(cpu, fname);
    } else if (is_pt) {
        return new pt_tracereader(cpu, fname);
    } else {
        return new input_tracereader(cpu, fname);
    }
}

