package com.thor.nativefg;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageInfo;
import android.content.pm.PackageInstaller;
import android.content.pm.PackageManager;
import android.content.pm.Signature;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.provider.Settings;

import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.security.MessageDigest;
import java.util.HashSet;
import java.util.Locale;
import java.util.Set;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * Same-package signed APK updates delivered through git.
 *
 * The manifest lives in the public repository (raw.githubusercontent) and the APK
 * is a GitHub Release asset, so publishing is: bump version, build+sign, upload the
 * asset, commit the JSON. Nothing here trusts the metadata: the downloaded APK must
 * match the declared size, SHA-256, package, version code and the signing
 * certificate of the installed app before the installer is even opened.
 */
final class AppUpdates {
    static final int IDLE=0,CHECKING=1,CURRENT=2,AVAILABLE=3,DOWNLOADING=4,READY=5,ERROR=6,VERIFYING=7;
    private static final String PREFS="thorfg_updates";
    private static final ExecutorService AUTO=Executors.newSingleThreadExecutor();
    private static boolean autoBusy;
    private static long attempted;

    private final Context context;
    private final Runnable changed;
    private final ExecutorService io=Executors.newSingleThreadExecutor();
    private final Handler ui=new Handler(Looper.getMainLooper());
    private volatile boolean closed,cancelled;
    private volatile HttpURLConnection active;

    int state=IDLE;
    String message="",version="",notes="";
    long done,total;
    private UpdateSpec release;

    AppUpdates(Context context,Runnable changed){this.context=context.getApplicationContext();this.changed=changed;}

    private static boolean english(){return !Locale.getDefault().getLanguage().startsWith("vi");}
    private String t(String vi,String en){return english()?en:vi;}

    private static int flags(){return Build.VERSION.SDK_INT>=28?PackageManager.GET_SIGNING_CERTIFICATES:PackageManager.GET_SIGNATURES;}
    private static long code(PackageInfo info){return Build.VERSION.SDK_INT>=28?info.getLongVersionCode():info.versionCode;}
    private static PackageInfo installed(Context c)throws Exception{return c.getPackageManager().getPackageInfo(c.getPackageName(),flags());}

    String installedVersion(){try{return installed(context).versionName;}catch(Exception e){return "?";}}

    private File directory(){File f=new File(context.getFilesDir(),"thorfg-update");f.mkdirs();return f;}
    private File apk(){return new File(directory(),"update.apk");}
    private void post(Runnable action){ui.post(()->{if(!closed){action.run();changed.run();}});}

    boolean busy(){return state==CHECKING||state==DOWNLOADING||state==VERIFYING;}

    static boolean hasUpdate(Context c){
        try{return c.getSharedPreferences(PREFS,0).getLong("offer_code",0)>code(installed(c));}catch(Exception e){return false;}
    }

    /** Silent, rate-limited check that only offers, never installs. */
    static synchronized void autoCheck(Activity activity){
        if(autoBusy||activity.isFinishing()||activity.isDestroyed())return;
        long now=SystemClock.elapsedRealtime();
        if(attempted!=0&&now-attempted<120000)return;
        Context c=activity.getApplicationContext();
        long age=System.currentTimeMillis()-c.getSharedPreferences(PREFS,0).getLong("checked_at",0);
        if(age>=0&&age<6*3600000L)return;
        autoBusy=true;attempted=now;
        java.lang.ref.WeakReference<Activity> ref=new java.lang.ref.WeakReference<>(activity);
        AUTO.execute(()->{
            try{
                UpdateSpec spec=parse(c,readManifest(c));
                if(spec.minSdk>Build.VERSION.SDK_INT)return;
                c.getSharedPreferences(PREFS,0).edit().putLong("checked_at",System.currentTimeMillis()).putLong("offer_code",spec.versionCode).apply();
                if(spec.versionCode<=code(installed(c)))return;
                new Handler(Looper.getMainLooper()).post(()->{
                    Activity current=ref.get();
                    if(current==null||current.isFinishing()||current.isDestroyed()||!current.hasWindowFocus())return;
                    if(c.getSharedPreferences(PREFS,0).getLong("notified_code",0)==spec.versionCode)return;
                    c.getSharedPreferences(PREFS,0).edit().putLong("notified_code",spec.versionCode).apply();
                    boolean en=english();
                    new AlertDialog.Builder(current)
                        .setTitle(en?"Update available":"Có bản cập nhật")
                        .setMessage(spec.versionName+"\n\n"+(en?"Download and verify the signed APK before installing over this app.":"Tải và kiểm tra chữ ký APK trước khi cài đè ứng dụng này."))
                        .setNegativeButton(en?"Later":"Để sau",null)
                        .setPositiveButton(en?"View":"Xem",(d,w)->((MainActivity)current).openUpdateDialog()).show();
                });
            }catch(Exception ignored){
                // An offline or malformed feed never affects licensing or saved data.
            }finally{synchronized(AppUpdates.class){autoBusy=false;}}
        });
    }

