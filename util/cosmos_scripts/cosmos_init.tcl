package require cmdline

set parameters {
    {i.arg   "127.0.0.1"   "ip of hardware server. Default: localhost"}
    {p.arg   "3121"   "port number. Default: 3121"}
    {e.arg   ""       "elf binary path"}
    {b.arg   "./config/sys_top_wrapper.bit"   "bitstream path"}
    {h.arg   "./config/system.hdf"   "system.hdf path"}
}

set usage "- A simple script to initialize cosmos"
array set opts [cmdline::getoptions ::argv $parameters $usage]

proc init_and_bitstream_flush {bitstream_path hdf_path elf_path cable} {
    targets -set -nocase -filter {name =~"APU*" && jtag_cable_name=~$cable}
    rst -system
    after 3000
    
    # puts "fpga"
    targets -set -filter {name !="APU" && jtag_cable_name=~$cable && level==0}
    fpga -file $bitstream_path
    loadhw -hw $hdf_path -mem-ranges [list {0x40000000 0xbfffffff}]
    configparams force-mem-access 1

    # puts "init"
    targets -set -nocase -filter {name =~"APU*" && jtag_cable_name =~$cable}
    ps7_init
    ps7_post_config

    # puts "download elf"
    targets -set -nocase -filter {name =~"ARM*0" && jtag_cable_name =~$cable}
    dow $elf_path

    configparams force-mem-access 0

    targets -set -nocase -filter {name =~"ARM*0" && jtag_cable_name =~$cable}
    con
}

# check file exist
set out 0
if {[file exist $opts(b)] != 1} {
    puts "$opts(b) does not exist!"
    set out 1
}

if {[file exist $opts(h)] != 1} {
    puts "$opts(h) does not exist!"
    set out 1
}

if {[file exist $opts(e)] != 1} {
    puts "elf binary $opts(e) does not exist!"
    set out 1
}

if {$out} {
    return -1;
}

puts "connect -url tcp:$opts(i):$opts(p)"
connect -url tcp:$opts(i):$opts(p)

set jtag_list [split [jtag target -filter {level == 0}] "\\\n"]

source "./config/ps7_init.tcl"
foreach jtag_name $jtag_list {
    set cable [lindex [split [string map {"  " ,} [string trim $jtag_name]] ","] 1]
    puts "init and start for $cable"
    init_and_bitstream_flush $opts(b) $opts(h) $opts(e) $cable
}
