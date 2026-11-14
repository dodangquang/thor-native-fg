layout(std430,binding=THOR_QUALITY_BINDING) buffer Quality {
 uint rgbSum;uint lumaSum;uint changed;uint valid;
 uint count;uint boundary;uint ambiguous;uint reliable;
 uint previousHistogram[64];uint currentHistogram[64];
 uint holdoff;uint allow;uint reasons;uint historySerial;
} stats;
