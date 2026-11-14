package com.thor.nativefg;

import java.util.HashMap;
import java.util.Locale;
import java.util.Map;

/** Native engine telemetry. Incoming/present requests are not panel scanouts. */
final class RuntimeMetrics {
    final int pid, inputWidth, inputHeight, outputWidth, outputHeight, flags;
    final boolean eligible;
    final boolean spatial;
    final int spatialWidth,spatialHeight,spatialModel;
    /** Whole frame-generation factor as reported by the layer: 1 = off, 2 = one extra frame, 3 = two extra frames. */
    final int factor;
    final boolean measuredDisplay;
    final double displayedFps;
    final double sourceFps, generatedFps;
    final long receivedAt;

    private RuntimeMetrics(Map<String,String> values,long now) {
        pid=integer(values,"pid");inputWidth=integer(values,"in_w");inputHeight=integer(values,"in_h");
        outputWidth=integer(values,"out_w");outputHeight=integer(values,"out_h");flags=integer(values,"flags");
        eligible=integer(values,"eligible")==1;sourceFps=number(values,"source_fps");generatedFps=number(values,"generated_fps");
        boolean version2="2".equals(values.get("v"));
        receivedAt=version2?Long.parseLong(values.get("uptime_ms")):now;
        spatial=version2?integer(values,"sr")==1:true;
        spatialWidth=version2?integer(values,"sr_in_w"):inputWidth;spatialHeight=version2?integer(values,"sr_in_h"):inputHeight;
        spatialModel=values.containsKey("sa")?integer(values,"sa"):0;
        if(spatialModel<0||spatialModel>4)throw new IllegalArgumentException("Unknown spatial algorithm");
        // Optional field: a layer without triple support reports neither, and
        // then the HUD must stay exactly as before.
        int tripleFlag=version2&&values.containsKey("triple")?integer(values,"triple"):0;
        if(tripleFlag!=0&&tripleFlag!=1)throw new IllegalArgumentException("Unknown frame generation factor");
        factor=tripleFlag==1?3:2;
        measuredDisplay="1".equals(values.get("display_valid"));displayedFps=measuredDisplay?number(values,"display_fps"):0;
        if(pid<=0||inputWidth<=0||inputHeight<=0||outputWidth<=0||outputHeight<=0||sourceFps<0||generatedFps<0||sourceFps>2000||generatedFps>4000)
            throw new IllegalArgumentException("Invalid engine telemetry");
        if(spatial&&(spatialWidth<=0||spatialHeight<=0))throw new IllegalArgumentException("Invalid SR dimensions");
        if(displayedFps<0||displayedFps>4000)throw new IllegalArgumentException("Invalid presentation rate");
    }
    static RuntimeMetrics parse(String line,long now) {
        int offset=line.indexOf("THOR_METRICS ");if(offset<0)return null;
        try {
            Map<String,String> values=new HashMap<>();
            for(String token:line.substring(offset+13).trim().split("\\s+")){int equal=token.indexOf('=');if(equal>0)values.put(token.substring(0,equal),token.substring(equal+1));}
            if(!"1".equals(values.get("v"))&&!"2".equals(values.get("v")))return null;
            return new RuntimeMetrics(values,now);
        } catch(RuntimeException invalid) {return null;}
    }
    private static int integer(Map<String,String> v,String key){return Integer.parseInt(v.get(key));}
    private static double number(Map<String,String> v,String key){double value=Double.parseDouble(v.get(key));if(Double.isNaN(value)||Double.isInfinite(value))throw new IllegalArgumentException("Nonfinite rate");return value;}
    boolean hasUpscaling(){return spatial&&outputWidth>spatialWidth&&outputHeight>spatialHeight;}
    boolean fresh(long now){return now>=receivedAt&&now-receivedAt<3500;}
    String srLabel(String externalSr) {
        if(hasUpscaling()){
            String tag=spatialModel==4?"Spatial9":spatialModel==3?"GSR2-cũ":spatialModel==2?"Neural2D":spatialModel==1?"Neural":"GSR1";
            return String.format(Locale.US,"%s %d×%d→%d×%d",tag,spatialWidth,spatialHeight,outputWidth,outputHeight);
        }
        if(externalSr!=null&&!externalSr.isEmpty())return externalSr;
        return "SR TẮT";
    }
    String hud() {
        return hud(null);
    }
    // FPS half of the HUD only: the overlay appends the AA mode and the device
    // temperature, which this class has no business parsing.
    String fpsOnly() {
        String mode=eligible&&factor==3?" · 3×":"";
        String fps=eligible?(measuredDisplay?String.format(Locale.US,"Gốc %.0f | FG %.0f",sourceFps,displayedFps):String.format(Locale.US,"Gốc %.0f | FG ≈%.0f",sourceFps,sourceFps+generatedFps)):String.format(Locale.US,"Gốc %.0f | FG —",sourceFps);
        return fps+mode;
    }
    String hud(String externalSr) {
        String mode=eligible&&factor==3?" · 3×":"";
        String fps=eligible?(measuredDisplay?String.format(Locale.US,"Gốc %.0f | FG %.0f",sourceFps,displayedFps):String.format(Locale.US,"Gốc %.0f | FG ≈%.0f",sourceFps,sourceFps+generatedFps)):String.format(Locale.US,"Gốc %.0f | FG —",sourceFps);
        return fps+mode+" | "+srLabel(externalSr);
    }
    String detail() {
        String algorithm=spatialModel==4?"Spatial 9-tap ON":spatialModel==3?"GSR2 thử nghiệm cũ ON — mở lại game để cập nhật":spatialModel==2?"Neural 2D giảm viền ON":spatialModel==1?"Neural 2D ON":"GSR1 ON";
        return hasUpscaling()?String.format(Locale.US,algorithm+" · %d×%d → %d×%d",spatialWidth,spatialHeight,outputWidth,outputHeight):"SR: chưa upscale";
    }
}
