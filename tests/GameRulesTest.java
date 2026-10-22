package com.thor.nativefg;
import java.util.*;
public class GameRulesTest {
    private static int checks;
    private static void check(boolean value,String label){checks++;if(!value)throw new AssertionError(label);System.out.println("PASS "+label);}
    public static void main(String[] args){
        check(GameRules.isEmulator("com.droiddeck.launcher"),"DroidDeck is detected");
        check(GameRules.isEmulator("org.ppsspp.ppsspp")&&GameRules.isEmulator("org.citra.eden"),"installed emulator package families are detected");
        check(!GameRules.isEmulator("com.android.settings"),"ordinary settings are not an emulator");
        check(GameRules.detected("com.example.game",true,false),"game metadata enables an ordinary game candidate");
        check(!GameRules.detected("com.ludashi.benchmark",true,false),"benchmark with game metadata is not enrolled by default");
        check(!GameRules.detected("com.iisulauncher",true,true)&&GameRules.detected("com.droiddeck.launcher",true,true),"ordinary home launcher is excluded while an emulator frontend remains eligible");
        Set<String> none=Collections.emptySet(),include=new HashSet<>(Arrays.asList("com.example.game")),exclude=new HashSet<>(Arrays.asList("com.droiddeck.launcher","com.example.game"));
        check(GameRules.selected("com.droiddeck.launcher",true,none,none),"detected game is selected by default");
        check(!GameRules.selected("com.example.browser",false,none,none),"ordinary app is not automatically enrolled");
        check(GameRules.selected("com.example.game",false,include,none),"user can enroll a game with missing metadata");
        check(!GameRules.selected("com.droiddeck.launcher",true,none,exclude),"user exclusion overrides emulator detection");
        check(!GameRules.selected("com.example.game",true,include,exclude),"exclusion wins when overrides conflict");
        check(GameRules.validPackage("com.game_2.app")&&!GameRules.validPackage("com.foo;id")&&!GameRules.validPackage("com..foo")&&!GameRules.validPackage("com.foo:service"),"only bounded package names reach root configuration");
        System.out.println("RESULT checks="+checks+" failures=0");
    }
}
