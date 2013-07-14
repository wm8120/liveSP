/*
 * Copyright (c) 2012 ARM Limited
 * All rights reserved.
 *
 * The license below extends only to copyright in the software and shall
 * not be construed as granting a license to any other intellectual
 * property including but not limited to intellectual property relating
 * to a hardware implementation of the functionality of the software
 * licensed hereunder.  You may use the software subject to the license
 * terms below provided that you ensure that this notice is replicated
 * unmodified and in its entirety in all distributions of the software,
 * modified or unmodified, in source code or in binary form.
 *
 * Copyright (c) 2002-2005 The Regents of The University of Michigan
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Authors: Steve Reinhardt
 */

#include "arch/locked_mem.hh"
#include "arch/mmapped_ipr.hh"
#include "arch/utility.hh"
#include "base/bigint.hh"
#include "base/output.hh"
#include "config/the_isa.hh"
#include "cpu/simple/mycpu.hh"
#include "cpu/exetrace.hh"
#include "debug/Drain.hh"
#include "debug/ExecFaulting.hh"
#include "debug/SimpleCPU.hh"
#include "mem/packet.hh"
#include "mem/packet_access.hh"
#include "mem/physical.hh"
#include "params/MyCPU.hh"
#include "sim/faults.hh"
#include "sim/system.hh"
#include "sim/full_system.hh"
#include "sim/sim_events.hh"

using namespace std;
using namespace TheISA;

MyCPU::TickEvent::TickEvent(MyCPU *c)
    : Event(CPU_Tick_Pri), cpu(c)
{
}


void
MyCPU::TickEvent::process()
{
    cpu->tick();
}

const char *
MyCPU::TickEvent::description() const
{
    return "MyCPU tick";
}

void
MyCPU::init()
{
    BaseCPU::init();

    // Initialise the ThreadContext's memory proxies
    tcBase()->initMemProxies(tcBase());

    if (FullSystem && !params()->switched_out) {
        ThreadID size = threadContexts.size();
        for (ThreadID i = 0; i < size; ++i) {
            ThreadContext *tc = threadContexts[i];
            // initialize CPU, including PC
            TheISA::initCPU(tc, tc->contextId());
        }
    }

    // Atomic doesn't do MT right now, so contextId == threadId
    ifetch_req.setThreadContext(_cpuId, 0); // Add thread ID if we add MT
    data_read_req.setThreadContext(_cpuId, 0); // Add thread ID here too
    data_write_req.setThreadContext(_cpuId, 0); // Add thread ID here too
}

MyCPU::MyCPU(MyCPUParams *p)
    : BaseSimpleCPU(p), tickEvent(this), width(p->width), locked(false),
      simulate_data_stalls(p->simulate_data_stalls),
      simulate_inst_stalls(p->simulate_inst_stalls),
      drain_manager(NULL),
      icachePort(name() + ".icache_port", this),
      dcachePort(name() + ".dcache_port", this),
      fastmem(p->fastmem),
      synth(p->synthesize),
      svc_flag(false),
      bb_simple(true),
      inst_start_num(p->synthesize_start),
      inst_end_num(p->synthesize_start+p->synthesize_interval-1),
      interval_num(p->synthesize_interval),
      synthStream(NULL),
      bbFreqStream(NULL),
      svcStream(NULL),
      debugStream(NULL),
      dsyscall(p->syscall_dump),
      syscallStream(NULL),
      simpoint(p->simpoint_profile),
      intervalSize(p->simpoint_interval),
      intervalCount(0),
      intervalDrift(0),
      simpointStream(NULL),
      bbsequenceStream(NULL),
      currentBBV(0, 0),
      currentBBVInstCount(0)
{
    _status = Idle;

    if (simpoint) {
        simpointStream = simout.create(p->simpoint_profile_file, false);
        bbsequenceStream = simout.create(p->bb_profile_file, false);
    }

    if (synth) {
        synthStream = simout.create(p->synthesize_file, false);
        bbFreqStream = simout.create(p->bb_freq_file, false);
        svcStream = simout.create(p->svc_reg_file, false);
        debugStream = simout.create(p->debug_file, false);
    }

    if (dsyscall){
        syscallStream = simout.create(p->syscall_profile_file, false);
    }
}


MyCPU::~MyCPU()
{
    if (tickEvent.scheduled()) {
        deschedule(tickEvent);
    }
    if (simpointStream) {
        simout.close(simpointStream);
    }
    if (bbsequenceStream) {
        simout.close(bbsequenceStream);
    }
    if (synthStream) {
        simout.close(synthStream);
    }
    if (bbFreqStream) {
        simout.close(bbFreqStream);
    }
    if (svcStream) {
        simout.close(svcStream);
    }
    if (syscallStream) {
        simout.close(syscallStream);
    }
    if (debugStream){
        simout.close(debugStream);    
    }
}

