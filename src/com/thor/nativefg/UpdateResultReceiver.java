package com.thor.nativefg;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageInstaller;

/** Receives the PackageInstaller session result and surfaces a short status. */
public final class UpdateResultReceiver extends BroadcastReceiver {
    static final String ACTION="com.thor.nativefg.UPDATE_RESULT";

    @Override public void onReceive(Context context,Intent intent){
        int status=intent.getIntExtra(PackageInstaller.EXTRA_STATUS,PackageInstaller.STATUS_FAILURE);
        String detail=intent.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE);
        boolean ok=status==PackageInstaller.STATUS_SUCCESS;
        String message=ok
            ?"Đã cập nhật xong. Mở lại ứng dụng để dùng bản mới."
            :"Cài đặt không hoàn tất"+(detail==null||detail.isEmpty()?"":": "+detail);
        try{android.util.Log.i("ThorNativeFG",message);}catch(Exception ignored){}
        try{
            android.widget.Toast.makeText(context,message,android.widget.Toast.LENGTH_LONG).show();
        }catch(Exception ignored){}
    }
}
