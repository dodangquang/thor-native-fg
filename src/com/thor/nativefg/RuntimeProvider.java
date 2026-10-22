package com.thor.nativefg;

import android.content.Context;
import android.content.pm.*;
import java.io.*;
import java.security.MessageDigest;

/** Immutable, signed runtime packages keep a bound game's layer path alive
 * across controller updates. A different engine binary gets a different package;
 * never replace or delete a runtime that a running process may still reference. */
final class RuntimeProvider {
    static synchronized String ensure(Context context)throws Exception {
        PackageManager pm=context.getPackageManager();
        ApplicationInfo provider=null;
        try{provider=pm.getApplicationInfo(RuntimeBuild.PACKAGE,0);}catch(PackageManager.NameNotFoundException missing){
            File apk=new File(context.getCacheDir(),"thor-runtime.apk");
            try(InputStream input=context.getAssets().open("thor-runtime.apk");FileOutputStream output=new FileOutputStream(apk)){
                byte[] buffer=new byte[32768];int n;while((n=input.read(buffer))!=-1)output.write(buffer,0,n);output.getFD().sync();
            }
            String staged="/data/local/tmp/thor-native-fg-runtime-"+RuntimeBuild.SHA256+".apk";
            try{
                RootShell.run("set -e\ntrap 'rm -f "+staged+"' EXIT\ncp "+RootShell.quote(apk.getAbsolutePath())+" "+staged+"\nchmod 644 "+staged+"\npm install --force-queryable "+staged);
            }finally{apk.delete();}
            provider=pm.getApplicationInfo(RuntimeBuild.PACKAGE,0);
        }
        if(pm.checkSignatures(context.getPackageName(),RuntimeBuild.PACKAGE)!=PackageManager.SIGNATURE_MATCH)throw new IOException("Runtime khác chữ ký của ứng dụng Thor");
        String path=provider.nativeLibraryDir;
        if(path==null||!path.startsWith("/data/app/")||!path.endsWith("/lib/arm64"))throw new IOException("Runtime ARM64 không hợp lệ");
        MessageDigest digest=MessageDigest.getInstance("SHA-256");
        try(InputStream input=new FileInputStream(new File(path,"libVkLayer_ThorFG.so"))){byte[] buffer=new byte[32768];int n;while((n=input.read(buffer))!=-1)digest.update(buffer,0,n);}
        StringBuilder actual=new StringBuilder();for(byte b:digest.digest())actual.append(String.format(java.util.Locale.ROOT,"%02x",b&255));
        if(!actual.toString().equals(RuntimeBuild.SHA256))throw new IOException("Thư viện runtime không khớp SHA-256");
        return path;
    }
}