    static UpdateSpec parse(Context c,JSONObject json)throws Exception{
        if(json.getInt("schemaVersion")!=1||!"full".equals(json.getString("channel")))throw new IOException("Wrong update channel");
        return new UpdateSpec(UpdateSpec.PACKAGE,json.getString("packageName"),json.getLong("versionCode"),json.getString("versionName"),
            json.getInt("minSdk"),json.getLong("bytes"),json.getString("sha256"),json.getString("apkUrl"),
            json.optString(english()?"notesEn":"notesVi",""));
    }

    static JSONObject readManifest(Context c)throws Exception{
        HttpURLConnection conn=connect(new URL(UpdateSpec.manifest()),false);
        try{
            if(conn.getContentLengthLong()>65536)throw new IOException("Update metadata too large");
            ByteArrayOutputStream out=new ByteArrayOutputStream();
            try(InputStream in=conn.getInputStream()){
                byte[] buffer=new byte[4096];int n;
                while((n=in.read(buffer))!=-1){
                    if(out.size()+n>65536)throw new IOException("Update metadata too large");
                    out.write(buffer,0,n);
                }
            }
            return new JSONObject(new String(out.toByteArray(),StandardCharsets.UTF_8));
        }finally{conn.disconnect();}
    }

    static HttpURLConnection connect(URL url,boolean binary)throws Exception{
        for(int attempt=0;attempt<5;attempt++){
            String host=url.getHost();
            boolean trusted=binary
                ?(host.equals("github.com")||host.equals("release-assets.githubusercontent.com")||host.equals("objects.githubusercontent.com"))
                :UpdateSpec.trustedManifest(url.toExternalForm());
            if(!"https".equals(url.getProtocol())||url.getUserInfo()!=null||url.getRef()!=null||url.getPort()!=-1||!trusted)throw new IOException("Untrusted update host");
            HttpURLConnection conn=(HttpURLConnection)url.openConnection();
            conn.setInstanceFollowRedirects(false);conn.setConnectTimeout(15000);conn.setReadTimeout(30000);conn.setUseCaches(false);
            conn.setRequestProperty("Accept",binary?"application/octet-stream":"application/json");
            conn.setRequestProperty("User-Agent","ThorNativeFG-Updater/1");
            conn.setRequestProperty("Cache-Control","no-cache");
            int status=conn.getResponseCode();
            if(status>=300&&status<400){
                String location=conn.getHeaderField("Location");conn.disconnect();
                if(!binary||location==null)throw new IOException("Update redirect refused");
                url=new URL(url,location);continue;
            }
            if(status!=200){conn.disconnect();throw new IOException("Update server HTTP "+status);}
            return conn;
        }
        throw new IOException("Too many update redirects");
    }

