package com.thor.nativefg;

import android.app.*;
import android.content.*;
import android.graphics.Color;
import android.graphics.PixelFormat;
import android.graphics.drawable.GradientDrawable;
import android.os.*;
import android.provider.Settings;
import android.view.*;
import android.widget.*;
import java.io.*;

/** Small, movable HUD backed by the selected game's native telemetry. */
public class FpsOverlayService extends Service {
    private static final String CHANNEL="thor-fps";
    private final Handler main=new Handler(Looper.getMainLooper());
    private WindowManager windows;private WindowManager.LayoutParams position;private TextView hud;
    private volatile boolean stopped=false,active=false;private volatile String target="",error="",externalSr="";
    private volatile boolean automatic=false;
    private volatile long configAt=0;private boolean attached=false;
    private volatile int targetPid=0;private volatile RuntimeMetrics metrics;
    private volatile java.lang.Process logProcess;private Thread logThread;

    @Override public void onCreate(){
        super.onCreate();
        if(!getPreferences().getBoolean("hud_visible",false)||!Settings.canDrawOverlays(this)){stopSelf();return;}
        NotificationManager manager=getSystemService(NotificationManager.class);
        manager.createNotificationChannel(new NotificationChannel(CHANNEL,"Thor FPS và FG/SR",NotificationManager.IMPORTANCE_LOW));
        PendingIntent open=PendingIntent.getActivity(this,0,new Intent(this,MainActivity.class),PendingIntent.FLAG_IMMUTABLE|PendingIntent.FLAG_UPDATE_CURRENT);
        PendingIntent hide=PendingIntent.getService(this,1,new Intent(this,FpsOverlayService.class).setAction("HIDE"),PendingIntent.FLAG_IMMUTABLE|PendingIntent.FLAG_UPDATE_CURRENT);
        Notification notification=new Notification.Builder(this,CHANNEL).setSmallIcon(android.R.drawable.ic_media_play)
            .setContentTitle("Thor · FPS + trạng thái FG/SR").setContentText("Chạm ô FPS để xem ý nghĩa số đo; kéo để di chuyển.")
            .setContentIntent(open).addAction(new Notification.Action.Builder(null,"Ẩn ô FPS",hide).build()).setOngoing(true).build();
        startForeground(41,notification);
        windows=(WindowManager)getSystemService(WINDOW_SERVICE);hud=new TextView(this);hud.setTextColor(Color.rgb(199,255,235));hud.setTextSize(11);
        hud.setPadding(dp(7),dp(4),dp(7),dp(4));hud.setMaxWidth(dp(440));hud.setSingleLine(true);
        GradientDrawable background=new GradientDrawable();background.setColor(0xCC10191F);background.setCornerRadius(dp(7));hud.setBackground(background);
        position=new WindowManager.LayoutParams(WindowManager.LayoutParams.WRAP_CONTENT,WindowManager.LayoutParams.WRAP_CONTENT,
            WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE|WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL,PixelFormat.TRANSLUCENT);
        position.gravity=Gravity.TOP|Gravity.LEFT;position.x=getPreferences().getInt("hud_x",dp(8));position.y=getPreferences().getInt("hud_y",dp(64));
        hud.setOnTouchListener(new View.OnTouchListener(){float downX,downY;int oldX,oldY;boolean dragged;
            public boolean onTouch(View view,MotionEvent event){
                if(event.getAction()==MotionEvent.ACTION_DOWN){downX=event.getRawX();downY=event.getRawY();oldX=position.x;oldY=position.y;dragged=false;return true;}
                if(event.getAction()==MotionEvent.ACTION_MOVE){float dx=event.getRawX()-downX,dy=event.getRawY()-downY;if(Math.abs(dx)+Math.abs(dy)>dp(5))dragged=true;if(dragged){position.x=oldX+(int)dx;position.y=oldY+(int)dy;clampPosition();windows.updateViewLayout(hud,position);}return true;}
                if(event.getAction()==MotionEvent.ACTION_UP){if(dragged)getPreferences().edit().putInt("hud_x",position.x).putInt("hud_y",position.y).apply();else{view.performClick();Toast.makeText(FpsOverlayService.this,"Gốc = khung ứng dụng gửi vào engine. FG dùng timestamp trình bày Vulkan khi có; dấu ≈ là ước tính từ yêu cầu gửi khung. Xem trạng thái SR trong app.",Toast.LENGTH_LONG).show();}return true;}
                return true;
            }});
        getPreferences().edit().putBoolean("hud_visible",true).apply();
        logThread=new Thread(()->readLogs(),"thor-runtime-hud");logThread.start();main.post(refresh);
    }
    private SharedPreferences getPreferences(){return getSharedPreferences("thor",MODE_PRIVATE);}
    // AA mode and device temperature for the HUD. Both are read locally: the
    // prefs are what writes the layer properties, and the thermal zones are
    // world readable (verified on the Thor: -r--r--r--).
    private String aaLabel(){
        boolean smaa=getPreferences().getBoolean("smaa",true);
        boolean fxaa=getPreferences().getBoolean("fxaa",false);
        return smaa?"AA SMAA":fxaa?"AA FXAA":"AA tắt";
    }
    private long temperatureAt=0;
    private String temperatureValue="—°C";
    private String temperature(){
        // Reading 40 sysfs files on every HUD tick made the overlay thread and the
        // UI feel sluggish; one sample every few seconds is plenty for a thermometer.
        long now=SystemClock.elapsedRealtime();
        if(now-temperatureAt<3000)return temperatureValue;
        temperatureAt=now;
        temperatureValue=readTemperature();
        return temperatureValue;
    }
    private String readTemperature(){
        int best=0;
        for(int zone=0;zone<40;zone++){
            try{
                java.io.BufferedReader reader=new java.io.BufferedReader(new java.io.FileReader("/sys/class/thermal/thermal_zone"+zone+"/temp"));
                String raw=reader.readLine();reader.close();
                if(raw==null)continue;
                int value=Integer.parseInt(raw.trim());
                if(value>1000)value/=1000;
                if(value>0&&value<125&&value>best)best=value;
            }catch(Exception ignored){}
        }
        if(best>0)return best+"°C";
        try{
            Intent battery=registerReceiver(null,new android.content.IntentFilter(Intent.ACTION_BATTERY_CHANGED));
            if(battery!=null){int tenths=battery.getIntExtra(android.os.BatteryManager.EXTRA_TEMPERATURE,0);if(tenths>0)return (tenths/10)+"°C";}
        }catch(Exception ignored){}
        return "—°C";
    }
    private int dp(int value){return Math.round(value*getResources().getDisplayMetrics().density);}
    private void clampPosition(){android.util.DisplayMetrics display=getResources().getDisplayMetrics();position.x=Math.max(0,Math.min(position.x,Math.max(0,display.widthPixels-hud.getWidth())));position.y=Math.max(0,Math.min(position.y,Math.max(0,display.heightPixels-hud.getHeight())));}
    @Override public void onConfigurationChanged(android.content.res.Configuration config){super.onConfigurationChanged(config);if(hud!=null&&attached){clampPosition();windows.updateViewLayout(hud,position);}}
    private void showWindow(boolean show){
        if(show&&!attached){try{clampPosition();windows.addView(hud,position);attached=true;}catch(RuntimeException failure){stopSelf();}}
        else if(!show&&attached){try{windows.removeView(hud);}catch(RuntimeException ignored){}attached=false;}
    }
    private void readConfig(String line){
        try{
            String selected="",pidText="0",on="",auto="",sgsr2="";
            for(String token:line.substring(12).trim().split("\\s+")){
                if(token.startsWith("target="))selected=token.substring(7);
                else if(token.startsWith("pid="))pidText=token.substring(4);
                else if(token.startsWith("active="))on=token.substring(7);
                else if(token.startsWith("auto="))auto=token.substring(5);
                else if(token.startsWith("sgsr2="))sgsr2=token.substring(6);
            }
            if(!selected.isEmpty()&&!selected.matches("[A-Za-z0-9_.]+"))return;
            int pid=pidText.matches("[0-9]+")?Integer.parseInt(pidText):0;
            if("1".equals(auto)&&pid>0&&!selected.isEmpty()&&!selected.equals(getPreferences().getString("auto_last_package","")))getPreferences().edit().putString("auto_last_package",selected).apply();
            if(!selected.equals(target)||pid!=targetPid)metrics=null;
            target=selected;targetPid=pid;active="1".equals(on);automatic="1".equals(auto);
            externalSr=sgsr2.matches("[0-9]+x[0-9]+->[0-9]+x[0-9]+")?"SGSR2 "+sgsr2.replace("x","×").replace("->","→"):"";
            error="";configAt=SystemClock.elapsedRealtime();
        }catch(RuntimeException invalid){/* Ignore incomplete monitor output. */}
    }
    private void readLogs(){
        try{
            // One root session avoids a new Magisk toast/authorization on every
            // refresh. stdin is a stop channel; EOF also tears down both children.
            String monitor="logcat -v brief -T 1 -s ThorNativeLayer:I '*:S' &\nlog_pid=$!\n"+
                "( while :; do\n game_active=$(getprop debug.thorfg.enabled)\n game_target=$(getprop debug.thorfg.target)\n"+
                " if [ \"$game_active\" = 1 ] && [ -z \"$game_target\" ]; then game_target=$(settings get global gpu_debug_app); fi\n"+
                " auto_mode=$(getprop debug.thorfg.auto)\n if [ \"$auto_mode\" = 1 ]; then\n game_target=$("+AutoMode.foregroundScript()+")\n game_active=0\n"+
                " case \"$game_target\" in ''|null|*[!A-Za-z0-9_.]*) game_target='';; *) if "+AutoMode.allowedScript()+"; then game_active=1; else game_target=''; fi;; esac\n fi\n"+
                " case \"$game_target\" in ''|null|*[!A-Za-z0-9_.]*) game_target='';; esac\n"+
                " game_pid=0\n if [ \"$game_active\" = 1 ] && [ -n \"$game_target\" ]; then game_pid=$(pidof -s \"$game_target\"); fi\n"+
                " sgsr2_info=''\n if [ \"$game_target\" = com.droiddeck.launcher ] && [ -n \"$game_pid\" ] && [ \"$game_pid\" != 0 ] && pgrep -u 10201 -f '\\.exe' >/dev/null 2>&1; then\n"+
                "  sgsr2_info=$(grep -h 'Initializing.*SGSR2 ' /data/user/0/com.droiddeck.launcher/files/linuxfs/root/.local/share/Steam/steamapps/common/*/LittleFoxSim_v1/Binaries/Win64/OptiScaler-Thor-GSR2.log /data/user/0/com.droiddeck.launcher/files/linuxfs/root/.local/share/Steam/steamapps/common/*/*/OptiScaler-Thor-GSR2.log /data/user/0/com.droiddeck.launcher/files/linuxfs/root/.local/share/Steam/steamapps/common/*/OptiScaler-Thor-GSR2.log 2>/dev/null | tail -n 1 | awk '{for(i=1;i<=NF;i++) if($i==\"SGSR2\") {print $(i+1)\"->\"$(i+3); break}}')\n fi\n"+
                " printf 'THOR_CONFIG target=%s active=%s pid=%s auto=%s sgsr2=%s\\n' \"$game_target\" \"$game_active\" \"$game_pid\" \"$auto_mode\" \"$sgsr2_info\"\n"+
                // A30FPS game votes the panel down to60Hz, which leaves no slot
                // for three generated frames. Keep120Hz while a framed game runs
                // and restore the user's own values afterwards.
                " triple_allowed=$(getprop persist.thorfg.triple)\n saved=/data/adb/thor-native-fg/refresh.saved\n"+
                " if [ \"$game_active\" = 1 ] && [ -n \"$game_target\" ] && [ \"$triple_allowed\" = 1 ]; then\n"+
                "  if [ ! -f \"$saved\" ]; then mkdir -p /data/adb/thor-native-fg; printf '%s %s\\n' \"$(settings get system peak_refresh_rate)\" \"$(settings get system min_refresh_rate)\" > \"$saved\"; fi\n"+
                "  log -t ThorFgSvc \"refresh hold on target=$game_target\"\n  [ \"$(settings get system peak_refresh_rate)\" = 120.0 ] || settings put system peak_refresh_rate 120.0\n"+
                "  [ \"$(settings get system min_refresh_rate)\" = 120.0 ] || settings put system min_refresh_rate 120.0\n"+
                " elif [ -f \"$saved\" ]; then\n"+
                "  read prev_peak prev_min < \"$saved\"\n"+
                "  case \"$prev_peak\" in ''|null) settings delete system peak_refresh_rate;; *) settings put system peak_refresh_rate \"$prev_peak\";; esac\n"+
                "  case \"$prev_min\" in ''|null) settings delete system min_refresh_rate;; *) settings put system min_refresh_rate \"$prev_min\";; esac\n"+
                "  log -t ThorFgSvc \"refresh hold restored\"\n  rm -f \"$saved\"\n"+
                " fi\n"+
                " sleep 2\n done ) &\npoll_pid=$!\n"+
                "trap 'kill \"$log_pid\" \"$poll_pid\" 2>/dev/null' EXIT HUP INT TERM\nIFS= read -r stop_command\n";
            java.lang.Process process=new ProcessBuilder("su","-c",monitor).redirectErrorStream(true).start();logProcess=process;
            if(stopped){process.getOutputStream().close();return;}
            try(BufferedReader reader=new BufferedReader(new InputStreamReader(process.getInputStream()))){String line;while(!stopped&&(line=reader.readLine())!=null){
                if(line.startsWith("THOR_CONFIG ")){readConfig(line);continue;}
                RuntimeMetrics next=RuntimeMetrics.parse(line,SystemClock.elapsedRealtime());if(next!=null&&next.pid==targetPid)metrics=next;
            }}
            if(!stopped){process.waitFor();error="Kết nối log engine đã ngắt";}
        }catch(Exception failure){if(!stopped)error="Chưa đọc được log engine";}
    }
    private final Runnable refresh=new Runnable(){public void run(){
        if(stopped||hud==null)return;
        RuntimeMetrics value=metrics;
        // Keep no overlay window at boot/on ordinary apps. Attaching a GONE
        // window still enters WindowManager/accessibility/display bookkeeping.
        long now=SystemClock.elapsedRealtime();
        showWindow(configAt>0&&now-configAt<5000&&(!automatic||(active&&targetPid>0)));
        if(!error.isEmpty())hud.setText("Gốc — | FG — | SR —");
        else if(!active)hud.setText("Gốc — | FG TẮT | SR TẮT");
        else if(targetPid==0)hud.setText("Gốc — | FG — | SR —");
        else if(value==null||!value.fresh(SystemClock.elapsedRealtime()))hud.setText("Gốc — | FG — | SR —");
        else hud.setText(value.fpsOnly()+" · "+aaLabel()+" · "+temperature());
        main.postDelayed(this,500);
    }};
    @Override public int onStartCommand(Intent intent,int flags,int id){if(intent!=null&&"HIDE".equals(intent.getAction())){getPreferences().edit().putBoolean("hud_visible",false).apply();stopSelf();return START_NOT_STICKY;}if(hud==null){stopSelf();return START_NOT_STICKY;}return START_STICKY;}
    @Override public IBinder onBind(Intent intent){return null;}
    @Override public void onDestroy(){stopped=true;main.removeCallbacksAndMessages(null);java.lang.Process process=logProcess;if(process!=null){try{process.getOutputStream().write("STOP\n".getBytes("UTF-8"));process.getOutputStream().flush();process.getOutputStream().close();}catch(IOException ignored){process.destroy();}}if(hud!=null&&attached)showWindow(false);hud=null;stopForeground(STOP_FOREGROUND_REMOVE);super.onDestroy();}
}
