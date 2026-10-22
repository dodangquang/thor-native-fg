package com.thor.nativefg;
import android.app.*;
import android.os.*;
import android.view.*;
import android.widget.*;
public class DemoActivity extends Activity implements SurfaceHolder.Callback {
 static {System.loadLibrary("thor_demo");}
 private static native void startNative(Surface surface);
 private static native void stopNative();
 private static native void modeNative(int mode);
 private SurfaceHolder activeHolder;
 private boolean active=false;
 @Override public void onCreate(Bundle state){super.onCreate(state);modeNative(getIntent().getIntExtra("mode",2));getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);LinearLayout root=new LinearLayout(this);root.setOrientation(1);LinearLayout bar=new LinearLayout(this);
 String[] labels={"Bilinear · 30","GSR 1 · 30","GSR 1 + FG · 60","Test layer ngoài"};for(int i=0;i<labels.length;i++){final int mode=i;Button b=new Button(this);b.setText(labels[i]);b.setTextSize(12);b.setOnClickListener(v->modeNative(mode));bar.addView(b,new LinearLayout.LayoutParams(0,(int)(56*getResources().getDisplayMetrics().density),1));}root.addView(bar);
 TextView text=new TextView(this);text.setText("Cảnh tổng hợp 640×360 • Mục tiêu 30 → 60 yêu cầu present/s, chưa phải FPS hiển thị đã đo • Đổi cảnh mỗi 5 giây");root.addView(text);SurfaceView surface=new SurfaceView(this);surface.getHolder().addCallback(this);root.addView(surface,new LinearLayout.LayoutParams(-1,0,1));setContentView(root);}
 public void surfaceCreated(SurfaceHolder holder){activeHolder=holder;startNative(holder.getSurface());active=true;}
 public void surfaceChanged(SurfaceHolder holder,int format,int w,int h){}
 private void stopIfActive(){if(active){active=false;stopNative();}}
 public void surfaceDestroyed(SurfaceHolder holder){if(holder==activeHolder){stopIfActive();activeHolder=null;}}
 @Override public void onPause(){super.onPause();stopIfActive();}
 @Override public void onResume(){super.onResume();if(!active&&activeHolder!=null&&activeHolder.getSurface().isValid()){startNative(activeHolder.getSurface());active=true;}}
}