    void check(){
        if(busy()||closed)return;
        state=CHECKING;message=t("Đang kiểm tra bản mới…","Checking for updates…");changed.run();
        io.execute(()->{
            try{
                UpdateSpec candidate=parse(context,readManifest(context));
                long current=code(installed(context));
                if(candidate.versionCode>current&&candidate.minSdk>Build.VERSION.SDK_INT)throw new IOException("API "+candidate.minSdk);
                boolean cached=false;
                if(candidate.versionCode>current&&apk().isFile()){
                    try{verify(apk(),candidate);cached=true;}catch(Exception ignored){}
                }
                final boolean ready=cached;
                post(()->{
                    release=candidate;version=candidate.versionName;notes=candidate.notes;
                    total=candidate.bytes;done=ready?total:0;
                    state=candidate.versionCode<=current?CURRENT:(ready?READY:AVAILABLE);
                    message=state==CURRENT?t("Bạn đang dùng bản mới nhất.","You are on the latest version.")
                        :ready?t("APK đã tải và kiểm tra trước đó. Sẵn sàng cập nhật.","APK already downloaded and verified. Ready to install.")
                        :t("Có bản mới. Chỉ tải khi bạn bấm cập nhật.","An update is available. Download starts when you choose.");
                    context.getSharedPreferences(PREFS,0).edit().putLong("checked_at",System.currentTimeMillis()).putLong("offer_code",candidate.versionCode).apply();
                });
            }catch(Exception e){
                post(()->{state=ERROR;message=t("Chưa kiểm tra được cập nhật. Kiểm tra mạng rồi thử lại.","Could not check for updates. Check your connection and retry.");});
            }
        });
    }

    void download(){
        if(state!=AVAILABLE||release==null||closed)return;
        final UpdateSpec candidate=release;
        state=DOWNLOADING;cancelled=false;done=0;total=candidate.bytes;
        message=t("Đang tải APK…","Downloading the APK…");changed.run();
        io.execute(()->{
            File part=new File(directory(),"update.part");
            try{
                HttpURLConnection conn=connect(new URL(candidate.url),true);
                active=conn;long received=0,last=0;
                try{
                    if(conn.getContentLengthLong()>candidate.bytes)throw new IOException("APK exceeds declared size");
                    try(InputStream in=conn.getInputStream();OutputStream out=new FileOutputStream(part)){
                        byte[] buffer=new byte[65536];int n;
                        while((n=in.read(buffer))!=-1){
                            if(cancelled||closed)throw new IOException("Cancelled");
                            received+=n;
                            if(received>candidate.bytes)throw new IOException("APK exceeds declared size");
                            out.write(buffer,0,n);
                            long now=SystemClock.elapsedRealtime();
                            if(now-last>=100||received==candidate.bytes){last=now;final long progress=received;post(()->done=progress);}
                        }
                    }
                }finally{active=null;conn.disconnect();}
                if(received!=candidate.bytes||cancelled||closed)throw new IOException("Incomplete APK");
                post(()->{state=VERIFYING;message=t("Đang kiểm tra SHA-256 và chữ ký…","Verifying SHA-256 and signing certificate…");});
                verify(part,candidate);
                if(cancelled||closed)throw new IOException("Cancelled");
                Files.move(part.toPath(),apk().toPath(),StandardCopyOption.REPLACE_EXISTING);
                post(()->{state=READY;done=total;message=t("Đã xác minh đúng ứng dụng, phiên bản, SHA-256 và chữ ký.","App, version, SHA-256 and signing certificate verified.");});
            }catch(Exception e){
                post(()->{
                    state=cancelled?AVAILABLE:ERROR;
                    message=cancelled?t("Đã huỷ tải cập nhật.","Update download cancelled.")
                        :t("Không tải hoặc xác minh được APK. Thử lại sau.","Could not download or verify the APK. Please retry.");
                });
            }finally{part.delete();}
        });
    }

    static String digest(File file)throws Exception{
        MessageDigest hash=MessageDigest.getInstance("SHA-256");
        try(InputStream in=new FileInputStream(file)){byte[] buffer=new byte[65536];int n;while((n=in.read(buffer))!=-1)hash.update(buffer,0,n);}
        return hex(hash.digest());
    }

    static String hex(byte[] bytes){StringBuilder out=new StringBuilder(bytes.length*2);for(byte b:bytes)out.append(String.format(Locale.ROOT,"%02x",b&255));return out.toString();}

    static Set<String> signatures(PackageInfo info)throws Exception{
        Signature[] values=Build.VERSION.SDK_INT>=28
            ?(info.signingInfo==null?null:info.signingInfo.getApkContentsSigners())
            :info.signatures;
        if(values==null||values.length==0)throw new IOException("Unsigned APK");
        Set<String> result=new HashSet<>();
        for(Signature value:values)result.add(hex(MessageDigest.getInstance("SHA-256").digest(value.toByteArray())));
        return result;
    }

