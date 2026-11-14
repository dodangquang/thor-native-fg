package com.thor.nativefg;

public class RuntimeMetricsTest {
    private static int checks;
    private static void check(boolean condition,String label){checks++;if(!condition)throw new AssertionError(label);System.out.println("PASS "+label);}
    public static void main(String[] args){
        String line="I/ThorNativeLayer( 1234): THOR_METRICS v=1 pid=1234 swap=1 eligible=1 in_w=1920 in_h=1080 out_w=1920 out_h=1080 source_fps=30.0 generated_fps=28.0 flags=0";
        RuntimeMetrics sameSize=RuntimeMetrics.parse(line,10000);
        check(sameSize!=null&&sameSize.pid==1234,"parse native protocol including logcat prefix");
        check(!sameSize.hasUpscaling()&&sameSize.detail().contains("chưa upscale"),"same-size filtering never claims SR active");
        check(sameSize.hud().equals("Gốc 30 | FG ≈58 | SR TẮT")&&!sameSize.hud().contains("\n"),"compact horizontal HUD shows source rate, FG rate, and SR state");
        check(sameSize.fresh(13499)&&!sameSize.fresh(13500)&&!sameSize.fresh(9000),"stale or reversed-clock telemetry cannot appear live");
        RuntimeMetrics upscale=RuntimeMetrics.parse(line.replace("in_w=1920 in_h=1080","in_w=960 in_h=540"),10000);
        check(upscale.hasUpscaling()&&upscale.detail().contains("960×540 → 1920×1080")&&upscale.hud().equals("Gốc 30 | FG ≈58 | GSR1 960×540→1920×1080"),"actual dimension increase identifies SR in horizontal HUD");
        RuntimeMetrics cut=RuntimeMetrics.parse(line.replace("generated_fps=28.0 flags=0","generated_fps=0 flags=3"),10000);
        check(cut.hud().equals("Gốc 30 | FG ≈30 | SR TẮT"),"zero generated rate during rejected cut does not inflate output rate");
        RuntimeMetrics unsupported=RuntimeMetrics.parse(line.replace("eligible=1","eligible=0"),10000);
        check(unsupported.hud().equals("Gốc 30 | FG — | SR TẮT"),"unsupported swapchain never claims active FG");
        String version2=line.replace("v=1","v=2")+" sr=1 sr_in_w=960 sr_in_h=540 uptime_ms=10000";
        RuntimeMetrics separate=RuntimeMetrics.parse(version2,10000);
        check(separate!=null&&separate.hasUpscaling()&&separate.inputWidth==1920&&separate.detail().contains("GSR1 ON")&&separate.hud().equals("Gốc 30 | FG ≈58 | GSR1 960×540→1920×1080"),"SR source dimensions are separate from full-resolution FG input");
        check(!RuntimeMetrics.parse(version2.replace("sr=1","sr=0"),10000).hasUpscaling(),"old SR dimensions cannot keep SR ON on a frame without an upscale pass");
        RuntimeMetrics neural=RuntimeMetrics.parse(version2+" sa=1",10000);
        check(neural.detail().contains("Neural 2D ON")&&!neural.detail().contains("GSR1"),"actual neural SR is identified independently of requested settings");
        check(neural.hud().equals("Gốc 30 | FG ≈58 | Neural 960×540→1920×1080"),"neural algorithm and resolution appear in horizontal HUD");
        check(RuntimeMetrics.parse(version2.replace("sr=1","sr=0")+" sa=1",10000).detail().equals("SR: chưa upscale"),"cached neural model cannot claim SR without an actual upscale pass");
        check(RuntimeMetrics.parse(version2+" sa=9",10000)==null,"unknown SR algorithm never silently claims GSR1");
        RuntimeMetrics spatial9=RuntimeMetrics.parse(version2+" sa=4",10000);
        check(spatial9!=null&&spatial9.detail().equals("Spatial 9-tap ON · 960×540 → 1920×1080"),"spatial 9-tap reports the new algorithm exactly");
        check(RuntimeMetrics.parse(version2+" sa=3",10000).detail().contains("thử nghiệm cũ")&&!RuntimeMetrics.parse(version2+" sa=3",10000).hud().contains("Spatial9"),"old runtimes never claim the new upscaler");
        check(spatial9.hud().contains("Spatial9 960×540→1920×1080"),"spatial 9-tap reports actual output dimensions");
        RuntimeMetrics bounded=RuntimeMetrics.parse(version2+" sa=2",10000);
        check(bounded!=null&&bounded.detail().contains("giảm viền ON"),"bounded neural has its own actual algorithm identifier");
        check(bounded.hud().equals("Gốc 30 | FG ≈58 | Neural2D 960×540→1920×1080"),"bounded neural shows Neural2D and resolution in horizontal HUD");
        check(sameSize.hud("SGSR2 853×480→1280×720").equals("Gốc 30 | FG ≈58 | SGSR2 853×480→1280×720"),"external SGSR2 resolution is displayed in horizontal HUD when layer SR is off");
        check(!RuntimeMetrics.parse(version2.replace("sr=1","sr=0")+" sa=2",10000).hasUpscaling(),"bounded model does not claim a missing upscale pass");
        check(!bounded.fresh(20000),"old bounded telemetry cannot appear live");
        check(!RuntimeMetrics.parse(version2.replace("sr_in_w=960 sr_in_h=540","sr_in_w=1920 sr_in_h=1080"),10000).hasUpscaling(),"same-size pass cannot claim SR in protocol2");
        check(RuntimeMetrics.parse(line.replace("v=1","v=2"),0)==null,"protocol2 without SR evidence rejected");
        check(!RuntimeMetrics.parse(version2,20000).fresh(20000),"reading an old buffered native log cannot make it appear live again");
        check(RuntimeMetrics.parse(version2+" display_valid=1 display_fps=59.5",10000).hud().equals("Gốc 30 | FG 60 | GSR1 960×540→1920×1080"),"Vulkan presentation timestamp rate replaces the request estimate when valid");
        check(RuntimeMetrics.parse(version2+" display_valid=0 display_fps=120",10000).hud().equals("Gốc 30 | FG ≈58 | GSR1 960×540→1920×1080"),"an invalid display measurement never inflates the HUD");
        check(RuntimeMetrics.parse(line.replace("v=1","v=9"),0)==null,"unknown protocol rejected");
        check(RuntimeMetrics.parse(line.replace("source_fps=30.0","source_fps=NaN"),0)==null,"NaN rejected");
        check(RuntimeMetrics.parse(line.replace("generated_fps=28.0","generated_fps=-1"),0)==null,"negative rate rejected");
        check(RuntimeMetrics.parse(line.replace("in_w=1920","in_w=0"),0)==null,"invalid image size rejected");
        check(RuntimeMetrics.parse("THOR_METRICS v=1 pid=1234",0)==null,"truncated logs rejected safely");
        check(RuntimeMetrics.parse("COUNTERS sources=60",0)==null,"unrelated native logs ignored");
        check(RuntimeMetrics.parse(version2+" triple=0",10000).hud().equals("Gốc 30 | FG ≈58 | GSR1 960×540→1920×1080"),"2x telemetry keeps the existing horizontal HUD");
        RuntimeMetrics triple=RuntimeMetrics.parse(version2+" triple=1",10000);
        check(triple!=null&&triple.factor==3&&triple.hud().equals("Gốc 30 | FG ≈58 · 3× | GSR1 960×540→1920×1080"),"3x telemetry is reported in the horizontal HUD");
        check(RuntimeMetrics.parse(version2,10000).factor==2,"frame generation factor is2x unless native telemetry proves3x");
        check(!RuntimeMetrics.parse(version2.replace("eligible=1","eligible=0")+" triple=1",10000).hud().contains("3×"),"an ineligible swapchain never advertises3x");
        check(RuntimeMetrics.parse(version2+" triple=7",10000)==null,"unknown frame generation factor rejected");
        check(RuntimeMetrics.parse(line+" triple=1",10000).factor==2,"protocol1 cannot claim3x");
        System.out.println("RESULT checks="+checks+" failures=0");
    }
}
