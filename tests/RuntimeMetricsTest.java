package com.thor.nativefg;

public class RuntimeMetricsTest {
    private static int checks;
    private static void check(boolean condition,String label){checks++;if(!condition)throw new AssertionError(label);System.out.println("PASS "+label);}
    public static void main(String[] args){
        String line="I/ThorNativeLayer( 1234): THOR_METRICS v=1 pid=1234 swap=1 eligible=1 in_w=1920 in_h=1080 out_w=1920 out_h=1080 source_fps=30.0 generated_fps=28.0 flags=0";
        RuntimeMetrics sameSize=RuntimeMetrics.parse(line,10000);
        check(sameSize!=null&&sameSize.pid==1234,"parse native protocol including logcat prefix");
        check(!sameSize.hasUpscaling()&&sameSize.hud().contains("chưa upscale"),"same-size filtering never claims SR active");
        check(sameSize.hud().contains("FG: ON")&&sameSize.hud().contains("28.0 gửi/s"),"FG status uses engine-generated request rate");
        check(sameSize.fresh(13499)&&!sameSize.fresh(13500)&&!sameSize.fresh(9000),"stale or reversed-clock telemetry cannot appear live");
        RuntimeMetrics upscale=RuntimeMetrics.parse(line.replace("in_w=1920 in_h=1080","in_w=960 in_h=540"),10000);
        check(upscale.hasUpscaling()&&upscale.hud().contains("960×540 → 1920×1080"),"actual dimension increase identifies SR");
        RuntimeMetrics cut=RuntimeMetrics.parse(line.replace("generated_fps=28.0 flags=0","generated_fps=0 flags=3"),10000);
        check(cut.hud().contains("bỏ cut"),"rejected cut is visible instead of showing FG ON");
        RuntimeMetrics unsupported=RuntimeMetrics.parse(line.replace("eligible=1","eligible=0"),10000);
        check(unsupported.hud().contains("chưa hỗ trợ")&&!unsupported.hud().contains("ON"),"unsupported swapchain never claims active FG");
        check(RuntimeMetrics.parse(line.replace("v=1","v=9"),0)==null,"unknown protocol rejected");
        check(RuntimeMetrics.parse(line.replace("source_fps=30.0","source_fps=NaN"),0)==null,"NaN rejected");
        check(RuntimeMetrics.parse(line.replace("generated_fps=28.0","generated_fps=-1"),0)==null,"negative rate rejected");
        check(RuntimeMetrics.parse(line.replace("in_w=1920","in_w=0"),0)==null,"invalid image size rejected");
        check(RuntimeMetrics.parse("THOR_METRICS v=1 pid=1234",0)==null,"truncated logs rejected safely");
        check(RuntimeMetrics.parse("COUNTERS sources=60",0)==null,"unrelated native logs ignored");
        System.out.println("RESULT checks="+checks+" failures=0");
    }
}
