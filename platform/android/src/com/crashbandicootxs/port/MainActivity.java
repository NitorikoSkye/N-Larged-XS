package com.crashbandicootxs.port;

import android.app.Activity;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;
import android.os.Build;
import android.os.Bundle;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowManager;

import java.io.BufferedInputStream;
import java.io.DataInputStream;
import java.io.File;
import java.io.IOException;
import java.io.OutputStream;
import java.util.concurrent.ArrayBlockingQueue;

/**
 * The game's window on Android (platform/android/README.md).
 *
 * The game itself is the pc platform's core, built for Android as a static
 * executable (libcrashxs_core.so, in the APK's native libraries so that the
 * app may run it). It runs as a child process with --stream
 * (platform/pc/src/stream.c): it writes each frame's picture (240x160
 * RGB565) and sound (16-bit mono, 48 kHz) to its stdout and reads the
 * buttons (the GBA's key bits) from its stdin, at the GBA's own pace.
 *
 * In the background the app stops reading: the game stops at its next
 * picture, the pipe full, until the app comes back. The save is the
 * game's EEPROM image in the app's files (the game writes it as it
 * changes).
 */
public class MainActivity extends Activity {
    /** The GBA's key bits (KEYINPUT, set for pressed). */
    static final int KEY_A = 1, KEY_B = 2, KEY_SELECT = 4, KEY_START = 8, KEY_RIGHT = 16, KEY_LEFT = 32,
            KEY_UP = 64, KEY_DOWN = 128, KEY_R = 256, KEY_L = 512;

    private GameView view;
    private Process core;
    private OutputStream coreIn;
    private Thread reader;
    private Thread player;
    private AudioTrack audio;
    private final ArrayBlockingQueue<short[]> sound = new ArrayBlockingQueue<short[]>(8);
    private final Object pauseLock = new Object();
    private volatile boolean paused;
    private volatile boolean running;

