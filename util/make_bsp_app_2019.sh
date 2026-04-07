#!/bin/bash

source /tools/Xilinx/SDK/2019.1/settings64.sh
mkdir /home/embe2024/workspace/GreedyFTL
xsct -nodisp util/make_bsp_app_2019.tcl
rm /home/embe2024/workspace/GreedyFTL/cosmos_app/src/platform*
rm /home/embe2024/workspace/GreedyFTL/cosmos_app/src/hello*

