package com.thor.nativefg;

import android.content.Context;
import android.content.pm.*;
import java.io.*;
import java.util.UUID;

/** Root-only, stopped-application preference transaction; never restores game data. */
final class DroidDeckPreset {
    static final String PACKAGE="com.droiddeck.launcher",PREFS="/data/user/0/"+PACKAGE+"/shared_prefs";
    static final String BASE=AutoMode.DIRECTORY+"/droiddeck-preset-v1";
    static String q(String s){return RootShell.quote(s);}
    static String run(String script)throws Exception{return RootShell.runMaster(script);}
    static String stopped(int uid){return "process_uids=$(ps -A -o UID)\nif printf '%s\\n' \"$process_uids\" | awk '$1=="+uid+" {found=1} END {exit !found}'; then echo 'Lưu game rồi thoát hẳn DroidDeck trước khi đổi preset.'; exit 1; fi\n";}
    static String hash(String file){return "$(sha256sum "+q(file)+" | cut -d ' ' -f1)";}
    static String same(String a,String b){return "test \""+hash(a)+"\" = \""+hash(b)+"\" || { echo 'Cấu hình DroidDeck đã đổi trong lúc đọc; chưa ghi preset.'; exit 1; }\n";}
    static String read(String path)throws Exception{return run("cat "+q(path));}
    static String files(){return "session.xml frame_gen.xml";}
    static String snapshot(String dest){
        return "mkdir -m 700 "+q(dest)+"\nfor file in "+files()+"; do\n src="+q(PREFS)+"/\"$file\"\n if [ -L \"$src\" ] || [ -e \"$src.bak\" ] || [ ! -f \"$src\" ]; then echo \"Không đọc được preference ổn định: $src\"; id; ls -ldZ \"$src\" \"$src.bak\" 2>&1 || true; exit 1; fi\n cp -p \"$src\" "+q(dest)+"/\"$file\"\ndone\n";
    }
    static String stage(String file,String data,int uid){
        String src=PREFS+"/"+file,tmp=src+".thor-next";
        // cp/chcon preserve the target application's SELinux category as well as uid/mode.
        return "test ! -e "+q(tmp)+"\ncp -p "+q(src)+" "+q(tmp)+"\nprintf %s "+q(data)+" > "+q(tmp)+"\nchown "+uid+":"+uid+" "+q(tmp)+"\nchmod 660 "+q(tmp)+"\nchcon \"$(ls -Zd "+q(src)+" | awk '{print $1}')\" "+q(tmp)+"\n";
    }
    static synchronized String change(Context context,boolean restore)throws Exception{
        PackageInfo app=context.getPackageManager().getPackageInfo(PACKAGE,0);
        // The three preferences this preset owns still exist in0.6.0 with the same
        // types, so both builds are supported; anything else is refused instead of
        // guessing at an unknown schema.
        boolean supported=("0.5.0".equals(app.versionName)&&app.getLongVersionCode()==14)||("0.6.0".equals(app.versionName)&&app.getLongVersionCode()==15);
        if(!supported||app.applicationInfo.uid<10000||app.applicationInfo.uid>=100000)throw new IOException("Preset mới xác nhận với DroidDeck 0.5.0 (14) hoặc 0.6.0 (15) ở hồ sơ chính, chưa hỗ trợ phiên bản này.");
        int uid=app.applicationInfo.uid;
        String tx=AutoMode.DIRECTORY+"/preset-tx-"+UUID.randomUUID().toString();
        // Keep originals in root-private storage, not app-readable /data/local/tmp.
        // The controller has one process and this method is synchronized. Do not
        // leave a filesystem lock that would block recovery after process death.
        run("set -e\numask 077\nmkdir -p "+AutoMode.DIRECTORY+"\nchmod 700 "+AutoMode.DIRECTORY+"\n");
        boolean txCreated=false;
        try{
            txCreated=true;
            run("set -e\n"+stopped(uid)+"rm -f "+q(PREFS+"/session.xml.thor-next")+" "+q(PREFS+"/frame_gen.xml.thor-next")+"\n"+snapshot(tx));
            String session=read(tx+"/session.xml"),fg=read(tx+"/frame_gen.xml");
            String exists=run("if [ -f "+q(BASE+"/ready")+" ]; then echo yes; fi");
            DroidDeckSettings.Pair next;
            if(restore){
                if(!exists.equals("yes"))throw new IOException("Chưa có preset đã lưu để hoàn tác.");
                next=DroidDeckSettings.restore(read(BASE+"/session.xml"),read(BASE+"/frame_gen.xml"),session,fg);
            }else{
                if(exists.equals("yes")){
                    if(DroidDeckSettings.active(session,fg))return "Preset Thor SR đã có. Chọn GSR1/Neural trong app, bật FG rồi mở DroidDeck; trạng thái SR cần có pass thật.";
                    throw new IOException("Đã có bản gốc của preset trước. Hoàn tác trước khi áp dụng lại để giữ đúng cấu hình của bạn.");
                }
                next=DroidDeckSettings.apply(session,fg);
            }
            String script="set -e\numask 077\n"+stopped(uid)+same(PREFS+"/session.xml",tx+"/session.xml")+same(PREFS+"/frame_gen.xml",tx+"/frame_gen.xml");
            if(!restore)script+="test ! -e "+q(BASE)+"\nmkdir -m 700 "+q(tx+"/original-pending")+"\ncp -p "+q(tx+"/session.xml")+" "+q(tx+"/original-pending/session.xml")+"\ncp -p "+q(tx+"/frame_gen.xml")+" "+q(tx+"/original-pending/frame_gen.xml")+"\nprintf %s 1 > "+q(tx+"/original-pending/ready")+"\n";
            // Staging failures do not change either live file. A journal is retained if
            // interruption occurs after the first rename, so recovery is still possible.
            script+=stage("session.xml",next.session,uid)+stage("frame_gen.xml",next.fg,uid)+stopped(uid);
            for(String file:new String[]{"session.xml","frame_gen.xml"})script+=same(PREFS+"/"+file,tx+"/"+file);
            if(!restore)script+="mv "+q(tx+"/original-pending")+" "+q(BASE)+"\n";
            script+="printf %s pending > "+q(tx+"/journal")+"\n";
            for(String file:new String[]{"session.xml","frame_gen.xml"})script+="mv -f "+q(PREFS+"/"+file+".thor-next")+" "+q(PREFS+"/"+file)+"\n";
            script+="printf %s committed > "+q(tx+"/journal")+"\n";
            if(restore)script+="mv "+q(BASE)+" "+q(tx+"/original-restored")+"\n";
            run(script);
            return restore?"Đã hoàn tác 3 mục của preset DroidDeck; giữ các cài đặt khác đã đổi sau đó. Không đụng dữ liệu game. FG Thor giữ chế độ hiện tại.":"Đã chọn linear, trung hòa hiệu ứng và tắt FG tích hợp của DroidDeck. Bản gốc đã lưu để hoàn tác. Chọn GSR1/Neural và bật FG Thor, rồi mở game ở độ phân giải thấp (ví dụ 1280×720). Không tự đổi độ phân giải game; SR chỉ ON khi nhận được ảnh thấp phù hợp.";
        }finally{
            // Retain a pending transaction rather than destroying evidence after a
            // partially committed pair. Completed snapshots can be safely removed.
            String cleanup="set -e\n";
            if(txCreated)cleanup+="if [ ! -f "+q(tx+"/journal")+" ] || grep -qx committed "+q(tx+"/journal")+"; then rm -f "+q(tx+"/session.xml")+" "+q(tx+"/frame_gen.xml")+" "+q(tx+"/journal")+" "+q(tx+"/original-pending/session.xml")+" "+q(tx+"/original-pending/frame_gen.xml")+" "+q(tx+"/original-pending/ready")+"; rmdir "+q(tx+"/original-pending")+" 2>/dev/null || true; rmdir "+q(tx)+" 2>/dev/null || true; fi\n";
            cleanup+="rm -f "+q(PREFS+"/session.xml.thor-next")+" "+q(PREFS+"/frame_gen.xml.thor-next")+"\n";
            run(cleanup);
        }
    }
}
