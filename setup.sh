#!/bin/bash
source /tools/Xilinx/Vivado/2019.1/settings64.sh
source /tools/Xilinx/SDK/2019.1/settings64.sh
WORKSPACE="/home/dhmin/VECTORSSD"
UTIL="$WORKSPACE/util"

PROFILE="debug"
TYPE=""
SCHEME=""

if [ $# -ne 2 ]
then
    echo -e "Usage:\n  ./setup.sh <project> <build/run/show> [optional] \n"
    echo -e "Description:"
    echo "- select scheme which you want to build or run or show"
    echo "- select type of action i.e. build, run, show"
    echo "ex) ./setup.sh vector1 build"
    exit 0
fi

if [[ "$1" == "vector1" || "$1" == "greedy_ftl" || "$1" == "debugprescar" ]]
then
    echo "Scheme: $1"
    SCHEME="$1"
else
    echo "Invalid Scheme!"
    exit -1
fi

if [[ "$2" == "run" || "$2" == "build" || "$2" == "show" ]]
then
    echo "TYPE: $2"
    if [[ "$2" == "show" ]]
    then
        # echo "Press the button 'X' to start"
        echo "Please wait a second..."
    fi
    TYPE="$2"
else
    echo "Invalid Type!"
    echo "Type list: build/run/show"
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
            # echo -e "xsct -nodisp ./$TYPE.tcl "
            echo $PWD
            echo $TARGET
            xsct -nodisp ./$TYPE.tcl $TARGET
            # xsct -nodisp /home/micron/vectorssd/vectorssd/cosmos_hw/ps7_init.tcl $TARGET
            echo "build done"
        popd > /dev/null
elif [[ "$TYPE" == "run" ]]
then
        pushd $WORKSPACE/util/cosmos_scripts > /dev/null
        # echo -e "xsct -nodisp ./cosmos_init.tcl -e $TARGET/cosmos_app/$PROFILE/cosmos_app.elf"
        # sudo  xsct -nodisp ./cosmos_init.tcl -e $TARGET/cosmos_app/$PROFILE/cosmos_app.elf
        sudo /tools/Xilinx/SDK/2019.1/bin/xsct -nodisp ./cosmos_init.tcl -e "$TARGET/cosmos_app/$PROFILE/cosmos_app.elf"

        #  xsct -nodisp /home/micron/vectorssd/vectorssd/cosmos_hw/ps7_init.tcl -e $TARGET/cosmos_app/$PROFILE/cosmos_app.elf
        popd > /dev/null
else
        sudo tio -e /dev/`dmesg | grep -e "cp210x converter now attached" | tail -n1 | rev | cut -d ' ' -f1 | rev`
fi


# echo -n "X" | sudo tee /dev/ttyUSB0