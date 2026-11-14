package com.thor.nativefg;

import android.content.Context;
import android.content.SharedPreferences;

import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.security.KeyFactory;
import java.security.PublicKey;
import java.security.Signature;
import java.security.spec.X509EncodedKeySpec;
import java.util.Base64;
import java.util.Locale;

/**
 * Offline licence check for Thor Native FG.
 *
 * Keys are signed with an RSA-2048 private key that never leaves the maintainer's
 * machine; the app only carries the public key, so a key can be verified without
 * any server and without root. A key binds an edition, an optional device hash and
 * an optional expiry:
 *
 *   THORFG1.&lt;payload-base64url&gt;.&lt;signature-base64url&gt;
 *
 * payload = {"pkg":"com.thor.nativefg","dev":"&lt;16 hex or empty&gt;","ed":"full",
 *            "exp":&lt;epoch seconds, 0 = perpetual&gt;,"i":&lt;issued&gt;,"n":"&lt;note&gt;"}
 *
 * The build ships with {@code enforce=false} in assets/thorfg-license.json, so an
 * unlicensed install keeps working; flipping the flag activates the gate.
 */
final class License {
    enum State{MISSING,TRIAL,FULL,EXPIRED,INVALID}

    static final class Status{
        final State state;final String detail;final int trialDaysLeft;final String edition;
        Status(State state,String detail,int trialDaysLeft,String edition){
            this.state=state;this.detail=detail;this.trialDaysLeft=trialDaysLeft;this.edition=edition;
        }
        boolean licensed(){return state==State.FULL;}
        boolean usable(){return state==State.FULL||state==State.TRIAL;}
        String label(){
            switch(state){
                case FULL:return "Bản quyền: FULL"+("".equals(edition)?"":" · "+edition);
                case TRIAL:return "Dùng thử: còn "+trialDaysLeft+" ngày";
                case EXPIRED:return "Bản quyền đã hết hạn";
                case INVALID:return "Key không hợp lệ";
                default:return "Chưa kích hoạt";
            }
        }
    }

    private static final String PREFIX="THORFG1.";
    private static final String PREFS="thorfg_license";
    private static final String KEY_TEXT="key_text";
    private static final String KEY_STATE="key_state";
    private static final String TRIAL_START="trial_start";
    private static final String TRIAL_LAST="trial_last_seen";
    private static final String TRIAL_CLAIMED="trial_claimed";

    private final Context context;
    private final SharedPreferences prefs;

    // Read from assets once.
    private boolean enforce;
    private int trialDays=7;
    private String keyId="";
    private String shop="https://ko-fi.com/thorqol/shop";
    private PublicKey publicKey;

    License(Context context){
        this.context=context.getApplicationContext();
        this.prefs=this.context.getSharedPreferences(PREFS,0);
        loadConfig();
    }

    private void loadConfig(){
        try(InputStream input=context.getAssets().open("thorfg-license.json")){
            ByteArrayOutputStream bytes=new ByteArrayOutputStream();
            byte[] buffer=new byte[4096];int n;
            while((n=input.read(buffer))!=-1){
                if(bytes.size()+n>16384)throw new IllegalStateException("Licence config too large");
                bytes.write(buffer,0,n);
            }
            JSONObject json=new JSONObject(new String(bytes.toByteArray(),StandardCharsets.UTF_8));
            if(!UpdateSpec.PACKAGE.equals(json.optString("packageName",UpdateSpec.PACKAGE)))throw new IllegalStateException("Wrong licence package");
            enforce=json.optBoolean("enforce",false);
            trialDays=Math.max(0,Math.min(90,json.optInt("trialDays",7)));
            keyId=json.optString("keyId","");
            String shopValue=json.optString("shopUrl","");
            if(allowedShop(shopValue))shop=shopValue;
            String spki=json.optString("publicKeySpki","");
            if(!spki.isEmpty()){
                byte[] der=Base64.getDecoder().decode(spki);
                publicKey=KeyFactory.getInstance("RSA").generatePublic(new X509EncodedKeySpec(der));
            }
        }catch(Exception e){
            // A broken or missing config leaves licensing informational only.
            publicKey=null;enforce=false;
        }
    }

    static boolean allowedShop(String value){
        if(value==null||value.isEmpty())return false;
        try{
            java.net.URI uri=new java.net.URI(value);
            if(!"https".equals(uri.getScheme())||uri.getHost()==null||uri.getUserInfo()!=null||uri.getPort()!=-1||uri.getQuery()!=null||uri.getFragment()!=null)return false;
            String path=uri.getRawPath();
            if("payhip.com".equals(uri.getHost()))return path!=null&&path.matches("/b/[a-zA-Z0-9_-]{4,100}");
            return "ko-fi.com".equals(uri.getHost())&&("/thorqol/shop".equals(path)||(path!=null&&path.matches("/s/[a-zA-Z0-9]{1,100}")));
        }catch(Exception e){return false;}
    }

