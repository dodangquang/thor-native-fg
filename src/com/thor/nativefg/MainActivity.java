package com.thor.nativefg;

import android.app.*;
import android.content.*;
import android.content.pm.*;
import android.graphics.Color;
import android.os.*;
import android.net.Uri;
import android.provider.Settings;
import android.view.*;
import android.widget.*;
import org.json.JSONObject;
import java.io.*;
import java.util.*;
import java.util.concurrent.Executors;

public class MainActivity extends Activity {
    private TextView status;
    private Spinner packages;
    private CheckBox upscale,neural;
    private Switch automatic;
    private boolean syncingAuto=false;
    private boolean syncingSr=false;
    private boolean syncingModel=false;
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
        page.addView(text("Frame gen + SR độc lập • Bản thử nghiệm 0.10",16));
        page.addView(text("Không phải NVIDIA DLSS. Chưa xác nhận tương thích mọi giả lập hay tăng FPS thực tế. Layer hỗ trợ thử nghiệm Vulkan FIFO, SDR RGBA/BGRA8; OpenGL ES và driver nạp riêng có thể không đi qua layer.",15));
        page.addView(text("Engine FG thuộc Thor Native FG. Khi thử, tắt FG tích hợp trong giả lập để tránh nội suy hai lần. GSR 1 xử lý ảnh render thấp khi đường hiển thị Vulkan phù hợp; hãy đặt độ phân giải thấp trong game/giả lập. Chỉ báo ON khi có pass upscale được gửi thật.",15));
        button(page,"Chạy cảnh mẫu Vulkan · GSR 1 + FG 2x",()->startActivity(new Intent(this,DemoActivity.class)));
        upscale=new CheckBox(this);upscale.setText("Upscale khi game render độ phân giải thấp");upscale.setTextColor(Color.rgb(223,233,246));upscale.setChecked(getSharedPreferences("thor",MODE_PRIVATE).getBoolean("upscale",true));page.addView(upscale);
        upscale.setOnCheckedChangeListener((view,checked)->{if(syncingSr)return;if(AutoMode.requested(this)){runJob(()->AutoMode.configure(this,true,checked)+"\nĐổi SR: mở lại giả lập nếu các lệnh GPU cũ vẫn được dùng.");}else AutoMode.preferences(this).edit().putBoolean("upscale",checked).apply();});
        neural=new CheckBox(this);neural.setText("Neural SR cho nét vẽ / game 2D (thử nghiệm)");neural.setTextColor(Color.rgb(223,233,246));neural.setChecked(AutoMode.preferences(this).getBoolean("neural_sr",false));page.addView(neural);
        page.addView(text("Bỏ chọn để dùng GSR1. Neural dùng mạng Anime4K CNN S, chưa xác nhận chất lượng cho mọi game 3D. Sau khi đổi, lưu game rồi mở lại giả lập. App báo thuật toán đã chạy trong phần trạng thái.",14));
        neural.setOnCheckedChangeListener((view,checked)->{if(syncingModel)return;runJob(()->AutoMode.setNeural(this,checked));});
        automatic=new Switch(this);automatic.setText("Tự bật FG khi mở game / giả lập");automatic.setTextColor(Color.rgb(223,233,246));automatic.setChecked(AutoMode.requested(this));page.addView(automatic);
        automatic.setOnCheckedChangeListener((view,checked)->{if(syncingAuto)return;if(busy){syncAutoSwitch();return;}final boolean sr=upscale.isChecked();runJob(()->{String result=AutoMode.configure(this,checked,sr);if(checked)runOnUiThread(()->showOverlay());return result;});});
        button(page,"Chọn game / giả lập tự động",()->editAutoApplications());
        page.addView(text("Tự động nhận diện các giả lập quen thuộc và ứng dụng khai báo là game. Có thể thêm/loại app trong danh sách. Chế độ này cần bootstrap 0.2; ứng dụng đang chạy cần thoát/mở lại một lần. Không tự đóng game.",14));
        page.addView(text("Chọn ứng dụng khi bật thủ công",17));
        packages=new Spinner(this);loadPackages();page.addView(packages);
        button(page,"Bật FG của Thor và mở ứng dụng",()->enableSelected());
        button(page,"Hiện FPS gốc / FG",()->showOverlay());
        button(page,"Ẩn ô FPS (giữ cấu hình FG)",()->{getSharedPreferences("thor",MODE_PRIVATE).edit().putBoolean("hud_visible",false).apply();stopService(new Intent(this,FpsOverlayService.class));});
        button(page,"Tắt layer · khôi phục cấu hình trước",()->runJob(()->{restore();return "Đã khôi phục cấu hình. Đóng/mở lại giả lập để gỡ layer khỏi tiến trình.";}));
        button(page,"Đọc trạng thái và log layer",()->runJob(()->readStatus()));
        status=text("Chọn ứng dụng → bật FG của Thor → cấp root → lưu game, thoát hẳn rồi mở lại giả lập và chọn Vulkan. Cấu hình thành công chưa chứng minh FG/SR đã chạy.\n\nÔ FPS chỉ hiện ‘Gốc’ và ‘FG’. Gốc là tốc độ ứng dụng gửi khung; FG là FPS sau xử lý, dùng timestamp trình bày Vulkan khi có. Dấu ≈ cho biết số ước tính từ yêu cầu gửi khung. Thiếu dữ liệu mới thì ô chuyển sang chờ. Kéo để di chuyển; chạm để xem ý nghĩa số đo.\n\nBản 0.3 thử GSR 1 ngay ở bước phóng ảnh Vulkan cuối: ví dụ game render 960×540, xuất 1920×1080. App không tự đổi độ phân giải game. Đường hiển thị khác hoặc ảnh đã bằng cỡ màn hình sẽ báo ‘chưa upscale’. Đây là spatial SR, chưa phải DLSS/temporal SR. Khi đổi SR hoặc tắt layer, mở lại giả lập để bỏ các lệnh GPU đã ghi trước đó.\n\nFG có loại bỏ cặp cut/chuyển động kém tin cậy. NPU, GSR 2, FSR 3 và hệ số 3x chưa được tích hợp.",14);page.addView(status);
        String previous=getSharedPreferences("thor",MODE_PRIVATE).getString("selected_package","");for(int i=0;i<ids.size();i++)if(ids.get(i).equals(previous)){packages.setSelection(i);break;}
        if(getIntent().getBooleanExtra("demo",false)){Intent demo=new Intent(this,DemoActivity.class);demo.putExtra("mode",getIntent().getIntExtra("mode",2));startActivity(demo);}
        if(getIntent().getBooleanExtra("hud",false)||(AutoMode.requested(this)&&AutoMode.preferences(this).getBoolean("hud_visible",false)&&Settings.canDrawOverlays(this)))showOverlay();
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
    private boolean isEmulator(String id){return GameRules.isEmulator(id);}
    private void syncAutoSwitch(){syncingAuto=true;automatic.setChecked(AutoMode.requested(this));syncingAuto=false;}
    private void editAutoApplications(){
        List<AutoMode.Entry> entries=AutoMode.applications(this);String[] labels=new String[entries.size()];boolean[] selected=new boolean[entries.size()];
        for(int i=0;i<entries.size();i++){AutoMode.Entry entry=entries.get(i);labels[i]=entry.label+"\n"+entry.id;selected[i]=entry.selected;}
        new AlertDialog.Builder(this).setTitle("Tự bật FG cho ứng dụng đã chọn")
            .setMultiChoiceItems(labels,selected,(dialog,which,checked)->entries.get(which).selected=checked)
            .setNegativeButton("Hủy",null).setPositiveButton("Lưu",(dialog,which)->{AutoMode.saveSelection(this,entries);if(AutoMode.requested(this)){final boolean sr=upscale.isChecked();runJob(()->AutoMode.configure(this,true,sr));}else status.setText("Đã lưu danh sách. Bật công tắc tự động để sử dụng.");}).show();
    }
    private void showOverlay(){
        if(!Settings.canDrawOverlays(this)){
            status.setText("Bật ‘Cho phép hiển thị trên ứng dụng khác’ cho Thor Native FG để hiện ô FPS nhỏ. Sau đó quay lại ứng dụng.");
            startActivityForResult(new Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION,Uri.parse("package:"+getPackageName())),104);return;
        }
        if(Build.VERSION.SDK_INT>=33&&checkSelfPermission("android.permission.POST_NOTIFICATIONS")!=PackageManager.PERMISSION_GRANTED)requestPermissions(new String[]{"android.permission.POST_NOTIFICATIONS"},105);
        getSharedPreferences("thor",MODE_PRIVATE).edit().putBoolean("hud_visible",true).apply();startForegroundService(new Intent(this,FpsOverlayService.class));
    }
    @Override protected void onActivityResult(int request,int result,Intent data){super.onActivityResult(request,result,data);if(request==104&&Settings.canDrawOverlays(this))showOverlay();}
    private String root(String script)throws Exception{
        return RootShell.run(script);
    }
    private File backup(){return new File(getFilesDir(),"settings-before.json");}
    private void saveBackup()throws Exception{
        if(backup().exists())return;JSONObject old=new JSONObject();for(String key:keys)old.put(key,root("settings get global "+key));old.put("thor_enabled",root("getprop debug.thorfg.enabled"));old.put("thor_target",root("getprop debug.thorfg.target"));old.put("thor_sr",root("getprop debug.thorfg.sr"));
        try(FileOutputStream f=new FileOutputStream(backup())){f.write(old.toString().getBytes("UTF-8"));f.getFD().sync();}
    }
    private void enableSelected(){
        if(ids.isEmpty())return;final String id=ids.get(packages.getSelectedItemPosition());
        final boolean sr=upscale.isChecked();getSharedPreferences("thor",MODE_PRIVATE).edit().putBoolean("upscale",sr).apply();
        getSharedPreferences("thor",MODE_PRIVATE).edit().putString("selected_package",id).apply();
        runJob(()->{
            if(!id.matches("[A-Za-z0-9_.]+"))throw new IOException("Invalid package");
            if(!root("id -u").equals("0"))throw new IOException("Chưa được cấp root");
            ApplicationInfo app=getPackageManager().getApplicationInfo(id,0);boolean debuggable=(app.flags&ApplicationInfo.FLAG_DEBUGGABLE)!=0;
            boolean module="yes".equals(root("if [ -f "+MODULE+"/module.prop ] && [ ! -f "+MODULE+"/disable ]; then echo yes; fi"));
            if(!debuggable&&!module)throw new IOException("Ứng dụng release này cần module Zygisk Thor Native FG và một lần khởi động lại. Chưa thể bật chỉ bằng quyền su. Module nằm tại E:\\ThorNative\\ThorNativeFG-Zygisk-experimental.zip trên PC.");
            saveBackup();
            RuntimeProvider.ensure(this);
            root("setprop persist.thorfg.sr_model "+AutoMode.model(this));
            if(AutoMode.requested(this))AutoMode.configure(this,false,sr);
            if(id.length()>90)throw new IOException("Package name too long for Android property");
            if(module)root("setprop debug.thorfg.target "+id);
            root("settings put global enable_gpu_debug_layers 1 && settings put global gpu_debug_app "+id+" && settings put global gpu_debug_layers VK_LAYER_THOR_native_fg && settings put global gpu_debug_layer_app "+RuntimeBuild.PACKAGE+" && setprop debug.thorfg.sr "+(sr?"1":"0")+" && setprop debug.thorfg.enabled 1");
            Intent launch=getPackageManager().getLaunchIntentForPackage(id);
            // Do not force-stop an emulator from the UI: the user may have unsaved gameplay.
            runOnUiThread(()->{if(Settings.canDrawOverlays(this))showOverlay();if(launch!=null)startActivity(launch);});
            return "Đã cấu hình cho "+id+". Nếu giả lập đang chạy, hãy lưu game, thoát hẳn và mở lại. Chọn Vulkan trong giả lập. Bấm đọc log để xác nhận layer thực sự nạp; cấu hình thành công chưa chứng minh FG hoạt động.";
        });
    }
    private void restore()throws Exception{
        AutoMode.configure(this,false,upscale.isChecked());
        root("setprop debug.thorfg.enabled 0; setprop debug.thorfg.sr 0; setprop debug.thorfg.target ''");
        if(!backup().exists())return;
        ByteArrayOutputStream bytes=new ByteArrayOutputStream();try(FileInputStream in=new FileInputStream(backup())){byte[] b=new byte[4096];int n;while((n=in.read(b))>0)bytes.write(b,0,n);}
        JSONObject old=new JSONObject(bytes.toString("UTF-8"));
        for(String key:keys){String value=old.getString(key);if(value.equals("null")||value.isEmpty())root("settings delete global "+key);else root("settings put global "+key+" '"+value.replace("'","'\\''")+"'");}
        String target=old.optString("thor_target","");if(target.matches("[A-Za-z0-9_.]+")&&target.length()<91)root("setprop debug.thorfg.target "+target);
        String property=old.optString("thor_enabled","");if(property.equals("1"))root("setprop debug.thorfg.enabled 1");if(!backup().delete())throw new IOException("Cannot remove restored backup");
        root("setprop debug.thorfg.sr "+("1".equals(old.optString("thor_sr",""))?"1":"0"));
    }
    private String readStatus()throws Exception{
        boolean auto=AutoMode.requested(this);String autoStatus=auto?"Tự động: đã yêu cầu; bootstrap đang chạy="+root("getprop sys.thorfg.bootstrap")+" (cần 2 trở lên).\n":"";
        String target=auto?root("game_target=$("+AutoMode.foregroundScript()+"); if "+AutoMode.allowedScript()+"; then printf %s \"$game_target\"; fi"):root("settings get global gpu_debug_app");
        if(auto&&target.isEmpty()){
            String last=AutoMode.preferences(this).getString("auto_last_package","");
            if(GameRules.validPackage(last)){target=root("game_target="+RootShell.quote(last)+"; if "+AutoMode.allowedScript()+"; then printf %s \"$game_target\"; fi");if(!target.isEmpty())autoStatus+="Ứng dụng gần nhất (có thể đang ở nền): "+target+"\n";}
        }
        String config=autoStatus+target+"\n"+root("getprop "+(auto?"debug.thorfg.auto":"debug.thorfg.enabled"));
        if(!target.matches("[A-Za-z0-9_.]+")||target.equals("null"))return autoStatus+(auto?"Đang chờ game/giả lập trong danh sách ở trước.":"Chưa chọn ứng dụng cho layer.");
        String pid=root("pidof -s "+target+" || true");
        if(!pid.matches("[0-9]+"))return "Target: "+target+"\nChưa có tiến trình đang chạy. Mở lại giả lập rồi kiểm tra.";
        String logs=root("logcat -d --pid="+pid+" -t 3000 -s ThorNativeBootstrap:I ThorNativeLayer:I ThorNativeDemo:I '*:S' | tail -c 18000");
        if(logs.length()>18000)logs=logs.substring(logs.length()-18000);
        RuntimeMetrics latest=null;for(String line:logs.split("\\r?\\n")){RuntimeMetrics value=RuntimeMetrics.parse(line,SystemClock.elapsedRealtime());if(value!=null&&value.pid==Integer.parseInt(pid))latest=value;}
        String summary=latest!=null&&latest.fresh(SystemClock.elapsedRealtime())?latest.hud()+"\n"+latest.detail():"Chờ dữ liệu FG/SR mới của tiến trình này.";
        return summary+"\n\nTarget / active / PID:\n"+config+"\n"+pid+"\n\n"+logs+"\n\nNếu chưa có INSTANCE/DEVICE/SWAPCHAIN thì chưa xác nhận layer hoạt động. COUNTERS là số yêu cầu present; không phải FPS hiển thị hay độ trễ đã đo.";
    }
    interface Work {String run()throws Exception;}
    private void runJob(Work work){busy=true;upscale.setEnabled(false);neural.setEnabled(false);automatic.setEnabled(false);status.setText("Đang xử lý… Nếu Magisk hỏi, cần cấp quyền root để thay cấu hình layer.");worker.execute(()->{String result;try{result=work.run();}catch(Exception e){result="Lỗi: "+e.getMessage();}final String value=result;runOnUiThread(()->{busy=false;syncAutoSwitch();syncingSr=true;upscale.setChecked(AutoMode.preferences(this).getBoolean("upscale",true));syncingSr=false;syncingModel=true;neural.setChecked(AutoMode.preferences(this).getBoolean("neural_sr",false));syncingModel=false;upscale.setEnabled(true);neural.setEnabled(true);automatic.setEnabled(true);status.setText(value);});});}
}
