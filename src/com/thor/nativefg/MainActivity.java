package com.thor.nativefg;

import android.app.*;
import android.content.*;
import android.content.pm.*;
import android.graphics.Color;
import android.os.*;
import android.view.*;
import android.widget.*;
import org.json.JSONObject;
import java.io.*;
import java.util.*;
import java.util.concurrent.Executors;

public class MainActivity extends Activity {
    private TextView status;
    private Spinner packages;
    private final ArrayList<String> ids=new ArrayList<>();
    private final java.util.concurrent.ExecutorService worker=Executors.newSingleThreadExecutor();
    private final String[] keys={"enable_gpu_debug_layers","gpu_debug_app","gpu_debug_layers","gpu_debug_layer_app"};
    private static final String MODULE="/data/adb/modules/thor_native_fg_bootstrap";
    private boolean busy=false;
    @Override public void onCreate(Bundle state){
        super.onCreate(state);
        LinearLayout page=new LinearLayout(this);page.setOrientation(1);page.setPadding(36,28,36,24);page.setBackgroundColor(Color.rgb(12,20,33));
        ScrollView scroll=new ScrollView(this);scroll.addView(page);setContentView(scroll);
        TextView title=text("THOR NATIVE FG",26);title.setTextColor(Color.rgb(87,222,212));page.addView(title);
        page.addView(text("GSR 1 + nội suy chuyển động GPU • Bản thử nghiệm 0.1",16));
        page.addView(text("Không phải NVIDIA DLSS. Chưa xác nhận tương thích mọi giả lập hay tăng FPS thực tế. Layer hỗ trợ thử nghiệm Vulkan FIFO, SDR RGBA/BGRA8; OpenGL ES và driver nạp riêng có thể không đi qua layer.",15));
        page.addView(text("Eden Nightly / Winlator có FG tích hợp: ưu tiên dùng tính năng đó. Tránh bật thêm layer nếu FG bên trong giả lập đang bật.",15));
        button(page,"Chạy cảnh mẫu Vulkan · GSR 1 + FG 2x",()->startActivity(new Intent(this,DemoActivity.class)));
        page.addView(text("Chọn giả lập / ứng dụng (mỗi lần một tiến trình)",17));
        packages=new Spinner(this);loadPackages();page.addView(packages);
        button(page,"Bật layer thử nghiệm và mở ứng dụng",()->enableSelected());
        button(page,"Tắt layer · khôi phục cấu hình trước",()->runJob(()->{restore();return "Đã khôi phục cấu hình. Đóng/mở lại giả lập để gỡ layer khỏi tiến trình.";}));
        button(page,"Đọc trạng thái và log layer",()->runJob(()->readStatus()));
        status=text("Chưa bật layer. Cảnh mẫu chạy độc lập, không cần root và không quay màn hình.\n\nNút bật layer yêu cầu quyền root Magisk. Layer chỉ hoạt động khi renderer thực sự nạp nó; log INSTANCE/DEVICE/SWAPCHAIN/COUNTERS là bằng chứng từng bước.\n\nTrong layer 0.1, GSR 1 lọc ảnh ở kích thước swapchain gốc; chưa hạ độ phân giải render của game. Cảnh mẫu có upscale thật từ 640×360 lên kích thước màn hình. FG dùng block matching RGB, có thể méo HUD/che khuất/chuyển cảnh.\n\nNPU, GSR 2, FSR 3 và hệ số 3x chưa được tích hợp.",14);page.addView(status);
        if(getIntent().getBooleanExtra("demo",false)){Intent demo=new Intent(this,DemoActivity.class);demo.putExtra("mode",getIntent().getIntExtra("mode",2));startActivity(demo);}
    }
    private TextView text(String value,int size){TextView v=new TextView(this);v.setText(value);v.setTextSize(size);v.setTextColor(Color.rgb(223,233,246));v.setPadding(0,10,0,16);return v;}
    private void button(LinearLayout page,String label,Runnable action){Button b=new Button(this);b.setText(label);page.addView(b);b.setOnClickListener(v->{if(!busy)action.run();});}
    private void loadPackages(){
        Intent intent=new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LAUNCHER);
        List<ResolveInfo> list=getPackageManager().queryIntentActivities(intent,0);Map<String,String> unique=new TreeMap<>();
        for(ResolveInfo r:list){String id=r.activityInfo.packageName;if(!id.equals(getPackageName()))unique.put(id,r.loadLabel(getPackageManager()).toString());}
        ArrayList<String> sorted=new ArrayList<>(unique.keySet());
        sorted.sort((a,b)->{boolean ea=isEmulator(a),eb=isEmulator(b);return ea==eb?unique.get(a).compareToIgnoreCase(unique.get(b)):(ea?-1:1);});
        ArrayList<String> labels=new ArrayList<>();for(String id:sorted){ids.add(id);labels.add((isEmulator(id)?"★ ":"")+unique.get(id)+"\n"+id);}
        ArrayAdapter<String> adapter=new ArrayAdapter<>(this,android.R.layout.simple_spinner_dropdown_item,labels);packages.setAdapter(adapter);
    }
    private boolean isEmulator(String id){String x=id.toLowerCase(Locale.ROOT);for(String s:new String[]{"eden","winlator","ppsspp","nethersx","aethersx","dolphin","retroarch","citron","yuzu","sudachi","citra","azahar","lime3ds","duckstation","mupen","redream","flycast","vita3k","cosmic","suyu","strato","skyline","melonds","drastic","uzuy","cemi"})if(x.contains(s))return true;return false;}
    private String root(String script)throws Exception{
        java.lang.Process p=new ProcessBuilder("su","-c",script).redirectErrorStream(true).start();StringBuilder out=new StringBuilder();
        try(BufferedReader r=new BufferedReader(new InputStreamReader(p.getInputStream()))){String line;while((line=r.readLine())!=null){out.append(line).append('\n');if(out.length()>64000)throw new IOException("Root output too large");}}
        int code=p.waitFor();if(code!=0)throw new IOException("Root exit "+code+": "+out);return out.toString().trim();
    }
    private File backup(){return new File(getFilesDir(),"settings-before.json");}
    private void saveBackup()throws Exception{
        if(backup().exists())return;JSONObject old=new JSONObject();for(String key:keys)old.put(key,root("settings get global "+key));old.put("thor_enabled",root("getprop debug.thorfg.enabled"));old.put("thor_target",root("getprop debug.thorfg.target"));
        try(FileOutputStream f=new FileOutputStream(backup())){f.write(old.toString().getBytes("UTF-8"));f.getFD().sync();}
    }
    private void enableSelected(){
        if(ids.isEmpty())return;final String id=ids.get(packages.getSelectedItemPosition());
        runJob(()->{
            if(!id.matches("[A-Za-z0-9_.]+"))throw new IOException("Invalid package");
            if(!root("id -u").equals("0"))throw new IOException("Chưa được cấp root");
            ApplicationInfo app=getPackageManager().getApplicationInfo(id,0);boolean debuggable=(app.flags&ApplicationInfo.FLAG_DEBUGGABLE)!=0;
            boolean module="yes".equals(root("if [ -f "+MODULE+"/module.prop ] && [ ! -f "+MODULE+"/disable ]; then echo yes; fi"));
            if(!debuggable&&!module)throw new IOException("Ứng dụng release này cần module Zygisk Thor Native FG và một lần khởi động lại. Chưa thể bật chỉ bằng quyền su. Module nằm tại E:\\ThorNative\\ThorNativeFG-Zygisk-experimental.zip trên PC.");
            saveBackup();
            if(id.length()>90)throw new IOException("Package name too long for Android property");
            if(module)root("setprop debug.thorfg.target "+id);
            root("settings put global enable_gpu_debug_layers 1 && settings put global gpu_debug_app "+id+" && settings put global gpu_debug_layers VK_LAYER_THOR_native_fg && settings put global gpu_debug_layer_app com.thor.nativefg && setprop debug.thorfg.enabled 1");
            Intent launch=getPackageManager().getLaunchIntentForPackage(id);
            // Do not force-stop an emulator from the UI: the user may have unsaved gameplay.
            runOnUiThread(()->{if(launch!=null)startActivity(launch);});
            return "Đã cấu hình cho "+id+". Nếu giả lập đang chạy, hãy lưu game, thoát hẳn và mở lại. Chọn Vulkan trong giả lập. Bấm đọc log để xác nhận layer thực sự nạp; cấu hình thành công chưa chứng minh FG hoạt động.";
        });
    }
    private void restore()throws Exception{
        root("setprop debug.thorfg.enabled 0; setprop debug.thorfg.target ''");
        if(!backup().exists())return;
        ByteArrayOutputStream bytes=new ByteArrayOutputStream();try(FileInputStream in=new FileInputStream(backup())){byte[] b=new byte[4096];int n;while((n=in.read(b))>0)bytes.write(b,0,n);}
        JSONObject old=new JSONObject(bytes.toString("UTF-8"));
        for(String key:keys){String value=old.getString(key);if(value.equals("null")||value.isEmpty())root("settings delete global "+key);else root("settings put global "+key+" '"+value.replace("'","'\\''")+"'");}
        String target=old.optString("thor_target","");if(target.matches("[A-Za-z0-9_.]+")&&target.length()<91)root("setprop debug.thorfg.target "+target);
        String property=old.optString("thor_enabled","");if(property.equals("1"))root("setprop debug.thorfg.enabled 1");if(!backup().delete())throw new IOException("Cannot remove restored backup");
    }
    private String readStatus()throws Exception{
        String target=root("settings get global gpu_debug_app");
        String config=target+"\n"+root("getprop debug.thorfg.enabled");
        if(!target.matches("[A-Za-z0-9_.]+")||target.equals("null"))return "Chưa chọn ứng dụng cho layer.";
        String pid=root("pidof -s "+target+" || true");
        if(!pid.matches("[0-9]+"))return "Target: "+target+"\nChưa có tiến trình đang chạy. Mở lại giả lập rồi kiểm tra.";
        String logs=root("logcat -d --pid="+pid+" -t 3000 -s ThorNativeBootstrap:I ThorNativeLayer:I ThorNativeDemo:I '*:S'");
        if(logs.length()>18000)logs=logs.substring(logs.length()-18000);
        return "Target / active / PID:\n"+config+"\n"+pid+"\n\n"+logs+"\n\nNếu chưa có INSTANCE/DEVICE/SWAPCHAIN thì chưa xác nhận layer hoạt động. COUNTERS là số yêu cầu present; không phải FPS hiển thị hay độ trễ đã đo.";
    }
    interface Work {String run()throws Exception;}
    private void runJob(Work work){busy=true;status.setText("Đang xử lý… Nếu Magisk hỏi, cần cấp quyền root để thay cấu hình layer.");worker.execute(()->{String result;try{result=work.run();}catch(Exception e){result="Lỗi: "+e.getMessage();}final String value=result;runOnUiThread(()->{busy=false;status.setText(value);});});}
}
