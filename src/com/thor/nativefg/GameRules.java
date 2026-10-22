package com.thor.nativefg;

import java.util.Locale;
import java.util.Set;

/** Classification is based on package metadata and the user's overrides. */
final class GameRules {
    static boolean validPackage(String id){return id!=null&&id.length()<256&&id.matches("[A-Za-z0-9_]+(?:\\.[A-Za-z0-9_]+)+");}
    static boolean isEmulator(String id){
        String x=id.toLowerCase(Locale.ROOT);
        for(String s:new String[]{"droiddeck","eden","winlator","ppsspp","nethersx","aethersx","dolphin","retroarch","citron","yuzu","sudachi","citra","azahar","lime3ds","duckstation","mupen","redream","flycast","vita3k","cosmic","suyu","strato","skyline","melonds","drastic","uzuy","cemi"})if(x.contains(s))return true;
        return false;
    }
    static boolean selected(String id,boolean detected,Set<String> includes,Set<String> excludes){return !excludes.contains(id)&&(detected||includes.contains(id));}
    static boolean detected(String id,boolean gameMetadata,boolean home){
        if(isEmulator(id))return true;
        String lower=id.toLowerCase(Locale.ROOT);
        boolean benchmark=lower.contains("benchmark")||lower.contains(".antutu.")||lower.contains(".geekbench")||lower.startsWith("com.futuremark.");
        return gameMetadata&&!home&&!benchmark;
    }
}
