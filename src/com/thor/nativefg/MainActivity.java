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
    // ThorQOL palette: navy background, lime accent, cyan glow.
    static final int C_BG=0xFF10151B,C_CARD=0xFF1B202C,C_INNER=0xFF292B40,C_STROKE=0xFF375B67,
        C_ACCENT=0xFFDCFF30,C_GLOW=0xFF20E5E5,C_TEXT=0xFFF7FFFF,C_TEXT2=0xFFB6C6D0,C_MUTED=0xFF84919D,C_RED=0xFFFF4795;
    private TextView status;
    private Button masterBtn,modeGsr2Btn,modeNeuralBtn,modeGsr1Btn,modeFgOnlyBtn;
    private Spinner packages,srModels;
    private CheckBox upscale,fxaaToggle,taaToggle,smaaToggle,tripleToggle;
    private boolean syncingAa=false;
    private boolean syncingTriple=false;
    private Switch automatic;
    private boolean syncingAuto=false;
    private boolean syncingSr=false;
    private boolean syncingModel=false;
    private License license;
    private AppUpdates updates;
    private TextView versionLabel,licenseLabel,updateLabel;
    private Button updateBtn,licenseBtn;
    private final ArrayList<String> ids=new ArrayList<>();
    private final java.util.concurrent.ExecutorService worker=Executors.newSingleThreadExecutor();
    private final String[] keys={"enable_gpu_debug_layers","gpu_debug_app","gpu_debug_layers","gpu_debug_layer_app"};
    private static final String MODULE="/data/adb/modules/thor_native_fg_bootstrap";
    private boolean busy=false;

    @Override public void onCreate(Bundle state){
        super.onCreate(state);
        license=new License(this);
        updates=new AppUpdates(this,()->runOnUiThread(this::refreshUpdateRow));
        LinearLayout page=new LinearLayout(this);page.setOrientation(LinearLayout.VERTICAL);page.setPadding(dp(18),dp(14),dp(18),dp(18));page.setBackgroundColor(C_BG);
        ScrollView scroll=new ScrollView(this);scroll.setFillViewport(true);scroll.addView(page);setContentView(scroll);

        TextView title=text("THOR NATIVE FG",22);title.setTextColor(C_ACCENT);title.setTypeface(Typeface.DEFAULT_BOLD);page.addView(title);
        TextView sub=text("Bù khung hình + khử răng cưa cho AYN Thor",12);sub.setTextColor(C_TEXT2);page.addView(sub);
        versionLabel=text("",12);versionLabel.setTextColor(C_MUTED);page.addView(versionLabel);
        try{
            PackageInfo info=getPackageManager().getPackageInfo(getPackageName(),0);
            long code=Build.VERSION.SDK_INT>=28?info.getLongVersionCode():info.versionCode;
            versionLabel.setText("Phiên bản "+info.versionName+" ("+code+")");
        }catch(Exception ignored){}

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
                    return "Đã TẮT toàn bộ FG + AA và ẩn thanh FPS.";
                });
            }else{
                final boolean sr=AutoMode.preferences(this).getBoolean("upscale",true);
                runJob(()->{
                    String res=AutoMode.configure(this,true,sr);
                    runOnUiThread(()->showOverlay());
                    return "Đã BẬT 1-chạm (FG 120Hz + "+aaLabelText(AutoMode.aaMode(this))+") và đồng bộ dữ liệu Eden.\n"+res;
                });
            }
        });

        // 1b. Bản quyền + cập nhật (cùng kiểu thẻ như ThorQOL)
        LinearLayout serviceRow=new LinearLayout(this);serviceRow.setOrientation(LinearLayout.HORIZONTAL);
        LinearLayout.LayoutParams serviceLp=new LinearLayout.LayoutParams(-1,-2);serviceLp.setMargins(0,0,0,dp(10));page.addView(serviceRow,serviceLp);
        licenseBtn=pillButton("Bản quyền",()->openLicenseDialog());
        updateBtn=pillButton("Cập nhật",()->openUpdateDialog());
        addWeighted(serviceRow,licenseBtn,dp(6));
        addWeighted(serviceRow,updateBtn,0);
        stylePill(licenseBtn,false);stylePill(updateBtn,false);
        licenseLabel=text("",12);licenseLabel.setTextColor(C_TEXT2);page.addView(licenseLabel);
        updateLabel=text("",12);updateLabel.setTextColor(C_MUTED);page.addView(updateLabel);

        TextView aaLabel=text("Khử răng cưa (chạm để đổi ngay):",13);aaLabel.setTextColor(Color.rgb(185,205,228));page.addView(aaLabel);
        LinearLayout srRow=new LinearLayout(this);srRow.setOrientation(LinearLayout.HORIZONTAL);
        LinearLayout.LayoutParams rowLp=new LinearLayout.LayoutParams(-1,-2);rowLp.setMargins(0,0,0,dp(10));page.addView(srRow,rowLp);
        modeGsr2Btn=pillButton("SMAA",()->selectAaMode("smaa"));
        modeNeuralBtn=pillButton("FXAA",()->selectAaMode("fxaa"));
        modeGsr1Btn=pillButton("Tắt AA",()->selectAaMode("off"));
        addWeighted(srRow,modeGsr2Btn,dp(6));
        addWeighted(srRow,modeNeuralBtn,dp(6));
        addWeighted(srRow,modeGsr1Btn,0);
        page.addView(text("SMAA: khử răng cưa hình thái, giữ chữ UI sắc nét (mặc định). FXAA: rẻ hơn, hơi mờ. AA chỉ áp cho khung 2× (màn 60Hz); chế độ 3× dùng khung thô để đủ ngân sách. Cần mở lại game sau khi đổi.",12));
        modeFgOnlyBtn=new Button(this);modeFgOnlyBtn.setVisibility(View.GONE);page.addView(modeFgOnlyBtn);
        // The pill row above is the AA selector now; the old checkboxes stay in the
        // layout tree (hidden) so the existing preference plumbing keeps working.
        smaaToggle=new CheckBox(this);smaaToggle.setVisibility(View.GONE);page.addView(smaaToggle);
        smaaToggle.setOnCheckedChangeListener((view,checked)->{
            if(syncingAa)return;
            if(busy){syncingAa=true;smaaToggle.setChecked(AutoMode.smaa(this));syncingAa=false;return;}
            runJob(()->AutoMode.setSmaa(this,checked));
            if(checked){syncingAa=true;taaToggle.setChecked(false);fxaaToggle.setChecked(false);syncingAa=false;}
        });
        taaToggle=new CheckBox(this);taaToggle.setVisibility(View.GONE);page.addView(taaToggle);
        taaToggle.setOnCheckedChangeListener((view,checked)->{
            if(syncingAa)return;
            if(busy){syncingAa=true;taaToggle.setChecked(AutoMode.taa(this));syncingAa=false;return;}
            runJob(()->AutoMode.setTaa(this,checked));
            if(checked){syncingAa=true;fxaaToggle.setChecked(false);smaaToggle.setChecked(false);syncingAa=false;}
        });
        fxaaToggle=new CheckBox(this);fxaaToggle.setVisibility(View.GONE);page.addView(fxaaToggle);
        fxaaToggle.setOnCheckedChangeListener((view,checked)->{
            if(syncingAa)return;
            if(busy){syncingAa=true;fxaaToggle.setChecked(AutoMode.fxaa(this));syncingAa=false;return;}
            runJob(()->AutoMode.setFxaa(this,checked));
            if(checked){syncingAa=true;taaToggle.setChecked(false);smaaToggle.setChecked(false);syncingAa=false;}
        });

        // 2b. Bù 3 khung hình (3×) — chỉ hợp với game dưới ~40 FPS trên màn 120Hz
        tripleToggle=new CheckBox(this);tripleToggle.setText("3× tự động · màn 120Hz bù 2 khung (30→90), màn 60Hz layer tự dùng 2×");tripleToggle.setTextColor(Color.rgb(223,233,246));tripleToggle.setChecked(AutoMode.triple(this));page.addView(tripleToggle);
        tripleToggle.setOnCheckedChangeListener((view,checked)->{
            if(syncingTriple)return;
            if(busy){syncingTriple=true;tripleToggle.setChecked(AutoMode.triple(this));syncingTriple=false;return;}
            runJob(()->AutoMode.setTriple(this,checked));
        });
        page.addView(text("Layer tự dò nhịp thật của màn hình: 60Hz thì chỉ bù 1 khung (2×), 120Hz thì bù 2 khung (3×) sau khi thử và xác nhận khung sinh thực sự lên màn hình. Bật/tắt ngay khi game đang chạy. Bỏ chọn nếu muốn ép chỉ dùng 2×.",12));

        // 3. Mở nhanh Game / Giả lập (1 chạm: tự bật + mở app)
        TextView launchLabel=text("Mở nhanh giả lập (1 chạm tự bật FG + AA & vào thẳng):",13);launchLabel.setTextColor(Color.rgb(185,205,228));page.addView(launchLabel);
        LinearLayout launchRow=new LinearLayout(this);launchRow.setOrientation(LinearLayout.HORIZONTAL);page.addView(launchRow,rowLp);
        Button edenThorBtn=actionButton("🎮 Eden Thor (AA+FG)",C_INNER,()->quickLaunch("dev.eden.eden_emulator.relWithDebInfo"));
        Button edenNightlyBtn=actionButton("🎮 Eden Nightly",C_CARD,()->quickLaunch("dev.eden.eden_emulator.nightly"));
        Button droidDeckBtn=actionButton("💻 DroidDeck (PC)",C_CARD,()->quickLaunch("com.droiddeck.launcher"));
        addWeighted(launchRow,edenThorBtn,dp(6));
        addWeighted(launchRow,edenNightlyBtn,dp(6));
        addWeighted(launchRow,droidDeckBtn,0);

        // 4. Thanh công cụ phụ (Hiện/Ẩn FPS, Kiểm tra trạng thái, Tùy chọn nâng cao)
        LinearLayout utilRow=new LinearLayout(this);utilRow.setOrientation(LinearLayout.HORIZONTAL);page.addView(utilRow,rowLp);
        Button fpsToggleBtn=actionButton("Hiện / Ẩn FPS",C_CARD,()->{
            boolean vis=AutoMode.preferences(this).getBoolean("hud_visible",false);
            if(vis){AutoMode.preferences(this).edit().putBoolean("hud_visible",false).apply();stopService(new Intent(this,FpsOverlayService.class));status.setText("Đã ẩn thanh FPS ngang.");}
            else{showOverlay();status.setText("Đã bật thanh FPS: Gốc | FG | AA | nhiệt độ.");}
        });
        Button checkBtn=actionButton("Kiểm tra FG + AA",C_CARD,()->runJob(()->readStatus(false)));
        LinearLayout advanced=new LinearLayout(this);advanced.setOrientation(LinearLayout.VERTICAL);advanced.setVisibility(View.GONE);
        Button moreBtn=actionButton("Tùy chọn khác ▾",C_CARD,null);
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
        advanced.addView(text("Chọn ứng dụng bất kỳ để mở cùng FG + AA:",13));
        packages=new Spinner(this);loadPackages();advanced.addView(packages);
        upscale=new CheckBox(this);upscale.setVisibility(View.GONE);upscale.setText("Bật upscale từ ảnh render thấp");upscale.setTextColor(Color.rgb(223,233,246));upscale.setChecked(AutoMode.preferences(this).getBoolean("upscale",true));advanced.addView(upscale);
        upscale.setOnCheckedChangeListener((view,checked)->{if(!syncingSr)selectSrModel(checked,AutoMode.model(this));});
        srModels=new Spinner(this);srModels.setVisibility(View.GONE);
        srModels.setAdapter(new ArrayAdapter<String>(this,android.R.layout.simple_spinner_dropdown_item,new String[]{"Spatial 9-tap · chặn viền sáng","Neural 2D · giảm viền","GSR 1 · nhẹ, mặc định"}));
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
        button(advanced,"DroidDeck · dùng scaler trong game",()->runJob(()->DroidDeckPreset.change(this,false)));
        button(advanced,"DroidDeck · dùng SGSR2 trong game (DLSS/FSR2)",()->runJob(()->DroidDeckPreset.change(this,true)));
        button(advanced,"Xem log kỹ thuật",()->runJob(()->readStatus(true)));
        button(advanced,"Chạy cảnh mẫu AA + FG",()->startActivity(new Intent(this,DemoActivity.class)));

        String previous=AutoMode.preferences(this).getString("selected_package","");for(int i=0;i<ids.size();i++)if(ids.get(i).equals(previous)){packages.setSelection(i);break;}
        refreshUiState();
        AppUpdates.autoCheck(this);
        if(getIntent().getBooleanExtra("demo",false)){Intent demo=new Intent(this,DemoActivity.class);demo.putExtra("mode",getIntent().getIntExtra("mode",2));startActivity(demo);}
        if(getIntent().getBooleanExtra("hud",false)||(AutoMode.requested(this)&&AutoMode.preferences(this).getBoolean("hud_visible",false)&&Settings.canDrawOverlays(this)))showOverlay();
    }

    private int dp(int v){return Math.round(v*getResources().getDisplayMetrics().density);}
    @Override protected void onResume(){
        super.onResume();
        // debug.* properties do not survive a reboot, and a manual setprop can
        // leave them out of sync with the saved switch. Re-apply quietly so the
        // checkbox never claims a mode the layer is not actually in.
        if(busy)return;
        final boolean want=AutoMode.triple(this);
        worker.execute(()->{
            try{String current=root("getprop debug.thorfg.triple");boolean allowed=!"0".equals(current);if(allowed!=want)AutoMode.setTriple(this,want);}catch(Exception ignored){}
        });
    }
    private TextView text(String value,int size){TextView v=new TextView(this);v.setText(value);v.setTextSize(size);v.setTextColor(C_TEXT);v.setPadding(0,dp(3),0,dp(5));return v;}
    private void button(LinearLayout page,String label,Runnable action){Button b=new Button(this);b.setText(label);b.setAllCaps(false);b.setTextSize(13);b.setTextColor(C_TEXT2);GradientDrawable bg=new GradientDrawable();bg.setColor(C_CARD);bg.setCornerRadius(dp(8));bg.setStroke(dp(1),C_STROKE);b.setBackground(bg);page.addView(b);b.setOnClickListener(v->{if(!busy)action.run();});}
    private Button pillButton(String label,Runnable action){
        Button b=new Button(this);b.setText(label);b.setAllCaps(false);b.setTextSize(13);b.setTextColor(C_TEXT);
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
        bg.setColor(active?C_ACCENT:C_CARD);
        bg.setCornerRadius(dp(8));
        bg.setStroke(dp(1),active?C_ACCENT:C_STROKE);
        btn.setBackground(bg);
        btn.setTextColor(active?0xFF10151B:C_TEXT);
        btn.setTypeface(active?Typeface.DEFAULT_BOLD:Typeface.DEFAULT);
    }
    private void refreshUiState(){
        syncingAa=true;fxaaToggle.setChecked(AutoMode.fxaa(this));taaToggle.setChecked(AutoMode.taa(this));smaaToggle.setChecked(AutoMode.smaa(this));syncingAa=false;
        syncingTriple=true;tripleToggle.setChecked(AutoMode.triple(this));syncingTriple=false;
        boolean on=AutoMode.requested(this);
        boolean sr=AutoMode.preferences(this).getBoolean("upscale",true);
        String aa=AutoMode.aaMode(this);
        GradientDrawable mBg=new GradientDrawable();
        mBg.setColor(on?0xFF3A1620:C_ACCENT);
        mBg.setCornerRadius(dp(10));
        mBg.setStroke(dp(1),on?C_RED:C_GLOW);
        masterBtn.setBackground(mBg);
        masterBtn.setTextColor(on?C_TEXT:0xFF10151B);
        masterBtn.setText(on?"⏹ ĐANG BẬT (FG 120Hz + "+aaLabelText(aa)+") · CHẠM ĐỂ TẮT":"⚡ 1 CHẠM: BẬT NGAY (FG 120Hz + "+aaLabelText(aa)+" + FPS)");
        stylePill(modeGsr2Btn,"smaa".equals(aa));
        stylePill(modeNeuralBtn,"fxaa".equals(aa));
        stylePill(modeGsr1Btn,"off".equals(aa));
        stylePill(modeFgOnlyBtn,false);
        syncAutoSwitch();
        syncingSr=true;upscale.setChecked(sr);syncingSr=false;
        // The SR model spinner stays hidden: the layer no longer upscales, the
        // emulator app owns that now and the pills above select the AA mode.
        refreshLicenseRow();refreshUpdateRow();applyLicenseGate();
    }
    private static String aaLabelText(String aa){
        if("smaa".equals(aa))return "SMAA";
        return "fxaa".equals(aa)?"FXAA":"Tắt AA";
    }
    private void selectAaMode(String mode){
        runJob(()->{
            AutoMode.setSmaa(this,"smaa".equals(mode));
            AutoMode.setFxaa(this,"fxaa".equals(mode));
            runOnUiThread(this::refreshUiState);
            return "Đã chọn khử răng cưa: "+aaLabelText(mode)+". Mở lại game để áp dụng (chỉ khung 2×).";
        });
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
        root("setprop debug.thorfg.enabled 0; setprop debug.thorfg.sr 0; setprop debug.thorfg.target ''; setprop debug.thorfg.triple 0; settings delete global enable_gpu_debug_layers >/dev/null 2>&1 || true; settings delete global gpu_debug_app >/dev/null 2>&1 || true; settings delete global gpu_debug_layers >/dev/null 2>&1 || true; settings delete global gpu_debug_layer_app >/dev/null 2>&1 || true");
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
        String summary=latest!=null&&latest.fresh(SystemClock.elapsedRealtime())?latest.fpsOnly()+"\n"+detailText:"Đang chờ khung hình Vulkan từ game…";
        if(!includeLogs)return summary+"\n\nỨng dụng: "+target;
        return summary+"\n\nTarget / active / PID:\n"+config+"\n"+pid+"\n\n"+logs;
    }
    // ---- Cập nhật & bản quyền -------------------------------------------------
    private LinearLayout card(){
        LinearLayout box=new LinearLayout(this);box.setOrientation(LinearLayout.VERTICAL);
        GradientDrawable bg=new GradientDrawable();bg.setColor(C_CARD);bg.setCornerRadius(dp(12));bg.setStroke(dp(1),C_STROKE);
        box.setBackground(bg);int pad=dp(12);box.setPadding(pad,pad,pad,pad);
        LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(-1,-2);lp.setMargins(0,0,0,dp(10));box.setLayoutParams(lp);
        return box;
    }

    private void refreshLicenseRow(){
        if(licenseLabel==null||license==null)return;
        License.Status status=license.status();
        licenseLabel.setText(status.label()+("".equals(status.detail)?"":" · "+status.detail));
        licenseLabel.setTextColor(status.usable()?C_ACCENT:C_MUTED);
        if(licenseBtn!=null)licenseBtn.setText(status.licensed()?"Bản quyền: FULL":(status.state==License.State.TRIAL?"Bản quyền: dùng thử":"Bản quyền"));
    }

    void refreshUpdateRow(){
        if(updateLabel==null||updates==null)return;
        String state;
        switch(updates.state){
            case AppUpdates.CHECKING:state="đang kiểm tra";break;
            case AppUpdates.CURRENT:state="mới nhất";break;
            case AppUpdates.AVAILABLE:state="có bản "+updates.version;break;
            case AppUpdates.DOWNLOADING:state=updates.total>0?(100*updates.done/updates.total)+"%":"đang tải";break;
            case AppUpdates.VERIFYING:state="đang xác minh";break;
            case AppUpdates.READY:state="sẵn sàng cài "+updates.version;break;
            case AppUpdates.ERROR:state="lỗi";break;
            default:state="chưa kiểm tra";
        }
        String text="Cập nhật: "+state;
        if(updates.message!=null&&!updates.message.isEmpty())text+=" · "+updates.message;
        updateLabel.setText(text);
        updateLabel.setTextColor(updates.state==AppUpdates.ERROR?C_RED:(updates.state==AppUpdates.READY?C_ACCENT:C_MUTED));
        if(updateBtn!=null)updateBtn.setText(AppUpdates.hasUpdate(this)?"Cập nhật ●":"Cập nhật");
    }

    void openUpdateDialog(){
        LinearLayout box=card();
        TextView info=text("",13);info.setTextColor(C_TEXT2);box.addView(info);
        ProgressBar bar=new ProgressBar(this,null,android.R.attr.progressBarStyleHorizontal);
        bar.setMax(100);bar.setProgress(0);box.addView(bar);
        LinearLayout actions=new LinearLayout(this);actions.setOrientation(LinearLayout.HORIZONTAL);
        Button check=pillButton("Kiểm tra",()->{updates.check();});
        Button download=pillButton("Tải",()->{updates.download();});
        Button install=pillButton("Cài",()->{updates.install(this);});
        addWeighted(actions,check,dp(6));
        addWeighted(actions,download,dp(6));
        addWeighted(actions,install,0);
        box.addView(actions);
        AlertDialog dialog=new AlertDialog.Builder(this).setTitle("Cập nhật qua git")
            .setView(box).setNegativeButton("Đóng",null).create();
        final android.os.Handler handler=new android.os.Handler(Looper.getMainLooper());
        final Runnable[] tick=new Runnable[1];
        tick[0]=()->{
            if(!dialog.isShowing())return;
            String current=updates.installedVersion();
            String text=updates.message==null?"":updates.message;
            switch(updates.state){
                case AppUpdates.CURRENT:text="Bạn đang dùng "+current+", mới nhất.";break;
                case AppUpdates.AVAILABLE:text="Có bản "+updates.version+" (đang dùng "+current+").";break;
                case AppUpdates.READY:text="Đã tải và xác minh bản "+updates.version+". Bấm Cài.";break;
                default:break;
            }
            info.setText(text+("\n\n"+(updates.notes==null?"":updates.notes)));
            bar.setProgress(updates.total>0?(int)(100*updates.done/updates.total):0);
            check.setEnabled(!updates.busy());
            download.setEnabled(updates.state==AppUpdates.AVAILABLE);
            install.setEnabled(updates.state==AppUpdates.READY);
            stylePill(download,download.isEnabled());
            stylePill(install,install.isEnabled());
            handler.postDelayed(tick[0],400);
        };
        dialog.setOnShowListener(d->{tick[0].run();});
        dialog.setOnDismissListener(d->handler.removeCallbacks(tick[0]));
        dialog.show();
        if(updates.state==AppUpdates.IDLE)updates.check();
    }

    void openLicenseDialog(){
        LinearLayout box=card();
        License.Status status=license.status();
        TextView info=text(status.label()+"\n"+status.detail+"\n"+license.describeDevice(),13);
        info.setTextColor(status.usable()?C_ACCENT:C_MUTED);box.addView(info);
        EditText input=new EditText(this);
        input.setHint("Dán key THORFG1…");
        input.setTextColor(C_TEXT);input.setHintTextColor(C_MUTED);
        box.addView(input);
        LinearLayout actions=new LinearLayout(this);actions.setOrientation(LinearLayout.HORIZONTAL);
        Button activate=pillButton("Kích hoạt",()->{
            License.Status result=license.activate(input.getText().toString());
            info.setText(result.label()+"\n"+result.detail+"\n"+license.describeDevice());
            info.setTextColor(result.usable()?C_ACCENT:C_RED);
            refreshLicenseRow();applyLicenseGate();
            toast(result.licensed()?"Đã kích hoạt bản quyền.":result.detail);
        });
        Button buy=pillButton("Mua",()->startActivity(new Intent(Intent.ACTION_VIEW,Uri.parse(license.shopUrl()))));
        Button clear=pillButton("Xoá key",()->{license.clear();refreshLicenseRow();applyLicenseGate();toast("Đã xoá key trên máy.");});
        addWeighted(actions,activate,dp(6));
        addWeighted(actions,buy,dp(6));
        addWeighted(actions,clear,0);
        box.addView(actions);
        if(!license.enforced()){box.addView(text("Bản build này chưa bật chặn bản quyền — key chỉ để hiển thị trạng thái.",11));}
        new AlertDialog.Builder(this).setTitle("Bản quyền Thor Native FG").setView(box).setNegativeButton("Đóng",null).show();
    }

    /** Blocks the one-tap master switch only when the build enforces licensing. */
    private void applyLicenseGate(){
        if(license==null||masterBtn==null)return;
        if(!license.enforced())return;
        License.Status licence=license.status();
        if(licence.usable())return;
        AutoMode.preferences(this).edit().putBoolean("locked_by_license",true).apply();
        masterBtn.setEnabled(false);
        if(status!=null)status.setText("Cần bản quyền: "+licence.label()+" — bấm “Bản quyền” để kích hoạt.");
    }

    private void toast(String message){
        try{Toast.makeText(this,message,Toast.LENGTH_SHORT).show();}catch(Exception ignored){}
    }
    interface Work {String run()throws Exception;}
    private void runJob(Work work){busy=true;masterBtn.setEnabled(false);modeNeuralBtn.setEnabled(false);modeGsr1Btn.setEnabled(false);modeFgOnlyBtn.setEnabled(false);status.setText("Đang áp dụng…");worker.execute(()->{String result;try{result=work.run();}catch(Exception e){result="Lỗi: "+e.getMessage();}final String value=result;runOnUiThread(()->{busy=false;masterBtn.setEnabled(true);modeNeuralBtn.setEnabled(true);modeGsr1Btn.setEnabled(true);modeFgOnlyBtn.setEnabled(true);refreshUiState();status.setText(value);});});}
}
