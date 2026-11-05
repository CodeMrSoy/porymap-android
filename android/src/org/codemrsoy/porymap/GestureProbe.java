package org.codemrsoy.porymap;

import android.app.Activity;
import android.content.Context;
import android.content.pm.ApplicationInfo;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.view.InputDevice;
import android.view.MotionEvent;
import java.io.*;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import org.json.*;

/** Debug-APK test bridge through Android's MotionEvent -> Qt input delegate. */
final class GestureProbe {
    static String dispatch(Context context, String script, String resultPath) throws Exception {
        if (!(context instanceof Activity) || (context.getApplicationInfo().flags & ApplicationInfo.FLAG_DEBUGGABLE) == 0)
            throw new IOException("Touch probes require a debuggable activity");
        final Activity activity = (Activity) context;
        final JSONArray frames = new JSONArray(script);
        if (frames.length() < 2 || frames.length() > 100) throw new IOException("Invalid touch probe");
        final Handler handler = new Handler(Looper.getMainLooper());
        final long downTime = SystemClock.uptimeMillis() + 50;
        final AtomicInteger accepted = new AtomicInteger();
        final AtomicReference<String> failure = new AtomicReference<>();
        long end = 0;
        for (int index = 0; index < frames.length(); ++index) {
            final JSONObject frame = frames.getJSONObject(index);
            final long delay = frame.getLong("delay");
            if (delay < end || delay > 10000) throw new IOException("Invalid probe timing");
            end = delay;
            handler.postAtTime(() -> {
                MotionEvent event = null;
                try {
                    JSONArray points = frame.getJSONArray("points");
                    int action = frame.getInt("action"), count = points.length();
                    if (count < 1 || count > 3) throw new IOException("Invalid pointer count");
                    MotionEvent.PointerProperties[] properties = new MotionEvent.PointerProperties[count];
                    MotionEvent.PointerCoords[] coords = new MotionEvent.PointerCoords[count];
                    int[] origin = new int[2]; activity.getWindow().getDecorView().getLocationOnScreen(origin);
                    for (int i = 0; i < count; ++i) {
                        JSONObject point = points.getJSONObject(i);
                        properties[i] = new MotionEvent.PointerProperties();
                        properties[i].id = point.getInt("id"); properties[i].toolType = MotionEvent.TOOL_TYPE_FINGER;
                        coords[i] = new MotionEvent.PointerCoords();
                        coords[i].x = (float) point.getDouble("x") - origin[0];
                        coords[i].y = (float) point.getDouble("y") - origin[1];
                        coords[i].pressure = (action & MotionEvent.ACTION_MASK) == MotionEvent.ACTION_UP ? 0 : 1;
                        coords[i].size = 0.1f;
                    }
                    event = MotionEvent.obtain(downTime, SystemClock.uptimeMillis(), action, count,
                        properties, coords, 0, 0, 1, 1, 0, 0, InputDevice.SOURCE_TOUCHSCREEN, 0);
                    if (activity.dispatchTouchEvent(event)) accepted.incrementAndGet();
                } catch (Exception e) { failure.compareAndSet(null, e.toString()); }
                finally { if (event != null) event.recycle(); }
            }, downTime + delay);
        }
        handler.postAtTime(() -> {
            try {
                JSONObject result = new JSONObject().put("frames", frames.length()).put("accepted", accepted.get());
                if (failure.get() != null) result.put("error", failure.get());
                File temporary = new File(resultPath + ".tmp");
                try (Writer output = new OutputStreamWriter(new FileOutputStream(temporary), "UTF-8")) {
                    output.write(result.toString());
                }
                if (!temporary.renameTo(new File(resultPath))) throw new IOException("Cannot publish touch probe result");
            } catch (Exception ignored) { /* The native test reports a missing result. */ }
        }, downTime + end + 80);
        return new JSONObject().put("scheduled", true).toString();
    }
}
