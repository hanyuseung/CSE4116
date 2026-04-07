set ws  "/home/embe2024/workspace/GreedyFTL"
set utild  "/home/embe2024/workspace/util"
set hdf "/home/embe2024/workspace/util/cosmos_scripts/config/system.hdf"

setws $ws

foreach p {cosmos_app cosmos_app_bsp cosmos_hw .metadata .sdk .Xil} {
  if {[file exists "$ws/$p"]} {
    file delete -force "$ws/$p"
  }
}

createhw  -name cosmos_hw -hwspec $hdf
createbsp -name cosmos_app_bsp -hwproject cosmos_hw -proc ps7_cortexa9_0 -os standalone
createapp -name cosmos_app    -hwproject cosmos_hw -proc ps7_cortexa9_0 -os standalone -lang C

if {[file isdirectory "$utild/origin_src"]} {
  importsources -name cosmos_app -path "$utild/origin_src"
} else {
  puts "WARN: $util/origin_src not found"
}

projects -build -type bsp -name cosmos_app_bsp
#projects -build -type app -name cosmos_app

