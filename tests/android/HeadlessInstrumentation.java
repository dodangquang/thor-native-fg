package com.thor.neuralqa;

import android.app.Activity;
import android.app.Instrumentation;
import android.os.Bundle;
import android.os.Process;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;

/** Shader correctness instrumentation with no Activity, overlay or game input. */
public final class HeadlessInstrumentation extends Instrumentation {
    private static native int runTests(String fixturePath);
    @Override public void onCreate(Bundle arguments){super.onCreate(arguments);start();}
    @Override public void onStart(){
        Bundle result=new Bundle();int code=Activity.RESULT_CANCELED;
        result.putInt("pid",Process.myPid());
        try{
            File fixture=new File(getContext().getCacheDir(),"neural-fixtures.bin");
            try(InputStream input=getContext().getAssets().open("fixtures.bin");FileOutputStream output=new FileOutputStream(fixture)){
                byte[] buffer=new byte[8192];int count;while((count=input.read(buffer))!=-1)output.write(buffer,0,count);
            }
            System.loadLibrary("thor_neural_qa");
            int failures=runTests(fixture.getAbsolutePath());
            result.putString("stream",failures==0?"Neural GPU fixtures completed; inspect per-PID validation log":"Neural GPU fixture failure");
            if(failures==0)code=Activity.RESULT_OK;
        }catch(Exception error){result.putString("stream",error.toString());}
        finish(code,result);
    }
}
