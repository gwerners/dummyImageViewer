#!/bin/bash

argc=$#

#https://vaneyckt.io/posts/safer_bash_scripts_with_set_euxo_pipefail/
set -euo pipefail

#show commands
#set -x

#move para o diretorio do script caso seja executado de outro diretorio:
cd "$(dirname "${0}")"

#save base dir
ROOT=$(pwd)

function safeRM {
  for file in ${@} ; do
    if [[ -f "${file}" ]]; then
      rm "${file}"
    fi
  done
}
function safeRMDIR {
  for dir in ${@} ; do
    if [[ -d "${dir}" ]]; then
      rm -rf "${dir}"
    fi
  done
}
function clean_base {
  safeRMDIR build
  safeRMDIR build-win
  safeRMDIR .vs
  safeRMDIR fmt
  safeRMDIR imgui
  safeRMDIR json
  safeRMDIR raylib
  safeRMDIR rlImGui
  safeRMDIR stb
  safeRMDIR .cache
  safeRM *.user
  safeRM *.ini
  cd ${ROOT}
}
function clean {
  clean_base
}
function buildClean {
  clean_base
  echo "done"
}
if [[ "${argc}" -ne 0 ]]; then
  if [[ ${1} == "build" ]]; then
    echo "removing build products"
    buildClean
  fi
else
  clean
fi
