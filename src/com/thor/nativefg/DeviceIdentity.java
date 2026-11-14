package com.thor.nativefg;

import android.content.Context;
import android.provider.Settings;

import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.Locale;

/**
 * Stable per-device identifier used to bind a licence key to one handheld.
 *
 * ANDROID_ID is scoped to (app signing key, user, device) since Android 8, so it
 * survives app updates and reinstalls but not a different device or a different
 * signing key. It needs no permission and no root.
 */
final class DeviceIdentity {
    private static String cached;

    static synchronized String id(Context context){
        if(cached!=null)return cached;
        String androidId="";
        try{androidId=Settings.Secure.getString(context.getContentResolver(),Settings.Secure.ANDROID_ID);}catch(Exception ignored){}
        if(androidId==null)androidId="";
        String material=androidId+"|"+context.getPackageName()+"|"+android.os.Build.FINGERPRINT;
        cached=hash(material).substring(0,16);
        return cached;
    }

    static String hash(String value){
        try{
            MessageDigest digest=MessageDigest.getInstance("SHA-256");
            byte[] bytes=digest.digest(value.getBytes(StandardCharsets.UTF_8));
            StringBuilder out=new StringBuilder(bytes.length*2);
            for(byte b:bytes)out.append(String.format(Locale.ROOT,"%02x",b&255));
            return out.toString();
        }catch(Exception e){
            return "0000000000000000";
        }
    }
}
