package com.limelight.binding.input;

import android.media.AudioAttributes;
import android.os.Handler;
import android.os.SystemClock;
import android.os.VibrationEffect;
import android.os.Vibrator;

final class RetroidDuoRumble implements RawHapticsReceiver.Sink {
    private static final int PULSE_MS = 1000;
    private static final int REFRESH_MS = 800;
    private static final AudioAttributes GAME_AUDIO = new AudioAttributes.Builder()
            .setUsage(AudioAttributes.USAGE_GAME).build();

    private final Vibrator vibrator;
    private final Handler handler;
    private final boolean amplitudeControl;
    private final Runnable refresh = this::refresh;
    private final Runnable expireRaw = this::expireRaw;
    private long rawExpiresAt;
    private int amplitude;
    private long refreshAt;
    private boolean closed;

    RetroidDuoRumble(Vibrator vibrator, Handler handler) {
        this.vibrator = vibrator;
        this.handler = handler;
        this.amplitudeControl = vibrator.hasAmplitudeControl();
    }

    synchronized void setMagnitude(int magnitude) {
        if (closed) return;

        // Use the Android NX game's cutoff and its one-shot/cancel behavior.
        int next = magnitude < 6 ? 0 : (amplitudeControl
                ? magnitude : VibrationEffect.DEFAULT_AMPLITUDE);
        if (next == amplitude) return;

        amplitude = next;
        handler.removeCallbacks(refresh);
        if (amplitude == 0) vibrator.cancel();
        else play();
    }

    private synchronized void refresh() {
        if (!closed && amplitude != 0 && SystemClock.uptimeMillis() >= refreshAt) play();
    }

    private void play() {
        vibrator.vibrate(VibrationEffect.createOneShot(PULSE_MS, amplitude), GAME_AUDIO);
        refreshAt = SystemClock.uptimeMillis() + REFRESH_MS;
        handler.postAtTime(refresh, refreshAt);
    }

    @Override public synchronized void apply(RawHapticsReceiver.Frame frame) {
        if (frame.player != 0 || closed) return;
        float[] bands = frame.getBands();
        float power = 0;
        for (int i = 0; i < bands.length; i += 2) power += bands[i] * bands[i];
        // Match Droidplay's intensity curve and use the same one-shot/cancel backend.
        float level = (float) Math.min(1, Math.sqrt(power));
        setMagnitude(level < .02f ? 0 : Math.max(6, Math.round(level * 255)));
        handler.removeCallbacks(expireRaw);
        if (level >= .02f) {
            rawExpiresAt = SystemClock.uptimeMillis() + frame.holdMicros / 1000;
            handler.postAtTime(expireRaw, rawExpiresAt);
        }
    }

    private synchronized void expireRaw() {
        if (SystemClock.uptimeMillis() >= rawExpiresAt) setMagnitude(0);
    }

    @Override public synchronized void stop() {
        handler.removeCallbacks(expireRaw);
        rawExpiresAt = Long.MAX_VALUE;
        setMagnitude(0);
    }

    synchronized void close() {
        stop();
        closed = true;
    }
}
