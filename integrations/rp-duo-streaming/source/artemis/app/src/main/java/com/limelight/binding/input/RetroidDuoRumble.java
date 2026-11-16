package com.limelight.binding.input;

import android.media.AudioAttributes;
import android.os.Handler;
import android.os.SystemClock;
import android.os.VibrationEffect;
import android.os.Vibrator;

final class RetroidDuoRumble implements RawHapticsReceiver.Sink {
    private static final int PULSE_MS = 1000;
    private static final int REFRESH_MS = 800;
    private static final int MIN_STANDARD_PULSE_MS = 20;
    private static final AudioAttributes GAME_AUDIO = new AudioAttributes.Builder()
            .setUsage(AudioAttributes.USAGE_GAME).build();

    private final Vibrator vibrator;
    private final Handler handler;
    private final boolean amplitudeControl;
    private final Runnable refresh = this::refresh;
    private final Runnable expireRaw = this::expireRaw;
    private final Runnable finishStandard = this::finishStandard;
    private long rawExpiresAt = Long.MAX_VALUE;
    private long standardStopNotBefore;
    private boolean standardStopPending;
    private int amplitude;
    private long refreshAt;
    private boolean closed;

    RetroidDuoRumble(Vibrator vibrator, Handler handler) {
        this.vibrator = vibrator;
        this.handler = handler;
        this.amplitudeControl = vibrator.hasAmplitudeControl();
    }

    synchronized void setControllerMagnitudes(short lowMotor, short highMotor) {
        if (closed) return;
        cancelRawExpiry();
        int low = lowMotor & 0xffff;
        int high = highMotor & 0xffff;
        // Mix before reducing to the built-in motor's 8-bit amplitude. Zero alone means stop.
        int magnitude = low == 0 && high == 0 ? 0
                : Math.max(6, Math.min(255, (80 * low + 33 * high + 12850) / 25700));
        long now = SystemClock.uptimeMillis();
        cancelStandardStop();
        if (magnitude == 0 && now < standardStopNotBefore) {
            standardStopPending = true;
            handler.postAtTime(finishStandard, standardStopNotBefore);
            return;
        }
        if (magnitude == 0) standardStopNotBefore = 0;
        else if (standardStopNotBefore == 0) standardStopNotBefore = now + MIN_STANDARD_PULSE_MS;
        setMagnitude(magnitude);
    }

    private void cancelStandardStop() {
        standardStopPending = false;
        handler.removeCallbacks(finishStandard);
    }

    private synchronized void finishStandard() {
        if (!standardStopPending || SystemClock.uptimeMillis() < standardStopNotBefore) return;
        standardStopPending = false;
        standardStopNotBefore = 0;
        setMagnitude(0);
    }

    private void setMagnitude(int magnitude) {
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
        if (closed || amplitude == 0) return;
        long now = SystemClock.uptimeMillis();
        if (now >= rawExpiresAt) expireRaw();
        else if (now >= refreshAt) play();
    }

    private void play() {
        vibrator.vibrate(VibrationEffect.createOneShot(PULSE_MS, amplitude), GAME_AUDIO);
        refreshAt = SystemClock.uptimeMillis() + REFRESH_MS;
        handler.postAtTime(refresh, refreshAt);
    }

    @Override public synchronized void apply(RawHapticsReceiver.Frame frame) {
        if (frame.player != 0 || closed) return;
        cancelStandardStop();
        standardStopNotBefore = 0;
        float[] bands = frame.getBands();
        float power = 0;
        for (int i = 0; i < bands.length; i += 2) power += bands[i] * bands[i];
        // Match Droidplay's intensity curve and use the same one-shot/cancel backend.
        float level = (float) Math.min(1, Math.sqrt(power));
        cancelRawExpiry();
        if (level < .02f || frame.holdMicros == 0) {
            setMagnitude(0);
            return;
        }
        rawExpiresAt = SystemClock.uptimeMillis() + (frame.holdMicros + 999) / 1000;
        setMagnitude(Math.max(6, Math.round(level * 255)));
        handler.postAtTime(expireRaw, rawExpiresAt);
    }

    private void cancelRawExpiry() {
        rawExpiresAt = Long.MAX_VALUE;
        handler.removeCallbacks(expireRaw);
    }

    private synchronized void expireRaw() {
        if (SystemClock.uptimeMillis() < rawExpiresAt) return;
        cancelRawExpiry();
        setMagnitude(0);
    }

    @Override public synchronized void stop() {
        cancelRawExpiry();
        cancelStandardStop();
        standardStopNotBefore = 0;
        setMagnitude(0);
    }

    synchronized void close() {
        stop();
        closed = true;
    }
}