unsigned int
MyCPU::drain(DrainManager *dm)
{
    assert(!drain_manager);
    if (switchedOut())
        return 0;

    if (!isDrained()) {
        DPRINTF(Drain, "Requesting drain: %s\n", pcState());
        drain_manager = dm;
        return 1;
    } else {
        if (tickEvent.scheduled())
            deschedule(tickEvent);

        DPRINTF(Drain, "Not executing microcode, no need to drain.\n");
        return 0;
    }
}

void
MyCPU::drainResume()
{
    assert(!tickEvent.scheduled());
    assert(!drain_manager);
    if (switchedOut())
        return;

    DPRINTF(SimpleCPU, "Resume\n");
    verifyMemoryMode();

    assert(!threadContexts.empty());
    if (threadContexts.size() > 1)
        fatal("The atomic CPU only supports one thread.\n");

    if (thread->status() == ThreadContext::Active) {
        schedule(tickEvent, nextCycle());
        _status = BaseSimpleCPU::Running;
    } else {
        _status = BaseSimpleCPU::Idle;
    }

    system->totalNumInsts = 0;
}

bool
MyCPU::tryCompleteDrain()
{
    if (!drain_manager)
        return false;

    DPRINTF(Drain, "tryCompleteDrain: %s\n", pcState());
    if (!isDrained())
        return false;

    DPRINTF(Drain, "CPU done draining, processing drain event\n");
    drain_manager->signalDrainDone();
    drain_manager = NULL;

    return true;
}


void
MyCPU::switchOut()
{
    BaseSimpleCPU::switchOut();

    assert(!tickEvent.scheduled());
    assert(_status == BaseSimpleCPU::Running || _status == Idle);
    assert(isDrained());
}


void
MyCPU::takeOverFrom(BaseCPU *oldCPU)
{
    BaseSimpleCPU::takeOverFrom(oldCPU);

    // The tick event should have been descheduled by drain()
    assert(!tickEvent.scheduled());

    ifetch_req.setThreadContext(_cpuId, 0); // Add thread ID if we add MT
    data_read_req.setThreadContext(_cpuId, 0); // Add thread ID here too
    data_write_req.setThreadContext(_cpuId, 0); // Add thread ID here too
}

void
MyCPU::verifyMemoryMode() const
{
    if (!system->isAtomicMode()) {
        fatal("The atomic CPU requires the memory system to be in "
              "'atomic' mode.\n");
    }
}

void
MyCPU::activateContext(ThreadID thread_num, Cycles delay)
{
    DPRINTF(SimpleCPU, "ActivateContext %d (%d cycles)\n", thread_num, delay);

    assert(thread_num == 0);
    assert(thread);

    assert(_status == Idle);
    assert(!tickEvent.scheduled());

    notIdleFraction++;
    numCycles += ticksToCycles(thread->lastActivate - thread->lastSuspend);

    //Make sure ticks are still on multiples of cycles
    schedule(tickEvent, clockEdge(delay));
    _status = BaseSimpleCPU::Running;
}


void
MyCPU::suspendContext(ThreadID thread_num)
{
    DPRINTF(SimpleCPU, "SuspendContext %d\n", thread_num);

    assert(thread_num == 0);
    assert(thread);

    if (_status == Idle)
        return;

    assert(_status == BaseSimpleCPU::Running);

    // tick event may not be scheduled if this gets called from inside
    // an instruction's execution, e.g. "quiesce"
    if (tickEvent.scheduled())
        deschedule(tickEvent);

    notIdleFraction--;
    _status = Idle;
}


