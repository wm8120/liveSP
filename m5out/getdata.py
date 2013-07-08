#!/usr/bin/python
import sys
import re

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print "usage: ./getdata.py [binary name]";
        exit(-1);

    binary = sys.argv[1];
    #try:
    #    sp_file = open(sys.argv[1]+".simpoints", 'r'); 
    #except IOError:
    #    print 'cannot open', sys.argv[1]+".simpoints"
    #    exit(-1);

    #try:
    #    wt_file = open(sys.argv[1]+".weights", 'r'); 
    #except IOError:
    #    print 'cannot open', sys.argv[1]+'.weights';
    #    exit(-1);
    
    try:
        stat = open(binary+'.stat', 'r');
    except IOError:
        print 'cannot open', binary+'.stat';
        exit(-1);

    mm = stat.read();
    common = '\s*\S+(?=\s*#.*)';
    host_time = re.findall('(?<=host_seconds)'+common, mm);
    print 'real time: ', host_time;
    insts = re.findall('(?<=sim_insts)\s*\d+(?=\s*#.*)', mm);
    print 'instructions: ', insts;
    ipc = re.findall('(?<=system.cpu.ipc)\s*\d+\.\d+(?=\s*#.*)', mm);
    print 'ipc:', ipc;
    icache = re.findall('(?<=system.cpu.icache.overall_mshr_miss_rate::cpu.inst)'+common, mm);
    print 'icache miss: ', icache;
    dcache = re.findall('(?<=system.cpu.dcache.overall_mshr_miss_rate::cpu.data)'+common, mm);
    print 'dcache miss: ', dcache;
    l2i = re.findall('(?<=system.l2.overall_mshr_miss_rate::cpu.inst)'+common, mm);
    print 'l2 instruction miss: ', l2i;
    l2d = re.findall('(?<=system.l2.overall_mshr_miss_rate::cpu.data)'+common, mm);
    print 'l2 data miss: ', l2d;
    l2tt = re.findall('(?<=system.l2.overall_mshr_miss_rate::total)'+common, mm);
    print 'l2 total miss: ', l2tt;
    branch_wrong = re.findall('(?<=system.cpu.branchPred.condIncorrect)\s*\d+(?=\s*#.*)', mm);
    print 'branch miss prediction: ', branch_wrong;
    branch_tt = re.findall('(?<=system.cpu.branchPred.condPredicted)\s*\d+(?=\s*#.*)', mm);
    print 'branch total prediction: ', branch_tt;
    
    exit(0);


    #wt_file = open(sys.argv[1]+".weights");