    void verify(File file,UpdateSpec spec)throws Exception{
        if(!UpdateSpec.PACKAGE.equals(spec.packageName))throw new IOException("Wrong package");
        if(file.length()!=spec.bytes)throw new IOException("APK size mismatch");
        if(!digest(file).equals(spec.sha256))throw new IOException("APK digest mismatch");
        PackageInfo found=context.getPackageManager().getPackageArchiveInfo(file.getAbsolutePath(),flags());
        PackageInfo current=installed(context);
        if(found==null)throw new IOException("Unreadable APK");
        if(!UpdateSpec.PACKAGE.equals(found.packageName))throw new IOException("Wrong app");
        if(code(found)!=spec.versionCode)throw new IOException("Wrong version code");
        if(code(found)<=code(current))throw new IOException("Not newer than installed");
        if(!spec.versionName.equals(found.versionName))throw new IOException("Wrong version name");
        if(found.applicationInfo==null||found.applicationInfo.minSdkVersion!=spec.minSdk)throw new IOException("Wrong minSdk");
        if(spec.minSdk>Build.VERSION.SDK_INT)throw new IOException("Requires newer Android");
        if(!signatures(found).equals(signatures(current)))throw new IOException("Signing certificate mismatch");
    }

    /**
     * Installs through a PackageInstaller session. No FileProvider or res/ resources
     * are involved, which keeps the aapt-only build of this project unchanged.
     */
    void install(Activity activity){
        if(state!=READY||release==null||closed)return;
        final UpdateSpec candidate=release;
        state=VERIFYING;message=t("Kiểm tra lại APK trước khi cài…","Rechecking the APK before installing…");changed.run();
        io.execute(()->{
            try{
                verify(apk(),candidate);
                post(()->{
                    state=READY;
                    if(activity.isFinishing()||activity.isDestroyed())return;
                    try{
                        if(Build.VERSION.SDK_INT>=26&&!activity.getPackageManager().canRequestPackageInstalls()){
                            activity.startActivity(new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,android.net.Uri.parse("package:"+activity.getPackageName())));
                            message=t("Cho phép cài ứng dụng từ nguồn này, quay lại và bấm cài lại.","Allow installs from this source, come back and install again.");
                            return;
                        }
                        installSession(activity,apk());
                        message=t("Xác nhận cập nhật trong trình cài đặt Android.","Confirm the update in the Android installer.");
                    }catch(Exception e){
                        state=ERROR;
                        message=t("Chưa mở được trình cài đặt Android.","Could not open the Android installer.");
                    }
                });
            }catch(Exception e){
                post(()->{state=ERROR;message=t("APK đã lưu không còn hợp lệ. Hãy tải lại.","The saved APK is no longer valid. Download it again.");});
            }
        });
    }

    private void installSession(Context context,File file)throws Exception{
        PackageInstaller installer=context.getPackageManager().getPackageInstaller();
        PackageInstaller.SessionParams params=new PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL);
        params.setAppPackageName(UpdateSpec.PACKAGE);
        int id=installer.createSession(params);
        try(PackageInstaller.Session session=installer.openSession(id)){
            try(OutputStream out=session.openWrite("base.apk",0,file.length());InputStream in=new FileInputStream(file)){
                byte[] buffer=new byte[65536];int n;
                while((n=in.read(buffer))!=-1)out.write(buffer,0,n);
                session.fsync(out);
            }
            Intent callback=new Intent(context,UpdateResultReceiver.class).setAction(UpdateResultReceiver.ACTION);
            int flags=PendingIntent.FLAG_UPDATE_CURRENT|(Build.VERSION.SDK_INT>=31?PendingIntent.FLAG_MUTABLE:0);
            PendingIntent pending=PendingIntent.getBroadcast(context,id,callback,flags);
            session.commit(pending.getIntentSender());
        }
    }

    void cancel(){cancelled=true;HttpURLConnection conn=active;if(conn!=null)conn.disconnect();}
    void close(){closed=true;cancel();io.shutdownNow();ui.removeCallbacksAndMessages(null);}
}
