package com.thor.gsr2qa;

import android.app.Activity;
import android.app.Instrumentation;
import android.os.Bundle;
import android.os.Process;
import java.io.*;
import java.security.MessageDigest;

/** Own-package, headless production-shader measurement. No game/input/settings. */
public final class SpatialInstrumentation extends Instrumentation {
    private static native int runLab(String fixtures,String directory);
    @Override public void onCreate(Bundle arguments){super.onCreate(arguments);start();}
    @Override public void onStart(){
        Bundle result=new Bundle();int code=Activity.RESULT_CANCELED;result.putInt("pid",Process.myPid());
        try{
            File fixture=new File(getContext().getCacheDir(),"spatial-fixtures.bin");MessageDigest digest=MessageDigest.getInstance("SHA-256");
            try(InputStream input=getContext().getAssets().open("fixtures.bin");FileOutputStream output=new FileOutputStream(fixture)){
                byte[] buffer=new byte[65536];int count;while((count=input.read(buffer))!=-1){output.write(buffer,0,count);digest.update(buffer,0,count);}
            }
            StringBuilder hash=new StringBuilder();for(byte value:digest.digest())hash.append(String.format(java.util.Locale.ROOT,"%02x",value&255));
            if(!hash.toString().equals(LabFixture.SHA256))throw new IOException("Fixture checksum mismatch");
            File directory=new File(getContext().getFilesDir(),"results");if(!directory.isDirectory()&&!directory.mkdirs())throw new IOException("Cannot create results directory");
            // Results belong only to this temporary QA package. Different corpus
            // sizes must not leave stale members that could be scored as current.
            File[] previous=directory.listFiles();if(previous==null)throw new IOException("Cannot list prior results");
            for(File file:previous){if(!file.isFile()||!file.getName().matches("(frame-[0-9]{3}\\.rgba16f|diagnostic(-001)?\\.bin)")||!file.delete())throw new IOException("Cannot clear prior QA result: "+file.getName());}
            MessageDigest nativeDigest=MessageDigest.getInstance("SHA-256");File library=new File(getContext().getApplicationInfo().nativeLibraryDir,"libthor_gsr2_lab.so");
            try(InputStream input=new FileInputStream(library)){byte[] buffer=new byte[65536];int count;while((count=input.read(buffer))!=-1)nativeDigest.update(buffer,0,count);}
            StringBuilder nativeHash=new StringBuilder();for(byte value:nativeDigest.digest())nativeHash.append(String.format(java.util.Locale.ROOT,"%02x",value&255));
            if(!nativeHash.toString().equals(LabFixture.NATIVE_SHA256))throw new IOException("Native lab checksum mismatch");
            result.putString("native_sha256",nativeHash.toString());
            System.loadLibrary("thor_gsr2_lab");int failure=runLab(fixture.getAbsolutePath(),directory.getAbsolutePath());
            result.putString("stream",failure==0?"Spatial outputs ready; inspect current-PID validation log before scoring":"Spatial lab failed");result.putString("fixtures_sha256",hash.toString());
            if(failure==0)code=Activity.RESULT_OK;
        }catch(Exception error){result.putString("stream",error.toString());}
        finish(code,result);
    }
}
