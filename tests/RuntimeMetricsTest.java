package com.thor.nativefg;

public class RuntimeMetricsTest {
    private static int checks;
    private static void check(boolean condition,String label){checks++;if(!condition)throw new AssertionError(label);System.out.println("PASS "+label);}
    public static void main(String[] args){
        String line="I/ThorNativeLayer( 1234): THOR_METRICS v=1 pid=1234 swap=1 eligible=1 in_w=1920 in_h=1080 out_w=1920 out_h=1080 source_fps=30.0 generated_fps=28.0 flags=0";
        RuntimeMetrics sameSize=RuntimeMetrics.parse(line,10000);
        check(sameSize!=null&&sameSize.pid==1234,"parse native protocol including logcat prefix");
        check(!sameSize.hasUpscaling()&&sameSize.detail().contains("chưa upscale"),"same-size filtering never claims SR active");
        check(sameSize.hud().equals("Gốc 30 | FG ≈58")&&!sameSize.hud().contains("\n"),"compact HUD shows only source rate and estimated rate after FG");
        check(sameSize.fresh(13499)&&!sameSize.fresh(13500)&&!sameSize.fresh(9000),"stale or reversed-clock telemetry cannot appear live");
        RuntimeMetrics upscale=RuntimeMetrics.parse(line.replace("in_w=1920 in_h=1080","in_w=960 in_h=540"),10000);
        check(upscale.hasUpscaling()&&upscale.detail().contains("960×540 → 1920×1080"),"actual dimension increase identifies SR");
        RuntimeMetrics cut=RuntimeMetrics.parse(line.replace("generated_fps=28.0 flags=0","generated_fps=0 flags=3"),10000);
        check(cut.hud().equals("Gốc 30 | FG ≈30"),"zero generated rate during rejected cut does not inflate output rate");
        RuntimeMetrics unsupported=RuntimeMetrics.parse(line.replace("eligible=1","eligible=0"),10000);
        check(unsupported.hud().equals("Gốc 30 | FG —"),"unsupported swapchain never claims active FG");
        String version2=line.replace("v=1","v=2")+" sr=1 sr_in_w=960 sr_in_h=540 uptime_ms=10000";
        RuntimeMetrics separate=RuntimeMetrics.parse(version2,10000);
        check(separate!=null&&separate.hasUpscaling()&&separate.inputWidth==1920&&separate.detail().contains("GSR1 ON"),"SR source dimensions are separate from full-resolution FG input");
        check(!RuntimeMetrics.parse(version2.replace("sr=1","sr=0"),10000).hasUpscaling(),"old SR dimensions cannot keep SR ON on a frame without an upscale pass");
        check(!RuntimeMetrics.parse(version2.replace("sr_in_w=960 sr_in_h=540","sr_in_w=1920 sr_in_h=1080"),10000).hasUpscaling(),"same-size pass cannot claim SR in protocol2");
        check(RuntimeMetrics.parse(line.replace("v=1","v=2"),0)==null,"protocol2 without SR evidence rejected");
        check(!RuntimeMetrics.parse(version2,20000).fresh(20000),"reading an old buffered native log cannot make it appear live again");
        check(RuntimeMetrics.parse(line.replace("v=1","v=9"),0)==null,"unknown protocol rejected");
        check(RuntimeMetrics.parse(line.replace("source_fps=30.0","source_fps=NaN"),0)==null,"NaN rejected");
        check(RuntimeMetrics.parse(line.replace("generated_fps=28.0","generated_fps=-1"),0)==null,"negative rate rejected");
        check(RuntimeMetrics.parse(line.replace("in_w=1920","in_w=0"),0)==null,"invalid image size rejected");
        check(RuntimeMetrics.parse("THOR_METRICS v=1 pid=1234",0)==null,"truncated logs rejected safely");
        check(RuntimeMetrics.parse("COUNTERS sources=60",0)==null,"unrelated native logs ignored");
        System.out.println("RESULT checks="+checks+" failures=0");
    }
}
