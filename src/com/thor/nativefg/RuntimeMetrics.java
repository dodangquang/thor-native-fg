package com.thor.nativefg;

import java.util.HashMap;
import java.util.Locale;
import java.util.Map;

/** Native engine telemetry. Incoming/present requests are not panel scanouts. */
final class RuntimeMetrics {
    final int pid, inputWidth, inputHeight, outputWidth, outputHeight, flags;
    final boolean eligible;
    final double sourceFps, generatedFps;
    final long receivedAt;

    private RuntimeMetrics(Map<String,String> values,long now) {
        pid=integer(values,"pid");inputWidth=integer(values,"in_w");inputHeight=integer(values,"in_h");
        outputWidth=integer(values,"out_w");outputHeight=integer(values,"out_h");flags=integer(values,"flags");
        eligible=integer(values,"eligible")==1;sourceFps=number(values,"source_fps");generatedFps=number(values,"generated_fps");receivedAt=now;
        if(pid<=0||inputWidth<=0||inputHeight<=0||outputWidth<=0||outputHeight<=0||sourceFps<0||generatedFps<0||sourceFps>2000||generatedFps>4000)
            throw new IllegalArgumentException("Invalid engine telemetry");
    }
    static RuntimeMetrics parse(String line,long now) {
        int offset=line.indexOf("THOR_METRICS ");if(offset<0)return null;
        try {
            Map<String,String> values=new HashMap<>();
            for(String token:line.substring(offset+13).trim().split("\\s+")){int equal=token.indexOf('=');if(equal>0)values.put(token.substring(0,equal),token.substring(equal+1));}
            if(!"1".equals(values.get("v")))return null;
            return new RuntimeMetrics(values,now);
        } catch(RuntimeException invalid) {return null;}
    }
    private static int integer(Map<String,String> v,String key){return Integer.parseInt(v.get(key));}
    private static double number(Map<String,String> v,String key){double value=Double.parseDouble(v.get(key));if(Double.isNaN(value)||Double.isInfinite(value))throw new IllegalArgumentException("Nonfinite rate");return value;}
    boolean hasUpscaling(){return outputWidth>inputWidth||outputHeight>inputHeight;}
    boolean fresh(long now){return now>=receivedAt&&now-receivedAt<3500;}
    String hud() {
        if(!eligible)return "THOR · Vulkan chưa hỗ trợ\nFG: chưa chạy · SR: chưa chạy";
        String fg=generatedFps>=0.5?"ON":((flags&3)!=0?"bỏ cut":((flags&12)!=0?"bỏ chuyển động lỗi":"chờ khung"));
        String sr=hasUpscaling()?String.format(Locale.US,"%d×%d → %d×%d",inputWidth,inputHeight,outputWidth,outputHeight):"chưa upscale";
        return String.format(Locale.US,"THOR · Nguồn %.1f FPS\nFG: %s (+%.1f gửi/s)\nSR: %s",sourceFps,fg,generatedFps,sr);
    }
}
