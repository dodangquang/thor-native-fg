package com.thor.nativefg;

import org.w3c.dom.*;

public final class DroidDeckSettingsTest {
    static int checks;
    interface Action {void run()throws Exception;}
    static void check(boolean ok,String what){if(!ok)throw new AssertionError(what);checks++;}
    static void fails(Action action,String what)throws Exception{try{action.run();}catch(Exception expected){checks++;return;}throw new AssertionError(what);}
    static boolean same(String a,String b,String key)throws Exception{return DroidDeckSettings.equal(DroidDeckSettings.entry(DroidDeckSettings.parse(a),key),DroidDeckSettings.entry(DroidDeckSettings.parse(b),key));}
    public static void main(String[] args)throws Exception{
        String s="<map><int name='upscaler' value='2'/><string name='screenEffects'>{&quot;cas&quot;:true}</string><string name='addedExe:game'>/game/A &amp; B's.exe</string><set name='drivers'><string>Turnip</string><string>System</string></set><boolean name='wifiDiscovery' value='true'/></map>";
        String f="<map><string name='engine'>lsfg</string><int name='multiplier' value='3'/><int name='target' value='120'/></map>";
        DroidDeckSettings.Pair p=DroidDeckSettings.apply(s,f);
        check(DroidDeckSettings.active(p.session,p.fg),"preset active");
        check(same(s,p.session,"addedExe:game"),"executable including entities unchanged");
        check(same(s,p.session,"drivers"),"nested unknown setting unchanged");
        check(same(f,p.fg,"target")&&same(f,p.fg,"multiplier"),"other FG preferences unchanged");
        DroidDeckSettings.Pair idempotent=DroidDeckSettings.apply(p.session,p.fg);
        check(DroidDeckSettings.active(idempotent.session,idempotent.fg),"idempotent");
        Document later=DroidDeckSettings.parse(p.session);DroidDeckSettings.put(later,"boolean","wifiDiscovery","false");DroidDeckSettings.put(later,"string","newUserSetting","new value");
        DroidDeckSettings.Pair restored=DroidDeckSettings.restore(s,f,DroidDeckSettings.text(later),p.fg);
        check(same(s,restored.session,"upscaler")&&same(s,restored.session,"screenEffects")&&same(f,restored.fg,"engine"),"restore original owned keys");
        check(same(DroidDeckSettings.text(later),restored.session,"wifiDiscovery")&&same(DroidDeckSettings.text(later),restored.session,"newUserSetting"),"preserve subsequent unrelated edits");
        DroidDeckSettings.Pair partial=DroidDeckSettings.restore(s,f,p.session,f);
        check(same(s,partial.session,"screenEffects")&&same(f,partial.fg,"engine"),"recover interruption between the two live-file renames");
        DroidDeckSettings.Pair alreadyRestored=DroidDeckSettings.restore(s,f,s,f);
        check(same(s,alreadyRestored.session,"upscaler")&&same(f,alreadyRestored.fg,"engine"),"retry completed restore before backup retirement");
        DroidDeckSettings.Pair empty=DroidDeckSettings.apply("<map/>","<map/>");
        DroidDeckSettings.Pair deleted=DroidDeckSettings.restore("<map/>","<map/>",empty.session,empty.fg);
        check(DroidDeckSettings.parse(deleted.session).getDocumentElement().getElementsByTagName("int").getLength()==0&&!DroidDeckSettings.active(deleted.session,deleted.fg),"restore absent keys");
        Document conflict=DroidDeckSettings.parse(p.session);DroidDeckSettings.put(conflict,"int","upscaler","4");
        fails(()->DroidDeckSettings.restore(s,f,DroidDeckSettings.text(conflict),p.fg),"reject changed owned key");
        Document fgConflict=DroidDeckSettings.parse(p.fg);DroidDeckSettings.put(fgConflict,"string","engine","winfg");
        fails(()->DroidDeckSettings.restore(s,f,p.session,DroidDeckSettings.text(fgConflict)),"reject changed FG engine");
        fails(()->DroidDeckSettings.apply("<map><string name='upscaler'>2</string></map>",f),"reject unknown schema");
        fails(()->DroidDeckSettings.apply("<map><int name='upscaler' value='1'/><int name='upscaler' value='2'/></map>",f),"reject duplicates");
        fails(()->DroidDeckSettings.apply("<!DOCTYPE map [<!ENTITY x SYSTEM 'file:///not-read'>]><map/>",f),"reject external entity");
        fails(()->DroidDeckSettings.apply("<wrong/>",f),"reject wrong root");
        System.out.println("DROID_PRESET_RESULT checks="+checks+" failures=0");
    }
}