    /* The keys: the touch controls' and the controllers' and keyboards'. */
    private int touchKeys;
    private int padKeys;
    private int stickKeys;
    private int sentKeys = -1;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        view = new GameView(this);
        setContentView(view);
        immersive();
        startCore();
    }

    private void immersive() {
        if (Build.VERSION.SDK_INT >= 19) {
            getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                    | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
        }
    }

    @Override
    public void onWindowFocusChanged(boolean focus) {
        super.onWindowFocusChanged(focus);
        if (focus) {
            immersive();
        }
    }

    /** The cores, in the order tried: musl's, then glibc's if musl's stops
     * before its first picture. */
    private static final String[] CORES = { "libcrashxs_core.so", "libcrashxs_gnu.so" };
    private int coreIndex;
    private Thread logThread;
    private volatile boolean gotPicture;
    /** What the cores tried before this one said when they stopped. */
    private final StringBuilder report = new StringBuilder();

    private void startCore() {
        if (audio == null) {
            int min = AudioTrack.getMinBufferSize(48000, AudioFormat.CHANNEL_OUT_MONO, AudioFormat.ENCODING_PCM_16BIT);
            audio = new AudioTrack(AudioManager.STREAM_MUSIC, 48000, AudioFormat.CHANNEL_OUT_MONO,
                    AudioFormat.ENCODING_PCM_16BIT, Math.max(min, 48000 / 60 * 2 * 6), AudioTrack.MODE_STREAM);
            audio.play();
            player = new Thread(new Runnable() {
                public void run() {
                    play();
                }
            }, "core-sound");
            player.start();
        }
        startCore(0);
    }

    private void startCore(int index) {
        coreIndex = index;
        gotPicture = false;
        running = true;
        File exe = new File(getApplicationInfo().nativeLibraryDir, CORES[index]);
        File save = new File(getFilesDir(), "crashbandicootxs.sav");
        final File logFile = new File(getFilesDir(), "core" + index + ".log");
        final Process p;

        try {
            if (!exe.exists()) {
                throw new IOException(exe + " is missing");
            }
            ProcessBuilder pb = new ProcessBuilder(exe.getAbsolutePath(), "--stream", "--save", save.getAbsolutePath());
            /* glibc's restartable sequences: not every Android kernel lets
             * an app use them (musl's core has none). */
            pb.environment().put("GLIBC_TUNABLES", "glibc.pthread.rseq=0");
            p = pb.start();
        } catch (Throwable e) {
            stopped(index, null, e);
            return;
        }
        core = p;
        /* What the game prints, to core<n>.log (shown if it stops). */
        final java.io.InputStream err = p.getErrorStream();
        logThread = new Thread(new Runnable() {
            public void run() {
                try {
                    java.io.FileOutputStream out = new java.io.FileOutputStream(logFile);
                    byte[] b = new byte[4096];
                    int n;
                    while ((n = err.read(b)) > 0) {
                        out.write(b, 0, n);
                        out.flush();
                    }
                    out.close();
                } catch (IOException e) {
                    /* closing */
                }
            }
        }, "core-log");
        logThread.start();
        synchronized (this) {
            coreIn = p.getOutputStream();
            sentKeys = -1;
        }
        sendKeys();
        reader = new Thread(new Runnable() {
            public void run() {
                read(p);
            }
        }, "core-reader");
        reader.start();
    }

    /** The core's messages: pictures to the view, sound to the player. */
    private void read(Process p) {
        DataInputStream in = new DataInputStream(new BufferedInputStream(p.getInputStream(), 1 << 17));
        byte[] head = new byte[8];

        try {
            while (running) {
                synchronized (pauseLock) {
                    while (paused && running) {
                        pauseLock.wait();
                    }
                }
                in.readFully(head);
                int type = (head[0] & 0xFF) | (head[1] & 0xFF) << 8 | (head[2] & 0xFF) << 16 | (head[3] & 0xFF) << 24;
                int n = (head[4] & 0xFF) | (head[5] & 0xFF) << 8 | (head[6] & 0xFF) << 16 | (head[7] & 0xFF) << 24;
                if (n < 0 || n > 1 << 20 || (type != 1 && type != 2)) {
                    throw new IOException("bad message from the game: type " + type + ", " + n + " bytes");
                }
                byte[] data = new byte[n];
                in.readFully(data);
                if (type == 1) {
                    if (n != GameView.W * GameView.H * 2) {
                        throw new IOException("bad picture from the game: " + n + " bytes");
                    }
                    gotPicture = true;
                    view.setFrame(data);
                } else {
                    short[] s = new short[n / 2];
                    for (int i = 0; i < s.length; i++) {
                        s[i] = (short) ((data[2 * i] & 0xFF) | data[2 * i + 1] << 8);
                    }
                    /* A full queue (the sound device slower than the game)
                     * drops the block. */
                    sound.offer(s);
                }
            }
        } catch (Throwable e) {
            if (running) {
                stopped(coreIndex, p, e);
            }
        }
    }

    /** A core stopped: the next core if this one never drew, else the
     * report on the screen. */
    private void stopped(int index, Process p, Throwable why) {
        StringBuilder r = new StringBuilder();

        r.append(CORES[index]).append(": ");
        if (why instanceof java.io.EOFException) {
            r.append("the game closed its output");
        } else {
            r.append(why.getClass().getSimpleName()).append(' ').append(why.getMessage());
        }
        if (p != null) {
            r.append(", ").append(exitOf(p));
            if (logThread != null) {
                try {
                    logThread.join(1500);
                } catch (InterruptedException e) {
                    /* go on */
                }
            }
            r.append('\n').append(logTail(new File(getFilesDir(), "core" + index + ".log")));
        }
        r.append('\n');
        report.append(r);
        if (!gotPicture && index + 1 < CORES.length && running) {
            startCore(index + 1);
            return;
        }
        String abi = Build.VERSION.SDK_INT >= 21 && Build.SUPPORTED_ABIS.length > 0 ? Build.SUPPORTED_ABIS[0] : Build.CPU_ABI;
        view.setError("The game stopped (Android app v0.02).\n" + report
                + "Android " + Build.VERSION.RELEASE + " (API " + Build.VERSION.SDK_INT + "), "
                + Build.MANUFACTURER + " " + Build.MODEL + ", " + abi);
    }

    /** How the process ended: its exit code, or the signal that ended it. */
    private static String exitOf(Process p) {
        for (int i = 0; i < 40; i++) {
            try {
                int code = p.exitValue();
                if (code > 128) {
                    return "killed by signal " + (code - 128) + signalName(code - 128);
                }
                return "exit code " + code;
            } catch (IllegalThreadStateException e) {
                try {
                    Thread.sleep(50);
                } catch (InterruptedException e2) {
                    break;
                }
            }
        }
        p.destroy();
        return "still running (stopped now)";
    }

    private static String signalName(int sig) {
        switch (sig) {
        case 4: return " (SIGILL)";
        case 6: return " (SIGABRT)";
        case 7: return " (SIGBUS)";
        case 9: return " (SIGKILL)";
        case 11: return " (SIGSEGV)";
        case 31: return " (SIGSYS, a system call the phone refused)";
        default: return "";
        }
    }

    /** The log's last lines. */
    private static String logTail(File log) {
        try {
            java.io.RandomAccessFile f = new java.io.RandomAccessFile(log, "r");
            long len = f.length();
            byte[] b = new byte[(int) Math.min(len, 600)];
            f.seek(len - b.length);
            f.readFully(b);
            f.close();
            String s = new String(b, "UTF-8").trim();
            String[] lines = s.split("\n");
            StringBuilder t = new StringBuilder();
            for (int i = Math.max(0, lines.length - 6); i < lines.length; i++) {
                t.append(t.length() > 0 ? "\n" : "").append("  ").append(lines[i]);
            }
            return t.length() > 0 ? t.toString() : "  (no log: it stopped before printing anything)";
        } catch (Exception e) {
            return "  (no log)";
        }
    }

    private void play() {
        try {
            while (true) {
                short[] s = sound.take();
                audio.write(s, 0, s.length);
            }
        } catch (InterruptedException e) {
            /* closing */
        }
    }

    /* --- The buttons --- */

    void setTouchKeys(int keys) {
        touchKeys = keys;
        sendKeys();
    }

    private synchronized void sendKeys() {
        int keys = touchKeys | padKeys | stickKeys;

        if (keys == sentKeys || coreIn == null) {
            return;
        }
        sentKeys = keys;
        try {
            coreIn.write(new byte[] { (byte) keys, (byte) (keys >> 8) });
            coreIn.flush();
        } catch (IOException e) {
            /* the game has stopped: the reader says so */
        }
    }

    /** A controller's or keyboard's key: the GBA's buttons by place (the
     * right face button is A, the bottom one B), and the PC port's keys. */
    private static int keyBit(int code) {
        switch (code) {
        case KeyEvent.KEYCODE_DPAD_UP: return KEY_UP;
        case KeyEvent.KEYCODE_DPAD_DOWN: return KEY_DOWN;
        case KeyEvent.KEYCODE_DPAD_LEFT: return KEY_LEFT;
        case KeyEvent.KEYCODE_DPAD_RIGHT: return KEY_RIGHT;
        case KeyEvent.KEYCODE_BUTTON_B: case KeyEvent.KEYCODE_X: return KEY_A;
        case KeyEvent.KEYCODE_BUTTON_A: case KeyEvent.KEYCODE_Z: return KEY_B;
        case KeyEvent.KEYCODE_BUTTON_L1: case KeyEvent.KEYCODE_BUTTON_L2: case KeyEvent.KEYCODE_A: return KEY_L;
        case KeyEvent.KEYCODE_BUTTON_R1: case KeyEvent.KEYCODE_BUTTON_R2: case KeyEvent.KEYCODE_S: return KEY_R;
        case KeyEvent.KEYCODE_BUTTON_START: case KeyEvent.KEYCODE_ENTER: return KEY_START;
        case KeyEvent.KEYCODE_BUTTON_SELECT: case KeyEvent.KEYCODE_DEL: return KEY_SELECT;
        default: return 0;
        }
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent e) {
        int bit = keyBit(e.getKeyCode());

        if (bit == 0) {
            return super.dispatchKeyEvent(e);
        }
        if (e.getAction() == KeyEvent.ACTION_DOWN) {
            padKeys |= bit;
            view.controllerUsed();
        } else if (e.getAction() == KeyEvent.ACTION_UP) {
            padKeys &= ~bit;
        }
        sendKeys();
        return true;
    }

    /** A controller's left stick, hat and triggers. */
    @Override
    public boolean dispatchGenericMotionEvent(MotionEvent e) {
        if ((e.getSource() & InputDevice.SOURCE_JOYSTICK) != InputDevice.SOURCE_JOYSTICK
                || e.getAction() != MotionEvent.ACTION_MOVE) {
            return super.dispatchGenericMotionEvent(e);
        }
        float x = e.getAxisValue(MotionEvent.AXIS_X) + e.getAxisValue(MotionEvent.AXIS_HAT_X);
        float y = e.getAxisValue(MotionEvent.AXIS_Y) + e.getAxisValue(MotionEvent.AXIS_HAT_Y);
        int keys = 0;

        if (x < -0.4f) keys |= KEY_LEFT;
        if (x > 0.4f) keys |= KEY_RIGHT;
        if (y < -0.4f) keys |= KEY_UP;
        if (y > 0.4f) keys |= KEY_DOWN;
        if (e.getAxisValue(MotionEvent.AXIS_LTRIGGER) > 0.4f || e.getAxisValue(MotionEvent.AXIS_BRAKE) > 0.4f) keys |= KEY_L;
        if (e.getAxisValue(MotionEvent.AXIS_RTRIGGER) > 0.4f || e.getAxisValue(MotionEvent.AXIS_GAS) > 0.4f) keys |= KEY_R;
        if (keys != 0) {
            view.controllerUsed();
        }
        stickKeys = keys;
        sendKeys();
        return true;
    }

    /* --- The app's life --- */

    @Override
    protected void onPause() {
        super.onPause();
        paused = true;
        touchKeys = padKeys = stickKeys = 0;
        sendKeys();
        if (audio != null) {
            audio.pause();
            audio.flush();
        }
        sound.clear();
    }

    @Override
    protected void onResume() {
        super.onResume();
        immersive();
        if (audio != null) {
            audio.play();
        }
        synchronized (pauseLock) {
            paused = false;
            pauseLock.notifyAll();
        }
    }

    @Override
    protected void onDestroy() {
        running = false;
        synchronized (pauseLock) {
            pauseLock.notifyAll();
        }
        if (core != null) {
            core.destroy();
        }
        if (player != null) {
            player.interrupt();
        }
        if (audio != null) {
            audio.release();
        }
        super.onDestroy();
    }
}