Fault
MyCPU::readMem(Addr addr, uint8_t * data,
                         unsigned size, unsigned flags)
{
    // use the CPU's statically allocated read request and packet objects
    Request *req = &data_read_req;

    if (traceData) {
        traceData->setAddr(addr);
    }

    //The block size of our peer.
    unsigned blockSize = dcachePort.peerBlockSize();
    //The size of the data we're trying to read.
    int fullSize = size;

    //The address of the second part of this access if it needs to be split
    //across a cache line boundary.
    Addr secondAddr = roundDown(addr + size - 1, blockSize);

    if (secondAddr > addr)
        size = secondAddr - addr;

    dcache_latency = 0;

    while (1) {
        req->setVirt(0, addr, size, flags, dataMasterId(), thread->pcState().instAddr());

        // translate to physical address
        Fault fault = thread->dtb->translateAtomic(req, tc, BaseTLB::Read);

        // Now do the access.
        if (fault == NoFault && !req->getFlags().isSet(Request::NO_ACCESS)) {
            Packet pkt = Packet(req,
                                req->isLLSC() ? MemCmd::LoadLockedReq :
                                MemCmd::ReadReq);
            pkt.dataStatic(data);

            if (req->isMmappedIpr())
                dcache_latency += TheISA::handleIprRead(thread->getTC(), &pkt);
            else {
                if (fastmem && system->isMemAddr(pkt.getAddr()))
                    system->getPhysMem().access(&pkt);
                else
                    dcache_latency += dcachePort.sendAtomic(&pkt);
            }
            dcache_access = true;

            assert(!pkt.isError());

            if (req->isLLSC()) {
                TheISA::handleLockedRead(thread, req);
            }
        }

        //If there's a fault, return it
        if (fault != NoFault) {
            if (req->isPrefetch()) {
                return NoFault;
            } else {
                return fault;
            }
        }

        //If we don't need to access a second cache line, stop now.
        if (secondAddr <= addr)
        {
            if (req->isLocked() && fault == NoFault) {
                assert(!locked);
                locked = true;
            }
            return fault;
        }

        /*
         * Set up for accessing the second cache line.
         */

        //Move the pointer we're reading into to the correct location.
        data += size;
        //Adjust the size to get the remaining bytes.
        size = addr + fullSize - secondAddr;
        //And access the right address.
        addr = secondAddr;
    }
}


Fault
MyCPU::writeMem(uint8_t *data, unsigned size,
                          Addr addr, unsigned flags, uint64_t *res)
{
    // use the CPU's statically allocated write request and packet objects
    Request *req = &data_write_req;

    if (traceData) {
        traceData->setAddr(addr);
    }

    //The block size of our peer.
    unsigned blockSize = dcachePort.peerBlockSize();
    //The size of the data we're trying to read.
    int fullSize = size;

    //The address of the second part of this access if it needs to be split
    //across a cache line boundary.
    Addr secondAddr = roundDown(addr + size - 1, blockSize);

    if(secondAddr > addr)
        size = secondAddr - addr;

    dcache_latency = 0;

    while(1) {
        req->setVirt(0, addr, size, flags, dataMasterId(), thread->pcState().instAddr());

        // translate to physical address
        Fault fault = thread->dtb->translateAtomic(req, tc, BaseTLB::Write);

        // Now do the access.
        if (fault == NoFault) {
            MemCmd cmd = MemCmd::WriteReq; // default
            bool do_access = true;  // flag to suppress cache access

            if (req->isLLSC()) {
                cmd = MemCmd::StoreCondReq;
                do_access = TheISA::handleLockedWrite(thread, req);
            } else if (req->isSwap()) {
                cmd = MemCmd::SwapReq;
                if (req->isCondSwap()) {
                    assert(res);
                    req->setExtraData(*res);
                }
            }

            if (do_access && !req->getFlags().isSet(Request::NO_ACCESS)) {
                Packet pkt = Packet(req, cmd);
                pkt.dataStatic(data);

                if (req->isMmappedIpr()) {
                    dcache_latency +=
                        TheISA::handleIprWrite(thread->getTC(), &pkt);
                } else {
                    if (fastmem && system->isMemAddr(pkt.getAddr()))
                        system->getPhysMem().access(&pkt);
                    else
                        dcache_latency += dcachePort.sendAtomic(&pkt);
                }
                dcache_access = true;
                assert(!pkt.isError());

                if (req->isSwap()) {
                    assert(res);
                    memcpy(res, pkt.getPtr<uint8_t>(), fullSize);
                }
            }

            if (res && !req->isSwap()) {
                *res = req->getExtraData();
            }
        }

        //If there's a fault or we don't need to access a second cache line,
        //stop now.
        if (fault != NoFault || secondAddr <= addr)
        {
            if (req->isLocked() && fault == NoFault) {
                assert(locked);
                locked = false;
            }
            if (fault != NoFault && req->isPrefetch()) {
                return NoFault;
            } else {
                return fault;
            }
        }

        /*
         * Set up for accessing the second cache line.
         */

        //Move the pointer we're reading into to the correct location.
        data += size;
        //Adjust the size to get the remaining bytes.
        size = addr + fullSize - secondAddr;
        //And access the right address.
        addr = secondAddr;
    }
}


