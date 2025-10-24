#!/usr/bin/env bash
set -euo pipefail

MODULE_DIR="/home/fide/gitrepository/LxDrTemp"
DEV_NODE="/dev/nxp_simtemp"

echo "[simtemp] Instalando artefactos"
sudo insmod "${MODULE_DIR}/simtemp.ko"

echo "[simtemp] Ejecutando autoprueba"
"${MODULE_DIR}/apitest/apitest" "${DEV_NODE}" --test

echo "[simtemp] Desinstalando módulo"
sudo rmmod simtemp
