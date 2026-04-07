#!/bin/bash

source /tools/Xilinx/SDK/2019.1/settings64.sh
WORKSPACE="/home/embe2024/workspace"
UTIL="$WORKSPACE/util"

PROFILE="debug"
TYPE=""
SCHEME=""

if [ $# -ne 2 ]
then
    echo -e "Usage:\n  ./setup.sh <project> <build/load/scan/show> \n"
    echo -e "Description:"
    echo "- select scheme which you want to build, load, scan or show"
    echo "- select type of action i.e. build, load, scan, show"
    echo "ex) ./setup.sh GreedyFTL build"
    exit 0
fi

if [[ "$1" == "GreedyFTL" ]]
then
    echo "Scheme: $1"
    SCHEME="$1"
else
    echo "Invalid Scheme!"
    echo "Scheme List: GreedyFTL"
    exit -1
fi

if [[ "$2" == "load" || "$2" == "build" || "$2" == "scan" || "$2" == "show" ]]
then
    echo "TYPE: $2"
    if [[ "$2" == "show" ]]
    then
        echo "Press the button 'X' to start"
    fi
    TYPE="$2"
else
    echo "Invalid Type!"
    echo "Type list: build/load/scan/show"
    exit -1
fi

if [[ "$3" == "debug" || "$3" == "release" ]]
then
    echo "PROFILE: $3"
    PROFILE="$3"
else
    PROFILE="Debug"
fi

TARGET="$WORKSPACE/$SCHEME"

if [[ "$TYPE" == "build" ]] 
then
	pushd $UTIL > /dev/null
	    xsct -nodisp ./$TYPE.tcl $TARGET
	popd > /dev/null
elif [[ "$TYPE" == "load" ]]
then
	pushd $WORKSPACE/util/cosmos_scripts > /dev/null
	    xsct -nodisp ./cosmos_init.tcl -e $TARGET/cosmos_app/$PROFILE/cosmos_app.elf
	popd > /dev/null
elif [[ "$TYPE" == "scan" ]]
then 
	PCIE_ADDR=`lspci | grep "Xilinx Corporation Device 7028" | cut -d ' ' -f 1`
	if [ -z $PCIE_ADDR ]
	then
		echo "Xilinx FPGA board is not detected!";
		exit 1;
	fi

	PCIE_ADDR=0000:`echo $PCIE_ADDR | cut -d' ' -f 1`
	echo "Xilinx FPGA board is detected at $PCIE_ADDR!"

	sudo sh -c 'echo 1 > /sys/bus/pci/devices/'$PCIE_ADDR'/remove'
	sleep 1
	sudo sh -c 'echo 1 > /sys/bus/pci/rescan'
else
	sudo tio -e /dev/`dmesg | grep -e "cp210x converter now attached" | tail -n1 | rev | cut -d ' ' -f1 | rev`
fi