void
MyCPU::tick()
{
    DPRINTF(SimpleCPU, "Tick\n");

    Tick latency = 0;

    for (int i = 0; i < width || locked; ++i) {
        numCycles++;

        if (!curStaticInst || !curStaticInst->isDelayedCommit())
            checkForInterrupts();

        checkPcEventQueue();
        // We must have just got suspended by a PC event
        if (_status == Idle) {
            tryCompleteDrain();
            return;
        }

        Fault fault = NoFault;

        TheISA::PCState pcState = thread->pcState();

        bool needToFetch = !isRomMicroPC(pcState.microPC()) &&
                           !curMacroStaticInst;
        if (needToFetch) {
            setupFetchRequest(&ifetch_req);
            fault = thread->itb->translateAtomic(&ifetch_req, tc,
                                                 BaseTLB::Execute);
        }

        if (fault == NoFault) {
            Tick icache_latency = 0;
            bool icache_access = false;
            dcache_access = false; // assume no dcache access

            if (needToFetch) {
                // This is commented out because the decoder would act like
                // a tiny cache otherwise. It wouldn't be flushed when needed
                // like the I cache. It should be flushed, and when that works
                // this code should be uncommented.
                //Fetch more instruction memory if necessary
                //if(decoder.needMoreBytes())
                //{
                    icache_access = true;
                    Packet ifetch_pkt = Packet(&ifetch_req, MemCmd::ReadReq);
                    ifetch_pkt.dataStatic(&inst);

                    if (fastmem && system->isMemAddr(ifetch_pkt.getAddr()))
                        system->getPhysMem().access(&ifetch_pkt);
                    else
                        icache_latency = icachePort.sendAtomic(&ifetch_pkt);

                    assert(!ifetch_pkt.isError());

                    // ifetch_req is initialized to read the instruction directly
                    // into the CPU object's inst field.
                //}
            }

            preExecute();

            if (curStaticInst) {

                if (svc_flag)
                {
                    for ( int i=0; i<4; i++)
                    {
                        *svcStream << hex << thread->readIntReg(i) << " " << endl;
                    }
                    *svcStream << hex << thread->instAddr() << endl;
                    svc_flag = false;
                }

                fault = curStaticInst->execute(this, traceData);

                // keep an instruction count
                if (fault == NoFault)
                    countInst();
                else if (traceData && !DTRACE(ExecFaulting)) {
                    delete traceData;
                    traceData = NULL;
                }

                if (dsyscall)
                {
                    Addr pc = thread->instAddr();
                    string inst = curStaticInst->disassemble(pc);
                    static string instpre;
                    static bool syscall_begin = false;
                    if (pc >= 0xffff0000)
                    {
                        if (syscall_begin == false)
                        {
                            syscall_begin = true;
                            //*syscallStream << endl;
                            *syscallStream << instpre << endl;
                        }
                        *syscallStream << "0x" << hex << pc << " : " << curStaticInst->disassemble(pc) << " : ";
                        if (traceData->getAddrValid())
                        {
                            Addr a = traceData->getAddr();
                            *syscallStream << "Addr=0x" << hex << a << " : "; 
                        }
                        if (traceData->getDataStatus() !=  0)
                        {
                            *syscallStream << "D=0x" << hex << traceData->getIntData();
                        }
                        *syscallStream << endl;
                    }
                    else
                    {
                        if (syscall_begin == true)
                        {
                            syscall_begin = false; 
                        }
                        else
                        {
                            stringstream ss;
                            ss << "0x" << hex << pc << " : " << inst;
                            instpre = ss.str();
                        }
                    }
                }

                if (synth)
                {
                    //arguments
                    static bool bb_start = false;
                    static uint64_t r57 =0;
                    //uint64_t stack_bottom = 0xbf000000;
                    //uint64_t stack_size = 0x00800000;
                    //uint64_t stack_top_limit = stack_bottom - stack_size;

                    Addr cur_pc = thread->instAddr();
                    string inst = curStaticInst->disassemble(cur_pc);
                    if ( numInst == inst_start_num-1 ) // skip first N instructions
                    {
                        if ( !curStaticInst->isControl())
                        {
                            inst_start_num++;
                            inst_end_num++;
                        }
                        else
                        {
                            // snap the status of all registers
                            short int i=0;
                            for (i=0; i<16; i++)
                            {
                                regs[i]=thread->readIntReg(i);
                            }
                            bb_start = true;
                            //last_bb_pc = 0;
                            //last_bb_exit_pc = 0;
                            //freq_last_bb = 0;
                            ///for (i=0; i<16; i++)
                            ///{
                            ///    cout<<hex<<regs[i]<<endl;
                            ///}
                        }
                    }
                    else if (numInst == inst_start_num && (curStaticInst->isControl() || cur_pc >= 0xffff0000))
                    {
                        // snap the status of all registers
                        short int i=0;
                        for (i=0; i<16; i++)
                        {
                            regs[i]=thread->readIntReg(i);
                        }
                        inst_start_num++;
                        inst_end_num++;
                    }
                    else if ( (numInst == inst_start_num && !curStaticInst->isControl() && cur_pc < 0xffff0000) || \
                            (numInst > inst_start_num) )
                            //(numInst > inst_start_num && numInst <= inst_end_num + interval_num/2) )
                    {
                        if (curStaticInst->opClass() == Enums::MemRead && traceData->getAddrValid() && traceData->getDataStatus())
                        {
                            //assert(traceData->getAddrValid());
                            Addr a = traceData->getAddr();
                            if (inst.find("ldrd") == string::npos)
                            {
                                int stride = traceData->getDataStatus();
                                uint64_t data_value = traceData->getIntData();
                                if (stride == 4)
                                {
                                    if (inst.find("h") != string::npos && data_value <=0xffff)
                                    {
                                        stride = 2;
                                    }
                                    else if (inst.find("b") != string::npos && data_value <=0xff)
                                    {
                                        stride = 1;
                                    }
                                }
                                prepareData(a, data_value, stride);
                                ///if (ignoreSet.find(a) == ignoreSet.end())
                                ///{
                                ///    stringstream ss;
                                ///    //if (inst.find("b") != string::npos)
                                ///    //{
                                ///    //    ss << "B";
                                ///    //    stride = 1;
                                ///    //}
                                ///    //else if (inst.find("h") != string::npos)
                                ///    //{
                                ///    //    ss << "H";
                                ///    //    stride = 2;
                                ///    //}
                                ///    //else
                                ///    //{
                                ///    //    ss << "W";
                                ///    //    stride = 4;
                                ///    //}
                                ///    int stride = traceData->getDataStatus();
                                ///    uint64_t data_value = traceData->getIntData();
                                ///    if (stride == 1)
                                ///    {
                                ///        ss << "B";
                                ///    }
                                ///    else if (stride == 2)
                                ///    {
                                ///        ss << "H";
                                ///    }
                                ///    else if (stride == 3)
                                ///    {
                                ///        ss << "D";
                                ///    }
                                ///    else if (stride == 4)
                                ///    {
                                ///        if (inst.find("h") != string::npos && data_value <=0xffff)
                                ///        {
                                ///            ss << "H";
                                ///            stride = 2;
                                ///        }
                                ///        else if (inst.find("b") != string::npos && data_value <=0xff)
                                ///        {
                                ///            ss << "B";
                                ///            stride = 1;
                                ///        }
                                ///        else
                                ///        {
                                ///            ss << "W";
                                ///        }
                                ///    }
                                ///    else if (stride == 8)
                                ///    {
                                ///        ss << "Q";
                                ///    }
                                ///    else
                                ///    {
                                ///        *debugStream << "unknown data stride" << endl;
                                ///    }
                                ///    ss << hex << traceData->getIntData();
                                ///    prepareTable[a] = ss.str();

                                ///    for (int i=0; i<stride; i++)
                                ///    {
                                ///        ignoreSet.insert(a+i);
                                ///    }
                                ///}
                            }
                            else
                            {
                                uint8_t idx0 = curStaticInst->destRegIdx(0);
                                uint8_t idx1 = curStaticInst->destRegIdx(1);
                                uint64_t value0 = thread->readIntReg(idx0);
                                uint64_t value1 = thread->readIntReg(idx1);
                                //if (ignoreSet.find(a) == ignoreSet.end())
                                //{
                                //    stringstream ss;
                                //    ss << "W" << hex << value0;
                                //    prepareTable[a] = ss.str();
                                //    for (int i=0; i<4; i++)
                                //    {
                                //        ignoreSet.insert(a+i);
                                //    }
                                //}

                                //a += 4;
                                //if (ignoreSet.find(a) == ignoreSet.end())
                                //{
                                //    stringstream ss;
                                //    ss << "W" << hex << value1;
                                //    prepareTable[a] = ss.str();
                                //    for (int i=0; i<4; i++)
                                //    {
                                //        ignoreSet.insert(a+i);
                                //    }
                                //}
                                prepareData(a, value0, 4);
                                prepareData(a+4, value1, 4);
                            }
                        }// read
                        else if (curStaticInst->opClass() == Enums::MemWrite && traceData->getAddrValid() && traceData->getDataStatus())
                        {
                            //assert(traceData->getAddrValid());
                            Addr a = traceData->getAddr();
                            if (ignoreSet.find(a) == ignoreSet.end())
                            {
                                stringstream ss;
                                ss << "B" << 0;
                                prepareTable[a] = ss.str();
                                ignoreSet.insert(a);
                            }
                            //int stride;
                            //if (inst.find("b") != string::npos)
                            //{
                            //    ss << "B";
                            //    stride = 1;
                            //}
                            //else if (inst.find("h") != string::npos)
                            //{
                            //    ss << "H";
                            //    stride = 2;
                            //}
                            //else
                            //{
                            //    ss << "W";
                            //    stride = 4;
                            //}
                            //ss << hex << traceData->getIntData();
                            //prepareTable[a] = ss.str();

                            //for (int i=0; i<stride; i++)
                            //{
                            //    ignoreSet.insert(a+i);
                            //}
                        }// write

                        if (cur_pc >= 0xffff0000)
                        {
                            //skip syscall emulation instruction
                            if (cur_pc == 0xffff0fe0)
                            {
                                if (r57 == 0)
                                {
                                    r57 = traceData->getIntData();
                                }
                                else
                                {
                                    assert(r57 == traceData->getIntData());
                                }
                            }
                        }// >= 0xffff0000
                        else
                        {
                            if (numInst == inst_start_num)
                            {
                                start_pc = cur_pc;
                                *debugStream << numInst << endl;
                                *debugStream << numOp << endl;
                                *debugStream << inst;
                            }

                            if (curStaticInst->disassemble(cur_pc).find("svc") != string::npos)
                            {
                                svc_flag = true;
                            }

                            if (bb_start)
                            {
                                bb_start_pc = cur_pc;
                                bb_start = false;
                                bb_simple = true;
                            }

                            if (curStaticInst->isControl())
                            {
                                if (bb_simple && inst.find("pc") != string::npos)
                                {
                                    bb_simple = false;
                                }

                                if (numInst <= inst_end_num)
                                {
                                    bb_start = true;

                                    auto freq_itr = freqTable.find(cur_pc);
                                    if ( freq_itr != freqTable.end() )
                                    {
                                        (freq_itr->second).bbStartSet.insert(bb_start_pc);
                                        freq_itr->second.freq++;
                                    }
                                    else
                                    {
                                        struct BBAttr bb;
                                        bb.freq=1;
                                        bb.bbStartSet.insert(bb_start_pc);
                                        bb.showup=false;
                                        if (bb_simple)
                                        {
                                            bb.simple = true;
                                        }
                                        else
                                        {
                                            bb.simple = false;
                                        }
                                        freqTable.insert(make_pair(cur_pc, bb));
                                    }
                                }
                                else
                                {
                                    Addr cur_pc = thread->instAddr();
                                    string inst = curStaticInst->disassemble(cur_pc);
                                    auto itr = freqTable.find(cur_pc);
                                    if ( itr != freqTable.end())
                                    {
                                        itr->second.showup = true;
                                    }
                                    else
                                    {
                                        if (bb_simple)
                                        {
                                            struct BBAttr bb;
                                            bb.freq=0;
                                            bb.bbStartSet.insert(bb_start_pc);
                                            bb.showup=true;
                                            bb.simple=true;
                                            freqTable.insert(make_pair(cur_pc, bb));
                                        }
                                    }
                                }
                            }
                            else
                            {
                                //if ( cur_pc == 0x1a1ac )
                                //{
                                //    *debugStream << inst << " bb_simple: " << bb_simple << endl;
                                //}
                                if ( bb_simple && (inst.find("eq") != string::npos || \
                                        inst.find("ne") != string::npos || \
                                        inst.find("cs") != string::npos || \
                                        inst.find("hs") != string::npos || \
                                        inst.find("cc") != string::npos || \
                                        inst.find("lo") != string::npos || \
                                        inst.find("mi") != string::npos || \
                                        inst.find("pl") != string::npos || \
                                        inst.find("vs") != string::npos || \
                                        inst.find("vc") != string::npos || \
                                        inst.find("hi") != string::npos || \
                                        inst.find("ls") != string::npos || \
                                        inst.find("ge") != string::npos || \
                                        inst.find("lt") != string::npos || \
                                        inst.find("gt") != string::npos || \
                                        inst.find("le") != string::npos || \
                                        inst.find("pc") != string::npos ))
                                {
                                    if ( cur_pc == 0x14294 )
                                    {
                                        *debugStream << "fall in" << endl;
                                    }
                                    bb_simple = false;
                                }
                            }

                            map<Addr, InstInfo>::iterator instmap_it;
                            instmap_it = instTable.find(cur_pc);
                            if (instmap_it == instTable.end())
                            {
                                insertInstTable(cur_pc);
                            }
                            else
                            {
                                string& inst = instmap_it->second.disassembly;
                                size_t pos = inst.find("WaitTarget");
                                if ( pos != string::npos)
                                {
                                    instTable.erase(instmap_it); 
                                    insertInstTable(cur_pc);
                                }
                            }
                        }// < 0xffff0000
                    }
                    else if (numInst > inst_end_num + interval_num/2)
                    {
                        //recover memory
                        *synthStream<<"memory:";
                        std::map<Addr, string> orderedTable(prepareTable.begin(), prepareTable.end());
                        prepareTable.clear();
                        for(auto it = orderedTable.begin(); it != orderedTable.end(); it++)
                        {
                            *synthStream<<" "<<hex<<it->first<<"="<<it->second;
                        }
                        *synthStream<<endl;

                        //recover register: r0-r14(aka. lr)
                        *synthStream<<"register:";
                        for(int i=0; i<=14; i++)
                        {
                            *synthStream<<" r"<<dec<<i<<"="<<hex<<regs[i];
                        }
                        *synthStream<<endl;

                        //output assembly instruction
                        *synthStream<<"start: "<<hex<<start_pc<<endl;
                        *synthStream<<"text:"<<endl;
                        for (auto it = instTable.begin(); it != instTable.end(); it++)
                        {
                            string inst = it->second.disassembly;
                            size_t pos;
                            if ((pos = inst.find("WaitTarget")) != string::npos)
                                inst.replace(pos, 10, "nop");
                            *synthStream<<hex<<it->first<<"="<<inst<<endl;
                        }

                        //emulate syscall
                        if (r57 != 0)
                        {
                            *synthStream<<hex<<0xffff0fe0<<"=mov32 r0, #0x" << hex << r57 << endl;
                            *synthStream<<hex<<0xffff0fe4<<"=mov pc, lr" << endl;
                        }

                        //bb frequency file
                        for (auto it = freqTable.begin(); it != freqTable.end(); it++)
                        {
                            BBAttr& bb = it->second;
                            unordered_set<Addr>& set = bb.bbStartSet;
                            if (set.size() == 1 && bb.simple && bb.showup && *(set.begin()) != it->first)
                            {
                                *bbFreqStream << hex << it->first << " " << hex << *(set.begin()) << " " << bb.freq << endl;
                            }
                        }

                        Event* evnt = new SimLoopExitEvent("synthesis finished", 0);
                        schedule(evnt, curTick());
                    }
                    else
                    {
                        if (traceData)
                        {
                            delete traceData;
                            traceData = NULL;
                        }
                    }

                    //if (curStaticInst && (!curStaticInst->isMicroop() || curStaticInst->isLastMicroop())) 
                    //{
                    //    inst_num++;
                    //}
                }//end of if(synth)

                postExecute();
            }

            // @todo remove me after debugging with legion done
            if (curStaticInst && (!curStaticInst->isMicroop() ||
                        curStaticInst->isFirstMicroop()))
                instCnt++;

            // profile for SimPoints if enabled and macro inst is finished
            if (simpoint && curStaticInst && (!curStaticInst->isMicroop() ||
                                        curStaticInst->isLastMicroop())) {
                profileSimPoint();
            }

            Tick stall_ticks = 0;
            if (simulate_inst_stalls && icache_access)
                stall_ticks += icache_latency;

            if (simulate_data_stalls && dcache_access)
                stall_ticks += dcache_latency;

            if (stall_ticks) {
                // the atomic cpu does its accounting in ticks, so
                // keep counting in ticks but round to the clock
                // period
                latency += divCeil(stall_ticks, clockPeriod()) *
                    clockPeriod();
            }

        }
        if(fault != NoFault || !stayAtPC)
            advancePC(fault);
    }

    if (tryCompleteDrain())
        return;

    // instruction takes at least one cycle
    if (latency < clockPeriod())
        latency = clockPeriod();

    if (_status != Idle)
        schedule(tickEvent, curTick() + latency);
}


