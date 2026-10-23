package com.thor.nativefg;

import java.io.*;
import java.util.*;
import javax.xml.parsers.*;
import javax.xml.transform.*;
import javax.xml.transform.dom.DOMSource;
import javax.xml.transform.stream.StreamResult;
import org.w3c.dom.*;
import org.xml.sax.InputSource;

/** Edits only the three preferences that obstruct Thor's low-resolution path. */
final class DroidDeckSettings {
    static final String NEUTRAL="{\"brightness\":0,\"contrast\":0,\"gamma\":1,\"saturation\":100,\"cas\":false,\"casLevel\":0,\"hdr\":false,\"deband\":false,\"debandStrength\":0,\"fxaa\":false,\"toon\":false,\"crt\":false,\"ntsc\":false}";
    static final String[] SESSION_KEYS={"upscaler","screenEffects"},FG_KEYS={"engine"};
    static final class Pair {
        final String session,fg;
        Pair(String session,String fg){this.session=session;this.fg=fg;}
    }
    static Document parse(String xml)throws Exception{
        if(xml.length()>60000||xml.contains("<!DOCTYPE")||xml.contains("<!ENTITY"))throw new IOException("Cấu hình XML không được hỗ trợ");
        DocumentBuilderFactory factory=DocumentBuilderFactory.newInstance();factory.setExpandEntityReferences(false);
        Document doc=factory.newDocumentBuilder().parse(new InputSource(new StringReader(xml)));
        if(!"map".equals(doc.getDocumentElement().getTagName()))throw new IOException("Cấu hình không phải Android preferences");
        HashSet<String> names=new HashSet<>();
        for(Node n=doc.getDocumentElement().getFirstChild();n!=null;n=n.getNextSibling())if(n instanceof Element){
            Element e=(Element)n;if(!e.hasAttribute("name")||!names.add(e.getAttribute("name")))throw new IOException("Tên preference thiếu hoặc trùng");
        }
        return doc;
    }
    static Element entry(Document doc,String key){
        for(Node n=doc.getDocumentElement().getFirstChild();n!=null;n=n.getNextSibling())if(n instanceof Element&&key.equals(((Element)n).getAttribute("name")))return (Element)n;
        return null;
    }
    static void checkType(Document doc,String key,String type)throws IOException{
        Element e=entry(doc,key);if(e!=null&&!type.equals(e.getTagName()))throw new IOException("Kiểu preference không được hỗ trợ: "+key);
    }
    static void put(Document doc,String type,String key,String value){
        Element old=entry(doc,key),e=doc.createElement(type);e.setAttribute("name",key);
        if("string".equals(type))e.setTextContent(value);else e.setAttribute("value",value);
        if(old==null)doc.getDocumentElement().appendChild(e);else doc.getDocumentElement().replaceChild(e,old);
    }
    static String text(Document doc)throws Exception{
        Transformer out=TransformerFactory.newInstance().newTransformer();out.setOutputProperty(OutputKeys.ENCODING,"utf-8");
        StringWriter writer=new StringWriter();out.transform(new DOMSource(doc),new StreamResult(writer));return writer.toString();
    }
    static Pair apply(String session,String fg)throws Exception{
        Document s=parse(session),f=parse(fg);checkType(s,"upscaler","int");checkType(s,"screenEffects","string");checkType(f,"engine","string");
        put(s,"int","upscaler","1");put(s,"string","screenEffects",NEUTRAL);put(f,"string","engine","off");
        return new Pair(text(s),text(f));
    }
    static boolean equal(Element a,Element b){
        if(a==null||b==null)return a==b;
        return a.getTagName().equals(b.getTagName())&&a.getTextContent().equals(b.getTextContent())&&a.getAttribute("value").equals(b.getAttribute("value"));
    }
    static void restoreKeys(Document live,Document before,Document expected,String[] keys)throws IOException{
        for(String key:keys){
            Element actual=entry(live,key),old=entry(before,key);
            if(!equal(actual,entry(expected,key))&&!equal(actual,old))throw new IOException("DroidDeck đã đổi '"+key+"' sau khi áp dụng preset; giữ cấu hình mới, chưa hoàn tác.");
        }
        for(String key:keys){
            Element actual=entry(live,key),old=entry(before,key);
            if(actual!=null)live.getDocumentElement().removeChild(actual);
            if(old!=null)live.getDocumentElement().appendChild(live.importNode(old,true));
        }
    }
    static Pair restore(String beforeSession,String beforeFg,String currentSession,String currentFg)throws Exception{
        Pair expected=apply(beforeSession,beforeFg);
        Document s=parse(currentSession),f=parse(currentFg);
        restoreKeys(s,parse(beforeSession),parse(expected.session),SESSION_KEYS);
        restoreKeys(f,parse(beforeFg),parse(expected.fg),FG_KEYS);
        return new Pair(text(s),text(f));
    }
    static boolean active(String session,String fg)throws Exception{
        Pair expected=apply(session,fg);Document s=parse(session),f=parse(fg),es=parse(expected.session),ef=parse(expected.fg);
        for(String key:SESSION_KEYS)if(!equal(entry(s,key),entry(es,key)))return false;
        for(String key:FG_KEYS)if(!equal(entry(f,key),entry(ef,key)))return false;
        return true;
    }
}
