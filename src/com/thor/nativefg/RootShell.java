package com.thor.nativefg;

import java.io.*;

final class RootShell {
    static String quote(String value){return "'"+value.replace("'","'\\''")+"'";}
    static String run(String script)throws Exception{
        Process process=new ProcessBuilder("su","-c",script).redirectErrorStream(true).start();StringBuilder out=new StringBuilder();
        try(BufferedReader reader=new BufferedReader(new InputStreamReader(process.getInputStream()))){String line;while((line=reader.readLine())!=null){out.append(line).append('\n');if(out.length()>64000){process.destroy();throw new IOException("Root output too large");}}}
        int code=process.waitFor();if(code!=0)throw new IOException("Root exit "+code+": "+out);return out.toString().trim();
    }
}