    boolean enforced(){return enforce;}
    String shopUrl(){return shop;}
    String keyId(){return keyId;}

    /** Parses and verifies the stored key, then folds in the trial clock. */
    Status status(){
        long now=System.currentTimeMillis()/1000L;
        Status grant=verifyStored();
        if(grant!=null&&grant.state==State.FULL)return grant;
        Status trial=trialStatus(now);
        if(trial!=null)return trial;
        return grant!=null?grant:new Status(State.MISSING,"Chưa kích hoạt bản quyền.",0,"");
    }

    /** Verifies a candidate key and, if valid, stores it. Returns the resulting state. */
    Status activate(String text){
        Status verified=verify(text);
        if(verified.state==State.FULL){
            prefs.edit().putString(KEY_TEXT,text.trim()).putString(KEY_STATE,verified.detail).apply();
            return verified;
        }
        prefs.edit().remove(KEY_TEXT).apply();
        return verified;
    }

    void clear(){prefs.edit().remove(KEY_TEXT).apply();}

    private Status verifyStored(){
        String text=prefs.getString(KEY_TEXT,"");
        if(text==null||text.isEmpty())return null;
        return verify(text);
    }

    private Status verify(String text){
        if(publicKey==null)return new Status(State.INVALID,"Thiếu khoá công khai trong cấu hình.",0,"");
        String trimmed=text==null?"":text.trim().replaceAll("\\s+","");
        if(!trimmed.startsWith(PREFIX))return new Status(State.INVALID,"Key sai định dạng.",0,"");
        String body=trimmed.substring(PREFIX.length());
        int dot=body.indexOf('.');
        if(dot<=0||dot==body.length()-1)return new Status(State.INVALID,"Key sai định dạng.",0,"");
        String payloadPart=body.substring(0,dot);
        String signaturePart=body.substring(dot+1);
        try{
            Signature verifier=Signature.getInstance("SHA256withRSA");
            verifier.initVerify(publicKey);
            verifier.update(payloadPart.getBytes(StandardCharsets.US_ASCII));
            if(!verifier.verify(Base64.getUrlDecoder().decode(signaturePart)))return new Status(State.INVALID,"Chữ ký không khớp.",0,"");
            JSONObject payload=new JSONObject(new String(Base64.getUrlDecoder().decode(payloadPart),StandardCharsets.UTF_8));
            if(!UpdateSpec.PACKAGE.equals(payload.optString("pkg","")))return new Status(State.INVALID,"Key dành cho ứng dụng khác.",0,"");
            String device=payload.optString("dev","");
            if(!device.isEmpty()&&!device.equals(DeviceIdentity.id(context)))return new Status(State.INVALID,"Key dành cho thiết bị khác.",0,"");
            long expires=payload.optLong("exp",0);
            String edition=payload.optString("ed","full");
            long now=System.currentTimeMillis()/1000L;
            if(expires>0&&now>expires){
                return new Status(State.EXPIRED,"Hết hạn "+new java.util.Date(expires*1000L).toString(),0,edition);
            }
            String detail=expires==0?"Vĩnh viễn":"Còn hạn đến "+new java.util.Date(expires*1000L).toString();
            return new Status(State.FULL,detail,trialDays,edition);
        }catch(Exception e){
            return new Status(State.INVALID,"Không đọc được key.",0,"");
        }
    }

    /**
     * Trial is anchored to the first launch and to the furthest wall clock ever seen,
     * so setting the clock back cannot restart it.
     */
    private Status trialStatus(long now){
        if(trialDays<=0)return null;
        long start=prefs.getLong(TRIAL_START,0);
        long last=prefs.getLong(TRIAL_LAST,0);
        if(start==0){
            prefs.edit().putLong(TRIAL_START,now).putLong(TRIAL_LAST,now).apply();
            start=now;last=now;
        }
        long seen=Math.max(last,now);
        if(seen!=last)prefs.edit().putLong(TRIAL_LAST,seen).apply();
        long elapsedDays=(seen-start)/86400L;
        int left=(int)Math.max(0,trialDays-elapsedDays);
        if(left<=0)return new Status(State.EXPIRED,"Hết thời gian dùng thử.",0,"trial");
        return new Status(State.TRIAL,"",left,"trial");
    }

    String deviceId(){return DeviceIdentity.id(context);}
    String describeDevice(){return "Thiết bị: "+deviceId().toUpperCase(Locale.ROOT);}
}
