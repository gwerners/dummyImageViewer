#!/bin/bash

#sudo apt install libavif-dev
#sudo apt install libwebp-dev libjxl-dev


argc=$#

#https://vaneyckt.io/posts/safer_bash_scripts_with_set_euxo_pipefail/
set -euo pipefail

#show commands (for debuging)
#set -x

#move to script dir 
cd "$(dirname "${0}")"

#save current dir as base dir
ROOT=$(pwd)

#where to store/get tar.gz / tar.bz2
DEPENDENCIES=../dependencies

#sudo apt install -f ninja-build

#avoid rm error if file doesn't exists
function safeRM {
  for file in ${@} ; do
    if [[ -f "${file}" ]]; then
      rm "${file}"
    fi
  done
}
#avoid rm error if directory doesn't exists
function safeRMDIR {
  for dir in ${@} ; do
    if [[ -d "${dir}" ]]; then
      rm -rf "${dir}"
    fi
  done
}
function safeMKDIR {
  if [ ! -d "$1" ]; then
    mkdir "$1"
  fi
}
function TAR {
  safeMKDIR "${DEPENDENCIES}"
  if [[ -f "${DEPENDENCIES}/$1.tar.gz" ]]; then
    echo "skipped"
    return
  else
    tar -cjf $1.tar.bz2 $1
    mv $1.tar.bz2 ${DEPENDENCIES}/
    echo "done"
    return
  fi
}
function UNTAR {
  if [[ ! -d "$1" ]]; then
    if [[ ! -f "${DEPENDENCIES}/$1.tar.gz" ]] && [[ ! -f "${DEPENDENCIES}/$1.tar.bz2" ]]; then
      echo "skipped"
      return
    else
      tar -xf ${DEPENDENCIES}/$1.tar.*
      echo "done"
      return
    fi
  fi
}
#check for dependency
function CheckNeeded {
  NEEDED=$(which ${1})
  if [ ! -f "${NEEDED}" ]; then
    echo "${1} is a dependency, you must install it first!"
    exit 0
  fi
}
function get {
  untarResult=$(UNTAR "$1")
  if [[ "${untarResult}"  == "skipped" ]]; then
    if [[ ! -d "$1" ]]; then
      git clone "$2"
      TAR "$1"
      cd ${ROOT}
    fi
  fi
}
function getAll {
  get fmt https://github.com/fmtlib/fmt.git
  get imgui https://github.com/ocornut/imgui.git
  get json https://github.com/nlohmann/json.git
  get raylib https://github.com/raysan5/raylib.git
  get rlImGui https://github.com/raylib-extras/rlImGui.git
}

function cleanOldBuild {
  ./clean.bash build
}
function build {
  pushd raylib
    mkdir -p build
    pushd build
      # o raylib novo desliga JPG/TGA/PNM por padrao; o viewer abre esses formatos
      cmake .. -G Ninja -DCUSTOMIZE_BUILD=ON \
        -DSUPPORT_FILEFORMAT_JPG=ON \
        -DSUPPORT_FILEFORMAT_TGA=ON \
        -DSUPPORT_FILEFORMAT_PNM=ON
      ninja
    popd
  popd
  safeMKDIR build
  pushd build
    cmake ..
    make all
  popd
}
function run {
  ./build/work/work
}

###############################################################################
#script main:
CheckNeeded g++ || exit 1
CheckNeeded cmake || exit 1
CheckNeeded ninja || exit 1
CheckNeeded git  || exit 1
getAll

build
run

###############################################################################