void
MyCPU::printAddr(Addr a)
{
    dcachePort.printAddr(a);
}

void
MyCPU::profileSimPoint()
{
    if (!currentBBVInstCount)
        currentBBV.first = thread->pcState().instAddr();

    ++intervalCount;
    ++currentBBVInstCount;

    // If inst is control inst, assume end of basic block.
    if (curStaticInst->isControl()) {
        currentBBV.second = thread->pcState().instAddr();

        auto map_itr = bbMap.find(currentBBV);
        if (map_itr == bbMap.end()){
            // If a new (previously unseen) basic block is found,
            // add a new unique id, record num of insts and insert into bbMap.
            BBInfo info;
            info.id = bbMap.size() + 1;
            info.insts = currentBBVInstCount;
            info.count = currentBBVInstCount;
            bbMap.insert(std::make_pair(currentBBV, info));
            *bbsequenceStream << info.id << "\n";
        } else {
            // If basic block is seen before, just increment the count by the
            // number of insts in basic block.
            BBInfo& info = map_itr->second;
            assert(info.insts == currentBBVInstCount);
            info.count += currentBBVInstCount;
            *bbsequenceStream << info.id << "\n";
        }
        currentBBVInstCount = 0;

        // Reached end of interval if the sum of the current inst count
        // (intervalCount) and the excessive inst count from the previous
        // interval (intervalDrift) is greater than/equal to the interval size.
        if (intervalCount + intervalDrift >= intervalSize) {
            // summarize interval and display BBV info
            std::map<uint64_t, uint64_t> counts;
            for (auto map_itr = bbMap.begin(); map_itr != bbMap.end();
                    ++map_itr) {
                BBInfo& info = map_itr->second;
                if (info.count != 0) {
                    counts[info.id] = info.count;
                    info.count = 0;
                }
            }

            // Print output BBV info
            *simpointStream << "T";
            for (auto cnt_itr = counts.begin(); cnt_itr != counts.end();
                    ++cnt_itr) {
                *simpointStream << ":" << cnt_itr->first
                                << ":" << cnt_itr->second << " ";
            }
            *simpointStream << "\n";

            intervalDrift = (intervalCount + intervalDrift) - intervalSize;
            intervalCount = 0;
        }
    }
}

