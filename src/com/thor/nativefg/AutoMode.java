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
    static String model(Context context){
        String m=preferences(context).getString("sr_model","sgsr2");
        if("neural2d".equals(m)||"sgsr1".equals(m)||"sgsr2".equals(m))return m;
        return "sgsr2";
    }
    static String modelLabel(Context context){
        String m=model(context);
        return "sgsr2".equals(m)?"GSR2 (Temporal)":("neural2d".equals(m)?"Neural 2D":"GSR1");
    }
    static synchronized String setModel(Context context,String selected)throws Exception{
        if(!"neural2d".equals(selected)&&!"sgsr1".equals(selected))selected="sgsr2";
        RootShell.run("set -e\nsetprop persist.thorfg.sr_model "+selected+"\ntest \"$(getprop persist.thorfg.sr_model)\" = "+selected);
        preferences(context).edit().putString("sr_model",selected).putBoolean("neural_sr","neural2d".equals(selected)).apply();
        return "Đã chọn "+modelLabel(context)+". Lựa chọn được áp dụng ngay cho lần vào game tiếp theo và giữ qua reboot.";
    }
    static synchronized String setNeural(Context context,boolean enabled)throws Exception{
        return setModel(context,enabled?"neural2d":"sgsr2");
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
    static String syncEdenScript(){
        return "n_ext=/data/media/0/Android/data/dev.eden.eden_emulator.nightly/files\n"+
            "r_ext=/data/media/0/Android/data/dev.eden.eden_emulator.relWithDebInfo/files\n"+
            "if [ -d \"$n_ext\" ]; then\n"+
            " if [ -f \"$n_ext/tas\" ]; then rm -f \"$n_ext/tas\"; fi\n"+
            " mkdir -p \"$n_ext/tas\" \"$n_ext/cache/shader\" \"$r_ext\"\n"+
            " chgrp -R 1078 \"$n_ext\" 2>/dev/null || true\n"+
            " chmod -R g+rwX \"$n_ext\" 2>/dev/null || true\n"+
            " find \"$n_ext\" -type d -exec chmod 2770 {} + 2>/dev/null || true\n"+
            " for b in /data/media/0 /mnt/pass_through/0/emulated/0 /mnt/user/0/emulated/0 /mnt/installer/0/emulated/0 /mnt/androidwritable/0/emulated/0 /storage/emulated/0; do\n"+
            "  t=\"$b/Android/data/dev.eden.eden_emulator.relWithDebInfo/files\"\n"+
            "  if [ -d \"$t\" ] && [ \"$(stat -c %i \"$t\" 2>/dev/null)\" != \"$(stat -c %i \"$n_ext\" 2>/dev/null)\" ]; then mount --bind \"$n_ext\" \"$t\" 2>/dev/null || true; fi\n"+
            " done\n"+
            " appops set dev.eden.eden_emulator.relWithDebInfo MANAGE_EXTERNAL_STORAGE allow 2>/dev/null || true\n"+
            " r_uid=$(stat -c %u /data/user/0/dev.eden.eden_emulator.relWithDebInfo 2>/dev/null || true)\n"+
            " if [ -n \"$r_uid\" ] && [ -d /data/user/0/dev.eden.eden_emulator.nightly/files/gpu_driver ]; then\n"+
            "  mkdir -p /data/user/0/dev.eden.eden_emulator.relWithDebInfo/files/gpu_driver\n"+
            "  cp -Rf /data/user/0/dev.eden.eden_emulator.nightly/files/gpu_driver/. /data/user/0/dev.eden.eden_emulator.relWithDebInfo/files/gpu_driver/ 2>/dev/null || true\n"+
            "  chown -R \"$r_uid:$r_uid\" /data/user/0/dev.eden.eden_emulator.relWithDebInfo/files/gpu_driver 2>/dev/null || true\n"+
            "  restorecon -RF /data/user/0/dev.eden.eden_emulator.relWithDebInfo/files/gpu_driver 2>/dev/null || true\n"+
            " fi\n"+
            " if [ -n \"$r_uid\" ] && [ -f /data/user/0/dev.eden.eden_emulator.nightly/shared_prefs/dev.eden.eden_emulator.nightly_preferences.xml ]; then\n"+
            "  mkdir -p /data/user/0/dev.eden.eden_emulator.relWithDebInfo/shared_prefs\n"+
            "  cp -f /data/user/0/dev.eden.eden_emulator.nightly/shared_prefs/dev.eden.eden_emulator.nightly_preferences.xml /data/user/0/dev.eden.eden_emulator.relWithDebInfo/shared_prefs/dev.eden.eden_emulator.relWithDebInfo_preferences.xml 2>/dev/null || true\n"+
            "  chown -R \"$r_uid:$r_uid\" /data/user/0/dev.eden.eden_emulator.relWithDebInfo/shared_prefs 2>/dev/null || true\n"+
            " fi\n"+
            "fi\n";
    }
    static synchronized String configure(Context context,boolean enabled,boolean sr)throws Exception{
        String provider=enabled?RuntimeProvider.ensure(context):context.getApplicationInfo().nativeLibraryDir;
        if(!provider.startsWith("/data/app/")||!provider.endsWith("/lib/arm64")||provider.contains("\n"))throw new IOException("Đường dẫn provider ARM64 không hợp lệ");
        StringBuilder config=new StringBuilder("THOR_AUTO_V1\nenabled=").append(enabled?'1':'0').append("\nsr=").append(sr?'1':'0').append("\nprovider=").append(provider).append('\n');
        int count=0;for(Entry entry:applications(context))if(entry.selected){config.append("app=").append(entry.uid).append(':').append(entry.id).append('\n');count++;}
        if(config.length()>65536||count>512)throw new IOException("Danh sách tự động quá lớn");
        String module="/data/adb/modules/thor_native_fg_bootstrap";
        String cleanupGlobal=enabled?"settings delete global enable_gpu_debug_layers >/dev/null 2>&1 || true\nsettings delete global gpu_debug_app >/dev/null 2>&1 || true\nsettings delete global gpu_debug_layers >/dev/null 2>&1 || true\nsettings delete global gpu_debug_layer_app >/dev/null 2>&1 || true\nsetprop debug.thorfg.enabled 0\nsetprop debug.thorfg.target ''\n":"";
        String script="set -e\n"+(enabled?"test -f "+module+"/module.prop\ntest ! -f "+module+"/disable\n":"")+"test -f "+RootShell.quote(provider+"/libVkLayer_ThorFG.so")+
            "\nmkdir -p "+DIRECTORY+"\nchmod 700 "+DIRECTORY+"\numask 077\nprintf %s "+RootShell.quote(config.toString())+" > "+FILE+".tmp\nchmod 600 "+FILE+".tmp\nchown 0:0 "+FILE+".tmp\nmv -f "+FILE+".tmp "+FILE+
            "\n"+cleanupGlobal+syncEdenScript()+"setprop persist.thorfg.sr_model "+model(context)+"\nsetprop debug.thorfg.auto_sr "+(sr?"1":"0")+"\nsetprop debug.thorfg.auto "+(enabled?"1":"0")+"\ngetprop sys.thorfg.bootstrap\n";
        String version=RootShell.runMaster(script);preferences(context).edit().putBoolean("auto_enabled",enabled).putBoolean("upscale",sr).apply();
        if(!enabled)return "Đã tắt tự động. Layer đã nạp sẽ ngừng FG; lần mở mới không được tự nạp nữa. Chế độ bật thủ công có cấu hình riêng.";
        if(!version.equals("2")&&!version.equals("3"))return "Đã lưu tự động cho "+count+" ứng dụng. Cần module bootstrap 0.2 trở lên và khởi động lại máy để nạp bộ kích hoạt mới. Chưa xác nhận tự động hoạt động. Không tự đóng game đang chạy.";
        return "Đã bật tự động cho "+count+" game/giả lập. Mở ứng dụng từ launcher bình thường; chọn Vulkan. Ứng dụng đang chạy cần lưu và thoát/mở lại một lần để nạp layer. Ô FPS chỉ hiện khi ứng dụng trong danh sách đang ở trước.";
    }
    static synchronized void refresh(Context context)throws Exception{if(requested(context))configure(context,true,preferences(context).getBoolean("upscale",true));}
}
