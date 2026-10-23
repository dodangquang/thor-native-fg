package com.thor.nativefg;

import android.content.*;
import android.content.pm.*;
import java.io.*;
import java.util.*;

final class AutoMode {
    static final String DIRECTORY="/data/adb/thor-native-fg",FILE=DIRECTORY+"/auto.conf";
    static final class Entry {
        final String id,label;final int uid;final boolean detected;boolean selected;
        Entry(String id,String label,int uid,boolean detected,boolean selected){this.id=id;this.label=label;this.uid=uid;this.detected=detected;this.selected=selected;}
    }
    static SharedPreferences preferences(Context context){return context.getSharedPreferences("thor",Context.MODE_PRIVATE);}
    static boolean requested(Context context){return preferences(context).getBoolean("auto_enabled",false);}
    static String model(Context context){return preferences(context).getBoolean("neural_sr",false)?"neural2d":"sgsr1";}
    static synchronized String setNeural(Context context,boolean enabled)throws Exception{
        String selected=enabled?"neural2d":"sgsr1";
        RootShell.run("set -e\nsetprop persist.thorfg.sr_model "+selected+"\ntest \"$(getprop persist.thorfg.sr_model)\" = "+selected);
        preferences(context).edit().putBoolean("neural_sr",enabled).apply();
        return "Đã chọn "+(enabled?"Neural 2D giảm viền (thử nghiệm)":"GSR1")+". Lưu game và mở lại giả lập để đổi thuật toán. Lựa chọn được giữ qua reboot; SR chỉ hoạt động khi nhận được ảnh render thấp phù hợp.";
    }
    static String foregroundScript(){
        // Consume the whole dump: an early pipe close can make dumpsys emit a
        // Binder 'Broken pipe' error into the controller's merged root output.
        return "dumpsys activity activities | awk '/topResumedActivity=ActivityRecord/ && !found { for(i=1;i<=NF;i++) if($i ~ /^[A-Za-z0-9_.]+\\//) {split($i,part,\"/\"); foreground=part[1]; found=1; break} } END {if(found) print foreground}'";
    }
    static String allowedScript(){return "awk -F: -v package_name=\"$game_target\" '$1 ~ /^app=[0-9]+$/ && $2==package_name {matched=1} END {exit !matched}' "+FILE;}
    static List<Entry> applications(Context context){
        SharedPreferences prefs=preferences(context);Set<String> includes=prefs.getStringSet("auto_include",Collections.emptySet()),excludes=prefs.getStringSet("auto_exclude",Collections.emptySet());
        PackageManager pm=context.getPackageManager();Map<String,Entry> unique=new TreeMap<>();
        Set<String> homes=new HashSet<>();for(ResolveInfo home:pm.queryIntentActivities(new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_HOME),0))homes.add(home.activityInfo.packageName);
        for(ResolveInfo resolved:pm.queryIntentActivities(new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LAUNCHER),0)){
            ApplicationInfo app=resolved.activityInfo.applicationInfo;String id=app.packageName;
            if(id.startsWith("com.thor.nativefg")||!GameRules.validPackage(id)||(app.flags&ApplicationInfo.FLAG_SYSTEM)!=0||app.uid%100000<10000||app.uid%100000>=90000)continue;
            boolean detected=GameRules.detected(id,app.category==ApplicationInfo.CATEGORY_GAME||(app.flags&ApplicationInfo.FLAG_IS_GAME)!=0,homes.contains(id));
            unique.put(id,new Entry(id,resolved.loadLabel(pm).toString(),app.uid,detected,GameRules.selected(id,detected,includes,excludes)));
        }
        ArrayList<Entry> result=new ArrayList<>(unique.values());result.sort((a,b)->a.selected!=b.selected?(a.selected?-1:1):a.label.compareToIgnoreCase(b.label));return result;
    }
    static void saveSelection(Context context,List<Entry> entries){
        Set<String> include=new HashSet<>(preferences(context).getStringSet("auto_include",Collections.emptySet())),exclude=new HashSet<>(preferences(context).getStringSet("auto_exclude",Collections.emptySet()));
        for(Entry entry:entries){include.remove(entry.id);exclude.remove(entry.id);if(entry.selected&&!entry.detected)include.add(entry.id);if(!entry.selected&&entry.detected)exclude.add(entry.id);}
        preferences(context).edit().putStringSet("auto_include",include).putStringSet("auto_exclude",exclude).apply();
    }
    static synchronized String configure(Context context,boolean enabled,boolean sr)throws Exception{
        String provider=enabled?RuntimeProvider.ensure(context):context.getApplicationInfo().nativeLibraryDir;
        if(!provider.startsWith("/data/app/")||!provider.endsWith("/lib/arm64")||provider.contains("\n"))throw new IOException("Đường dẫn provider ARM64 không hợp lệ");
        StringBuilder config=new StringBuilder("THOR_AUTO_V1\nenabled=").append(enabled?'1':'0').append("\nsr=").append(sr?'1':'0').append("\nprovider=").append(provider).append('\n');
        int count=0;for(Entry entry:applications(context))if(entry.selected){config.append("app=").append(entry.uid).append(':').append(entry.id).append('\n');count++;}
        if(config.length()>65536||count>512)throw new IOException("Danh sách tự động quá lớn");
        String module="/data/adb/modules/thor_native_fg_bootstrap";
        String script="set -e\n"+(enabled?"test -f "+module+"/module.prop\ntest ! -f "+module+"/disable\n":"")+"test -f "+RootShell.quote(provider+"/libVkLayer_ThorFG.so")+
            "\nmkdir -p "+DIRECTORY+"\nchmod 700 "+DIRECTORY+"\numask 077\nprintf %s "+RootShell.quote(config.toString())+" > "+FILE+".tmp\nchmod 600 "+FILE+".tmp\nchown 0:0 "+FILE+".tmp\nmv -f "+FILE+".tmp "+FILE+
            "\nsetprop persist.thorfg.sr_model "+model(context)+"\nsetprop debug.thorfg.auto_sr "+(sr?"1":"0")+"\nsetprop debug.thorfg.auto "+(enabled?"1":"0")+"\ngetprop sys.thorfg.bootstrap\n";
        String version=RootShell.run(script);preferences(context).edit().putBoolean("auto_enabled",enabled).putBoolean("upscale",sr).apply();
        if(!enabled)return "Đã tắt tự động. Layer đã nạp sẽ ngừng FG; lần mở mới không được tự nạp nữa. Chế độ bật thủ công có cấu hình riêng.";
        if(!version.equals("2")&&!version.equals("3"))return "Đã lưu tự động cho "+count+" ứng dụng. Cần module bootstrap 0.2 trở lên và khởi động lại máy để nạp bộ kích hoạt mới. Chưa xác nhận tự động hoạt động. Không tự đóng game đang chạy.";
        return "Đã bật tự động cho "+count+" game/giả lập. Mở ứng dụng từ launcher bình thường; chọn Vulkan. Ứng dụng đang chạy cần lưu và thoát/mở lại một lần để nạp layer. Ô FPS chỉ hiện khi ứng dụng trong danh sách đang ở trước.";
    }
    static synchronized void refresh(Context context)throws Exception{if(requested(context))configure(context,true,preferences(context).getBoolean("upscale",true));}
}
