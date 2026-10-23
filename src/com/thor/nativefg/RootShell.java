package com.thor.nativefg;

import java.io.*;

final class RootShell {
    static String quote(String value){return "'"+value.replace("'","'\\''")+"'";}
    static String run(String script)throws Exception{
        return execute(script,false);
    }
    // Android's app mount namespace hides other packages' data directories even
    // from an inherited root shell. Only cross-app preset operations need this.
    static String runMaster(String script)throws Exception{return execute(script,true);}
    private static String execute(String script,boolean master)throws Exception{
        Process process=(master?new ProcessBuilder("su","-mm","-c",script):new ProcessBuilder("su","-c",script)).redirectErrorStream(true).start();StringBuilder out=new StringBuilder();
        try(BufferedReader reader=new BufferedReader(new InputStreamReader(process.getInputStream()))){String line;while((line=reader.readLine())!=null){out.append(line).append('\n');if(out.length()>64000){process.destroy();throw new IOException("Root output too large");}}}
        int code=process.waitFor();if(code!=0)throw new IOException("Root exit "+code+": "+out);return out.toString().trim();
    }
}
