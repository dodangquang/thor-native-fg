#!/system/bin/sh
# Restore the original empty layer configuration observed before this session's tests.
set -eu
setprop debug.thorfg.enabled 0
setprop debug.thorfg.target ''
setprop debug.thorfg.validate 0
settings put global enable_gpu_debug_layers 0
settings delete global gpu_debug_app
settings delete global gpu_debug_layers
settings delete global gpu_debug_layer_app
am clear-debug-app
