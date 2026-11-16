package android.os;
public class SystemClock {
    public static long now=1_000_000_000L;
    public static long elapsedRealtimeNanos() { return now; }
}
