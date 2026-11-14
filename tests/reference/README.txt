flow_serial.comp is the serial reference for Thor's current coarse-to-fine
flow algorithm, originally based on commit c98e427. Both versions now use
the extended search window and reliable-match counter. It is not a frozen
baseline; keep candidate order, costs and tie behavior aligned for comparison.

build.ps1 compiles both implementations and the headless thor-gpu-bench tool.
On an authorized Android test device, run the tool with:
  thor-gpu-bench 1920 1080 serial
  thor-gpu-bench 1920 1080 cooperative
  thor-gpu-bench 257 145 compare_toggle
The optional arguments are width, height and mode. compare and compare_toggle
perform GPU readbacks of flow, midpoint, real-frame and quality results across
motion/cut/history-reset fixtures. Comparison/readback timings must not be used
as production-performance figures. Stage timestamps exclude source rendering,
acquisition, presentation and input latency; production uses no stage profiling.

The layer defaults to cooperative. debug.thorfg.flow=serial selects the serial
pipeline without recompiling or resetting source history. Clear the property
to restore the production default. debug.thorfg.flow_compare=1 before the FG
engine is first prepared enables eight shadow comparisons on actual source
frames. This allocates a second engine and readback memory, releases them after
the comparisons, and must be disabled for performance measurements. A mismatch
disables that swapchain's FG and preserves the original frame. Clear all test
properties after use; none is required for normal automatic activation.
