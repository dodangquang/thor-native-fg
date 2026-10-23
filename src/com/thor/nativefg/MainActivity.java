package com.thor.nativefg;

import android.app.*;
import android.content.*;
import android.content.pm.*;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
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
    private Button masterBtn,modeGsr2Btn,modeNeuralBtn,modeGsr1Btn,modeFgOnlyBtn;
    private Spinner packages,srModels;
    private CheckBox upscale;
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
        LinearLayout page=new LinearLayout(this);page.setOrientation(LinearLayout.VERTICAL);page.setPadding(dp(18),dp(14),dp(18),dp(18));page.setBackgroundColor(Color.rgb(12,20,33));
        ScrollView scroll=new ScrollView(this);scroll.setFillViewport(true);scroll.addView(page);setContentView(scroll);

        TextView title=text("THOR · SR + FG (1-CHẠM)",20);title.setTextColor(Color.rgb(87,222,212));title.setTypeface(Typeface.DEFAULT_BOLD);page.addView(title);
        TextView sub=text("Bù khung hình 120Hz & Nâng độ phân giải · Bản 0.14 RC1",12);sub.setTextColor(Color.rgb(160,182,208));page.addView(sub);

        // 1. Nút chính 1 chạm (BẬT / TẮT TOÀN BỘ)
        masterBtn=new Button(this);masterBtn.setAllCaps(false);masterBtn.setTextSize(16);masterBtn.setTypeface(Typeface.DEFAULT_BOLD);masterBtn.setTextColor(Color.WHITE);
        LinearLayout.LayoutParams masterLp=new LinearLayout.LayoutParams(-1,dp(54));masterLp.setMargins(0,dp(6),0,dp(10));
        page.addView(masterBtn,masterLp);
        masterBtn.setOnClickListener(v->{
            if(busy)return;
            if(AutoMode.requested(this)){
                runJob(()->{
                    restore();
                    runOnUiThread(()->{AutoMode.preferences(this).edit().putBoolean("hud_visible",false).apply();stopService(new Intent(this,FpsOverlayService.class));});
                    return "Đã TẮT toàn bộ FG + SR và ẩn thanh FPS.";
                });
            }else{
                final boolean sr=AutoMode.preferences(this).getBoolean("upscale",true);
                runJob(()->{
                    String res=AutoMode.configure(this,true,sr);
                    runOnUiThread(()->showOverlay());
                    return "Đã BẬT 1-chạm (FG 120Hz + "+(sr?AutoMode.modelLabel(this):"Tắt SR")+") và đồng bộ dữ liệu Eden.\n"+res;
                });
            }
        });

        // 2. Chọn nhanh chế độ SR (1 chạm — 4 nút ngang: GSR2, Neural 2D, GSR1, Tắt SR)
        TextView srLabel=text("Chế độ nâng nét (chạm để đổi ngay):",13);srLabel.setTextColor(Color.rgb(185,205,228));page.addView(srLabel);
        LinearLayout srRow=new LinearLayout(this);srRow.setOrientation(LinearLayout.HORIZONTAL);
        LinearLayout.LayoutParams rowLp=new LinearLayout.LayoutParams(-1,-2);rowLp.setMargins(0,0,0,dp(10));page.addView(srRow,rowLp);
        modeGsr2Btn=pillButton("GSR2 (Mới)",()->selectSrModel(true,"sgsr2"));
        modeNeuralBtn=pillButton("Neural 2D",()->selectSrModel(true,"neural2d"));
        modeGsr1Btn=pillButton("GSR1",()->selectSrModel(true,"sgsr1"));
        modeFgOnlyBtn=pillButton("Tắt SR",()->selectSrModel(false,AutoMode.model(this)));
        addWeighted(srRow,modeGsr2Btn,dp(6));
        addWeighted(srRow,modeNeuralBtn,dp(6));
        addWeighted(srRow,modeGsr1Btn,dp(6));
        addWeighted(srRow,modeFgOnlyBtn,0);

        // 3. Mở nhanh Game / Giả lập (1 chạm: tự bật + mở app)
        TextView launchLabel=text("Mở nhanh giả lập (1 chạm tự bật FG/SR & vào thẳng):",13);launchLabel.setTextColor(Color.rgb(185,205,228));page.addView(launchLabel);
        LinearLayout launchRow=new LinearLayout(this);launchRow.setOrientation(LinearLayout.HORIZONTAL);page.addView(launchRow,rowLp);
        Button edenThorBtn=actionButton("🎮 Eden Thor (SR+FG)",Color.rgb(25,92,118),()->quickLaunch("dev.eden.eden_emulator.relWithDebInfo"));
        Button edenNightlyBtn=actionButton("🎮 Eden Nightly",Color.rgb(34,68,105),()->quickLaunch("dev.eden.eden_emulator.nightly"));
        Button droidDeckBtn=actionButton("💻 DroidDeck (PC)",Color.rgb(52,62,96),()->quickLaunch("com.droiddeck.launcher"));
        addWeighted(launchRow,edenThorBtn,dp(6));
        addWeighted(launchRow,edenNightlyBtn,dp(6));
        addWeighted(launchRow,droidDeckBtn,0);

        // 4. Thanh công cụ phụ (Hiện/Ẩn FPS, Kiểm tra trạng thái, Tùy chọn nâng cao)
        LinearLayout utilRow=new LinearLayout(this);utilRow.setOrientation(LinearLayout.HORIZONTAL);page.addView(utilRow,rowLp);
        Button fpsToggleBtn=actionButton("Hiện / Ẩn FPS",Color.rgb(30,45,68),()->{
            boolean vis=AutoMode.preferences(this).getBoolean("hud_visible",false);
            if(vis){AutoMode.preferences(this).edit().putBoolean("hud_visible",false).apply();stopService(new Intent(this,FpsOverlayService.class));status.setText("Đã ẩn thanh FPS ngang.");}
            else{showOverlay();status.setText("Đã bật thanh FPS ngang: Gốc | FG | SR.");}
        });
        Button checkBtn=actionButton("Kiểm tra FG + SR",Color.rgb(30,45,68),()->runJob(()->readStatus(false)));
        LinearLayout advanced=new LinearLayout(this);advanced.setOrientation(LinearLayout.VERTICAL);advanced.setVisibility(View.GONE);
        Button moreBtn=actionButton("Tùy chọn khác ▾",Color.rgb(30,45,68),null);
        moreBtn.setOnClickListener(v->{boolean open=advanced.getVisibility()!=View.VISIBLE;advanced.setVisibility(open?View.VISIBLE:View.GONE);moreBtn.setText(open?"Thu gọn ▴":"Tùy chọn khác ▾");});
        addWeighted(utilRow,fpsToggleBtn,dp(6));
        addWeighted(utilRow,checkBtn,dp(6));
        addWeighted(utilRow,moreBtn,0);

        // Ô hiển thị trạng thái ngắn gọn
        status=text("Bấm nút lớn ở trên hoặc chạm thẳng vào giả lập muốn chơi. Bản Eden Thor dùng chung toàn bộ save game, driver và danh sách game với Eden Nightly.",13);
        GradientDrawable statusBg=new GradientDrawable();statusBg.setColor(Color.rgb(18,30,48));statusBg.setCornerRadius(dp(8));
        status.setBackground(statusBg);status.setPadding(dp(12),dp(10),dp(12),dp(10));
        page.addView(status,rowLp);

        // Khu vực nâng cao (ẩn mặc định)
        page.addView(advanced);
        advanced.addView(text("Chọn ứng dụng bất kỳ để mở cùng FG + SR:",13));
        packages=new Spinner(this);loadPackages();advanced.addView(packages);
        upscale=new CheckBox(this);upscale.setText("Bật upscale từ ảnh render thấp");upscale.setTextColor(Color.rgb(223,233,246));upscale.setChecked(AutoMode.preferences(this).getBoolean("upscale",true));advanced.addView(upscale);
        upscale.setOnCheckedChangeListener((view,checked)->{if(!syncingSr)selectSrModel(checked,AutoMode.model(this));});
        srModels=new Spinner(this);
        srModels.setAdapter(new ArrayAdapter<String>(this,android.R.layout.simple_spinner_dropdown_item,new String[]{"GSR 2 · Temporal 9-tap (Khuyên dùng)","Neural 2D · giảm viền","GSR 1 · lọc không gian"}));
        String curModel=AutoMode.model(this);
        srModels.setSelection("neural2d".equals(curModel)?1:("sgsr1".equals(curModel)?2:0));advanced.addView(srModels);
        srModels.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener(){
            public void onNothingSelected(AdapterView<?> parent){}
            public void onItemSelected(AdapterView<?> parent,View view,int position,long id){
                if(!syncingModel&&!busy)selectSrModel(AutoMode.preferences(MainActivity.this).getBoolean("upscale",true),position==1?"neural2d":(position==2?"sgsr1":"sgsr2"));
            }
        });
        automatic=new Switch(this);automatic.setText("Tự bật khi mở game");automatic.setTextColor(Color.rgb(223,233,246));automatic.setChecked(AutoMode.requested(this));advanced.addView(automatic);
        automatic.setOnCheckedChangeListener((view,checked)->{if(syncingAuto||busy)return;final boolean sr=AutoMode.preferences(this).getBoolean("upscale",true);runJob(()->{String result=AutoMode.configure(this,checked,sr);if(checked)runOnUiThread(()->showOverlay());return result;});});
        button(advanced,"Bật & Mở ứng dụng đang chọn ở danh sách trên",()->enableSelected());
        button(advanced,"Danh sách game tự động",()->editAutoApplications());
        button(advanced,"Đồng bộ lại Save + Driver Eden Nightly → Eden Thor",()->runJob(()->{RootShell.runMaster(AutoMode.syncEdenScript());return "Đã đồng bộ chung thư mục gốc (save game, driver, cấu hình, danh sách game) từ Eden Nightly sang Eden Thor.";}));
        button(advanced,"DroidDeck · dùng SR của Thor (GSR1/Neural 2D)",()->runJob(()->DroidDeckPreset.change(this,false)));
        button(advanced,"DroidDeck · dùng SGSR2 trong game (DLSS/FSR2)",()->runJob(()->DroidDeckPreset.change(this,true)));
        button(advanced,"Xem log kỹ thuật",()->runJob(()->readStatus(true)));
        button(advanced,"Chạy cảnh mẫu GSR 1 + FG",()->startActivity(new Intent(this,DemoActivity.class)));

        String previous=AutoMode.preferences(this).getString("selected_package","");for(int i=0;i<ids.size();i++)if(ids.get(i).equals(previous)){packages.setSelection(i);break;}
        refreshUiState();
        if(getIntent().getBooleanExtra("demo",false)){Intent demo=new Intent(this,DemoActivity.class);demo.putExtra("mode",getIntent().getIntExtra("mode",2));startActivity(demo);}
        if(getIntent().getBooleanExtra("hud",false)||(AutoMode.requested(this)&&AutoMode.preferences(this).getBoolean("hud_visible",false)&&Settings.canDrawOverlays(this)))showOverlay();
    }

    private int dp(int v){return Math.round(v*getResources().getDisplayMetrics().density);}
    private TextView text(String value,int size){TextView v=new TextView(this);v.setText(value);v.setTextSize(size);v.setTextColor(Color.rgb(223,233,246));v.setPadding(0,dp(3),0,dp(5));return v;}
    private void button(LinearLayout page,String label,Runnable action){Button b=new Button(this);b.setText(label);b.setAllCaps(false);b.setTextSize(13);page.addView(b);b.setOnClickListener(v->{if(!busy)action.run();});}
    private Button pillButton(String label,Runnable action){
        Button b=new Button(this);b.setText(label);b.setAllCaps(false);b.setTextSize(13);b.setTextColor(Color.WHITE);
        b.setOnClickListener(v->{if(!busy)action.run();});return b;
    }
    private Button actionButton(String label,int bgColor,Runnable action){
        Button b=new Button(this);b.setText(label);b.setAllCaps(false);b.setTextSize(13);b.setTextColor(Color.WHITE);
        GradientDrawable bg=new GradientDrawable();bg.setColor(bgColor);bg.setCornerRadius(dp(8));b.setBackground(bg);
        if(action!=null)b.setOnClickListener(v->{if(!busy)action.run();});
        return b;
    }
    private void addWeighted(LinearLayout row,View child,int rightMargin){
        LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(0,dp(44),1f);lp.setMargins(0,0,rightMargin,0);row.addView(child,lp);
    }
    private void stylePill(Button btn,boolean active){
        GradientDrawable bg=new GradientDrawable();
        bg.setColor(active?Color.rgb(22,126,118):Color.rgb(26,38,56));
        bg.setCornerRadius(dp(8));
        bg.setStroke(dp(1),active?Color.rgb(87,222,212):Color.rgb(48,66,92));
        btn.setBackground(bg);
        btn.setTypeface(active?Typeface.DEFAULT_BOLD:Typeface.DEFAULT);
    }
    private void refreshUiState(){
        boolean on=AutoMode.requested(this);
        boolean sr=AutoMode.preferences(this).getBoolean("upscale",true);
        String m=AutoMode.model(this);
        GradientDrawable mBg=new GradientDrawable();
        mBg.setColor(on?Color.rgb(142,43,43):Color.rgb(20,126,110));
        mBg.setCornerRadius(dp(10));
        masterBtn.setBackground(mBg);
        masterBtn.setText(on?"⏹ ĐANG BẬT (FG 120Hz + "+(sr?AutoMode.modelLabel(this):"Tắt SR")+") · CHẠM ĐỂ TẮT":"⚡ 1 CHẠM: BẬT NGAY (FG 120Hz + "+AutoMode.modelLabel(this)+" + FPS)");
        stylePill(modeGsr2Btn,sr&&"sgsr2".equals(m));
        stylePill(modeNeuralBtn,sr&&"neural2d".equals(m));
        stylePill(modeGsr1Btn,sr&&"sgsr1".equals(m));
        stylePill(modeFgOnlyBtn,!sr);
        syncAutoSwitch();
        syncingSr=true;upscale.setChecked(sr);syncingSr=false;
        syncingModel=true;srModels.setSelection("neural2d".equals(m)?1:("sgsr1".equals(m)?2:0));syncingModel=false;
    }
    private void selectSrModel(boolean enableSr,String modelName){
        AutoMode.preferences(this).edit().putBoolean("upscale",enableSr).putString("sr_model",modelName).putBoolean("neural_sr","neural2d".equals(modelName)).apply();
        refreshUiState();
        runJob(()->{
            AutoMode.setModel(this,modelName);
            if(AutoMode.requested(this)){
                AutoMode.configure(this,true,enableSr);
                return "Đã chuyển sang: "+(enableSr?("SR "+AutoMode.modelLabel(this)+" + FG 120Hz"):"Chỉ FG 120Hz (Tắt SR)")+".";
            }
            return "Đã chọn chế độ: "+(enableSr?("SR "+AutoMode.modelLabel(this)):"Chỉ FG (Tắt SR)")+". Bấm nút BẬT NGAY hoặc chạm vào giả lập để chơi.";
        });
    }
    private void quickLaunch(String pkg){
        final boolean sr=AutoMode.preferences(this).getBoolean("upscale",true);
        AutoMode.preferences(this).edit().putString("selected_package",pkg).apply();
        runJob(()->{
            Set<String> inc=new HashSet<>(AutoMode.preferences(this).getStringSet("auto_include",Collections.emptySet()));
            Set<String> exc=new HashSet<>(AutoMode.preferences(this).getStringSet("auto_exclude",Collections.emptySet()));
            inc.add(pkg);exc.remove(pkg);
            AutoMode.preferences(this).edit().putStringSet("auto_include",inc).putStringSet("auto_exclude",exc).apply();
            AutoMode.configure(this,true,sr);
            Intent launch=getPackageManager().getLaunchIntentForPackage(pkg);
            if(launch==null)return "Đã bật FG + SR nhưng chưa tìm thấy ứng dụng "+pkg+" trên máy.";
            runOnUiThread(()->{if(Settings.canDrawOverlays(this))showOverlay();startActivity(launch);});
            return "Đã bật FG 120Hz + "+(sr?AutoMode.modelLabel(this):"Tắt SR")+" và mở "+pkg+".";
        });
    }

    private void loadPackages(){
        Intent intent=new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LAUNCHER);
        List<ResolveInfo> list=getPackageManager().queryIntentActivities(intent,0);Map<String,String> unique=new TreeMap<>();
        for(ResolveInfo r:list){String id=r.activityInfo.packageName;if(!id.equals(getPackageName()))unique.put(id,r.loadLabel(getPackageManager()).toString());}
        ArrayList<String> sorted=new ArrayList<>(unique.keySet());
        sorted.sort((a,b)->{boolean ea=isEmulator(a),eb=isEmulator(b);return ea==eb?unique.get(a).compareToIgnoreCase(unique.get(b)):(ea?-1:1);});
        ArrayList<String> labels=new ArrayList<>();for(String id:sorted){ids.add(id);labels.add((isEmulator(id)?"★ ":"")+unique.get(id)+"\n"+id);}
        ArrayAdapter<String> adapter=new ArrayAdapter<String>(this,android.R.layout.simple_spinner_dropdown_item,labels){
            @Override public View getView(int position,View recycled,ViewGroup parent){
                TextView view=(TextView)super.getView(position,recycled,parent);
                view.setText(unique.get(ids.get(position)));view.setTextSize(15);return view;
            }
        };packages.setAdapter(adapter);
    }
    private boolean isEmulator(String id){return GameRules.isEmulator(id);}
    private void syncAutoSwitch(){syncingAuto=true;automatic.setChecked(AutoMode.requested(this));syncingAuto=false;}
    private void editAutoApplications(){
        List<AutoMode.Entry> entries=AutoMode.applications(this);String[] labels=new String[entries.size()];boolean[] selected=new boolean[entries.size()];
        for(int i=0;i<entries.size();i++){AutoMode.Entry entry=entries.get(i);labels[i]=entry.label+"\n"+entry.id;selected[i]=entry.selected;}
        new AlertDialog.Builder(this).setTitle("Tự bật FG cho ứng dụng đã chọn")
            .setMultiChoiceItems(labels,selected,(dialog,which,checked)->entries.get(which).selected=checked)
            .setNegativeButton("Hủy",null).setPositiveButton("Lưu",(dialog,which)->{AutoMode.saveSelection(this,entries);if(AutoMode.requested(this)){final boolean sr=upscale.isChecked();runJob(()->AutoMode.configure(this,true,sr));}else status.setText("Đã lưu danh sách.");}).show();
    }
    private void showOverlay(){
        if(!Settings.canDrawOverlays(this)){
            status.setText("Bật ‘Cho phép hiển thị trên ứng dụng khác’ cho Thor Native FG để hiện ô FPS nhỏ.");
            startActivityForResult(new Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION,Uri.parse("package:"+getPackageName())),104);return;
        }
        if(Build.VERSION.SDK_INT>=33&&checkSelfPermission("android.permission.POST_NOTIFICATIONS")!=PackageManager.PERMISSION_GRANTED)requestPermissions(new String[]{"android.permission.POST_NOTIFICATIONS"},105);
        getSharedPreferences("thor",MODE_PRIVATE).edit().putBoolean("hud_visible",true).apply();startForegroundService(new Intent(this,FpsOverlayService.class));
    }
    @Override protected void onActivityResult(int request,int result,Intent data){super.onActivityResult(request,result,data);if(request==104&&Settings.canDrawOverlays(this))showOverlay();}
    private String root(String script)throws Exception{return RootShell.run(script);}
    private File backup(){return new File(getFilesDir(),"settings-before.json");}
    private void saveBackup()throws Exception{
        if(backup().exists())return;JSONObject old=new JSONObject();for(String key:keys)old.put(key,root("settings get global "+key));old.put("thor_enabled",root("getprop debug.thorfg.enabled"));old.put("thor_target",root("getprop debug.thorfg.target"));old.put("thor_sr",root("getprop debug.thorfg.sr"));
        try(FileOutputStream f=new FileOutputStream(backup())){f.write(old.toString().getBytes("UTF-8"));f.getFD().sync();}
    }
    private void enableSelected(){
        if(ids.isEmpty())return;final String id=ids.get(packages.getSelectedItemPosition());
        quickLaunch(id);
    }
    private void restore()throws Exception{
        AutoMode.configure(this,false,upscale.isChecked());
        root("setprop debug.thorfg.enabled 0; setprop debug.thorfg.sr 0; setprop debug.thorfg.target ''; settings delete global enable_gpu_debug_layers >/dev/null 2>&1 || true; settings delete global gpu_debug_app >/dev/null 2>&1 || true; settings delete global gpu_debug_layers >/dev/null 2>&1 || true; settings delete global gpu_debug_layer_app >/dev/null 2>&1 || true");
        if(backup().exists())backup().delete();
    }
    private String readStatus(boolean includeLogs)throws Exception{
        boolean auto=AutoMode.requested(this);String autoStatus=auto?"Chế độ 1-Chạm: ĐANG BẬT (bootstrap="+root("getprop sys.thorfg.bootstrap")+")\n":"";
        String target=auto?root("game_target=$("+AutoMode.foregroundScript()+"); if "+AutoMode.allowedScript()+"; then printf %s \"$game_target\"; fi"):root("settings get global gpu_debug_app");
        if(auto&&target.isEmpty()){
            String last=AutoMode.preferences(this).getString("auto_last_package","");
            if(GameRules.validPackage(last)){target=root("game_target="+RootShell.quote(last)+"; if "+AutoMode.allowedScript()+"; then printf %s \"$game_target\"; fi");if(!target.isEmpty())autoStatus+="Ứng dụng gần nhất: "+target+"\n";}
        }
        String config=autoStatus+target+"\n"+root("getprop "+(auto?"debug.thorfg.auto":"debug.thorfg.enabled"));
        if(!target.matches("[A-Za-z0-9_.]+")||target.equals("null"))return autoStatus+(auto?"Sẵn sàng! Chạm vào nút mở Eden Thor / Eden Nightly / DroidDeck ở trên để vào game.":"Đang tắt. Bấm nút lớn ở trên để bật.");
        String pid=root("pidof -s "+target+" || true");
        if(!pid.matches("[0-9]+"))return "Target: "+target+"\nChưa có tiến trình đang chạy. Hãy mở game.";
        String logs=root("logcat -d --pid="+pid+" -t 3000 -s ThorNativeBootstrap:I ThorNativeLayer:I ThorNativeDemo:I '*:S' | tail -c 18000");
        if(logs.length()>18000)logs=logs.substring(logs.length()-18000);
        String extSr="";
        if("com.droiddeck.launcher".equals(target)){
            String raw=root("if pgrep -u 10201 -f '\\.exe' >/dev/null 2>&1; then grep -h 'Initializing.*SGSR2 ' /data/user/0/com.droiddeck.launcher/files/linuxfs/root/.local/share/Steam/steamapps/common/*/LittleFoxSim_v1/Binaries/Win64/OptiScaler-Thor-GSR2.log /data/user/0/com.droiddeck.launcher/files/linuxfs/root/.local/share/Steam/steamapps/common/*/*/OptiScaler-Thor-GSR2.log /data/user/0/com.droiddeck.launcher/files/linuxfs/root/.local/share/Steam/steamapps/common/*/OptiScaler-Thor-GSR2.log 2>/dev/null | tail -n 1 | awk '{for(i=1;i<=NF;i++) if($i==\"SGSR2\") {print $(i+1)\"->\"$(i+3); break}}'; fi");
            if(raw.matches("[0-9]+x[0-9]+->[0-9]+x[0-9]+"))extSr="SGSR2 "+raw.replace("x","×").replace("->","→");
        }
        RuntimeMetrics latest=null;for(String line:logs.split("\\r?\\n")){RuntimeMetrics value=RuntimeMetrics.parse(line,SystemClock.elapsedRealtime());if(value!=null&&value.pid==Integer.parseInt(pid))latest=value;}
        String detailText=latest!=null?(extSr.isEmpty()?latest.detail():extSr+" ON"):"";
        String summary=latest!=null&&latest.fresh(SystemClock.elapsedRealtime())?latest.hud(extSr)+"\n"+detailText:"Đang chờ khung hình Vulkan từ game…";
        if(!includeLogs)return summary+"\n\nỨng dụng: "+target;
        return summary+"\n\nTarget / active / PID:\n"+config+"\n"+pid+"\n\n"+logs;
    }
    interface Work {String run()throws Exception;}
    private void runJob(Work work){busy=true;masterBtn.setEnabled(false);modeNeuralBtn.setEnabled(false);modeGsr1Btn.setEnabled(false);modeFgOnlyBtn.setEnabled(false);status.setText("Đang áp dụng…");worker.execute(()->{String result;try{result=work.run();}catch(Exception e){result="Lỗi: "+e.getMessage();}final String value=result;runOnUiThread(()->{busy=false;masterBtn.setEnabled(true);modeNeuralBtn.setEnabled(true);modeGsr1Btn.setEnabled(true);modeFgOnlyBtn.setEnabled(true);refreshUiState();status.setText(value);});});}
}
