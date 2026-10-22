package com.thor.nativefg;

import android.content.*;

/** Refresh package UIDs and the provider path after app/package updates. */
public class AutoReceiver extends BroadcastReceiver {
    @Override public void onReceive(Context context,Intent intent){
        if(!AutoMode.requested(context))return;
        PendingResult pending=goAsync();new Thread(()->{try{
            AutoMode.refresh(context);
            if((Intent.ACTION_MY_PACKAGE_REPLACED.equals(intent.getAction())||Intent.ACTION_BOOT_COMPLETED.equals(intent.getAction()))&&AutoMode.preferences(context).getBoolean("hud_visible",false)&&android.provider.Settings.canDrawOverlays(context))context.startForegroundService(new Intent(context,FpsOverlayService.class));
        }catch(Exception error){android.util.Log.e("ThorAutoMode","Config refresh failed",error);}finally{pending.finish();}},"thor-auto-refresh").start();
    }
}
