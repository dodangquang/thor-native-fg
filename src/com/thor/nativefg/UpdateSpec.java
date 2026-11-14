package com.thor.nativefg;

import java.net.URI;
import java.util.Locale;

/**
 * One parsed update manifest. Everything here is treated as untrusted input: the
 * URLs are pinned to this repository, the sizes are bounded, and the APK itself is
 * still verified against the installed signing certificate before installation.
 *
 * Update metadata is served straight from the public git repository (raw
 * GitHub), so publishing an update is a git commit, not a Netlify deploy.
 */
final class UpdateSpec {
    static final String PACKAGE="com.thor.nativefg";
    /** raw.githubusercontent.com origin of the public repository, branch master. */
    static final String FEED_ORIGIN="https://raw.githubusercontent.com/dodangquang/thor-native-fg/master";
    static final String RELEASES="https://github.com/dodangquang/thor-native-fg/releases/download";
    static final long MAX_APK=256L*1024*1024;

    final String packageName,versionName,url,sha256,notes;
    final long versionCode,bytes;
    final int minSdk;

    UpdateSpec(String expected,String pkg,long code,String version,int sdk,long size,String hash,String apk,String copy){
        if(!PACKAGE.equals(expected)||!expected.equals(pkg))throw new IllegalArgumentException("Wrong package");
        if(code<1||code>2100000000L)throw new IllegalArgumentException("Bad version code");
        if(version==null||!version.matches("[0-9]+\\.[0-9]+\\.[0-9]+[A-Za-z0-9._-]*"))throw new IllegalArgumentException("Bad version name");
        if(sdk<26||sdk>100)throw new IllegalArgumentException("Bad minSdk");
        if(size<1||size>MAX_APK)throw new IllegalArgumentException("Bad size");
        if(hash==null||!hash.matches("[a-fA-F0-9]{64}"))throw new IllegalArgumentException("Bad hash");
        if(copy==null||copy.length()>8192)throw new IllegalArgumentException("Bad notes");
        String filename=filename(version);
        try{
            URI u=new URI(apk);
            if(!"https".equals(u.getScheme())||!"github.com".equals(u.getHost())||u.getPort()!=-1||u.getRawUserInfo()!=null||u.getRawQuery()!=null||u.getRawFragment()!=null)throw new IllegalArgumentException("Untrusted APK host");
            String expectedPath="/dodangquang/thor-native-fg/releases/download/thorfg-v"+version+"/"+filename;
            if(!expectedPath.equals(u.getRawPath()))throw new IllegalArgumentException("Untrusted APK path");
        }catch(IllegalArgumentException e){throw e;}catch(Exception e){throw new IllegalArgumentException("Untrusted APK URL");}
        packageName=pkg;versionCode=code;versionName=version;minSdk=sdk;bytes=size;
        sha256=hash.toLowerCase(Locale.ROOT);url=apk;notes=copy;
    }

    static String filename(String version){return "ThorNativeFG-"+version+".apk";}
    static String manifest(){return FEED_ORIGIN+"/updates/v1/"+PACKAGE+".json";}
    /** Only the exact manifest URL is accepted, so nothing can relocate the feed. */
    static boolean trustedManifest(String url){return manifest().equals(url);}
    static String releaseTag(String version){return "thorfg-v"+version;}
}