inline void MyCPU::insertInstTable(Addr a)
{
    std::stringstream sstr;

    if (curStaticInst->isDirectCtrl())
    {
        Addr tgt = curStaticInst->branchTarget(thread->pcState()).pc();    
        sstr<<curStaticInst->disassemble(a)<<"\tL"<<hex<<tgt;
        map<Addr, InstInfo>::iterator instmap_it;
        instmap_it = instTable.find(tgt);
        if (instmap_it == instTable.end())
        {
            //insert target to instTable
            InstInfo iinfo;
            iinfo.disassembly = "WaitTarget";
            instTable.insert(std::make_pair(tgt, iinfo));
        }
    }
    //else if (curStaticInst->isFirstMicroop())
    //{
    //    cout<<"First Microop"<<endl;
    //    sstr<<"L0x"<<hex<<a<<": "<<"macroinst";
    //}
    else if (curStaticInst->isMicroop())
    {
        if ((unsigned short)((thread->pcState()).microPC()) == 0)
        {
            sstr<<"WaitInst";
        }
        else
            return;
    }
    else
    {
        std::string str;
        str = curStaticInst->disassemble(a);
        size_t pos;
        //format correctly instruction
        if ((pos = str.find("LSL")) != string::npos && (str.find("ldr") != string::npos || str.find("str") != string::npos))
        {
            str = str.replace(pos, 3, ",lsl"); 
            sstr<<str;
        }
        else if (str.find("uxtb") != string::npos)
        {
            sstr<<"WaitInst";
        }
        else if (str.find("bx") != string::npos || str.find("blx") != string::npos)
        {
            sstr<<"WaitInst";
        }
        else if (str.find("svc") != string::npos)
        {
            sstr<<"WaitInst";
        }
        else if (str.find("mov") != string::npos)
        {
            sstr<<"WaitInst";
        }
        else if (str.find("mul") != string::npos\
                || str.find("mla") != string::npos\
                || str.find("mls") != string::npos\
                || str.find("maa") != string::npos\
                || str.find("mua") != string::npos\
                || str.find("mus") != string::npos\
                || str.find("mia") != string::npos\
                )
        {
            sstr<<"WaitInst";
        }
        else if (str.find("vmrs") != string::npos\
                || str.find("vmsr") != string::npos)
        {
            sstr<<"WaitInst";
        }
        else if (str.find("hdr") != string::npos)
        {
            sstr<<"WaitInst";
        }
        else
        {
            sstr<<str;
        }
    }
    InstInfo iinfo;
    iinfo.disassembly = sstr.str();
    instTable.insert(std::make_pair(a, iinfo));
}

inline void MyCPU::prepareData(Addr a, uint64_t data, int stride)
{
    bool insert = false;
    unsigned short byte;
    stringstream ss;
    for (int i=0; i<stride; i++)
    {
        //pair<unordered_set<Addr>::iterator, bool> p = ignoreSet.insert(a+i);
        auto p = ignoreSet.insert(a+i);
        insert = p.second;
        if (insert)
        {
            ss.str("");
            byte = (data >> i*8) & 0x00000000000000ff;
            ss << "B" << hex << byte;
            prepareTable.insert(make_pair(a+i, ss.str()));
        }
    }
}
////////////////////////////////////////////////////////////////////////
//
//  MyCPU Simulation Object
//
MyCPU *
MyCPUParams::create()
{
    numThreads = 1;
    if (!FullSystem && workload.size() != 1)
        panic("only one workload allowed");
    return new MyCPU(this);
}
