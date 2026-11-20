package com.crashbandicootxs.port;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;
import android.os.Build;
import android.os.Bundle;
import android.view.Display;
import android.view.Gravity;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

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
 * app may run it), with the game's data blanked out: the first time, the
 * player picks their ROM (the import screen), and GameData makes each
 * core's data from it. The core runs as a child process with --stream
 * (platform/pc/src/stream.c): it writes the pictures, the sound, its touch
 * mode, its settings and toasts to its stdout and reads the buttons, taps,
 * back and the rest from its stdin, at the GBA's own pace. The settings
 * menu is the core's, drawn in the game's style.
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

    private static final int PICK_ROM = 1;
    private TextView importStatus;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        immersive();
        if (GameData.haveRom(this)) {
            startGame();
        } else {
            showImport(null);
        }
    }

    /* --- The ROM --- */

    /** The first screen: what the app needs, and the button to pick it. */
    private void showImport(String message) {
        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setGravity(Gravity.CENTER);
        box.setBackgroundColor(Color.rgb(12, 16, 32));
        int pad = (int) (24 * getResources().getDisplayMetrics().density);
        box.setPadding(pad, pad, pad, pad);

        TextView title = new TextView(this);
        title.setText("Crash Bandicoot XS");
        title.setTextColor(Color.rgb(255, 196, 64));
        title.setTextSize(28);
        title.setGravity(Gravity.CENTER);
        box.addView(title);

        TextView about = new TextView(this);
        about.setText("N. Larged XS " + version() + "\n\nThis app doesn't include the game. Pick your own copy: "
                + "Crash Bandicoot XS (Europe) (En,Fr,De,Es,It,Nl), a .gba file. It's kept in the app's own "
                + "storage, so you only do this once.");
        about.setTextColor(Color.WHITE);
        about.setTextSize(16);
        about.setGravity(Gravity.CENTER);
        about.setPadding(0, pad / 2, 0, pad / 2);
        box.addView(about);

        Button pick = new Button(this);
        pick.setText("Import ROM");
        pick.setTextSize(20);
        pick.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) {
                pickRom();
            }
        });
        box.addView(pick);

        importStatus = new TextView(this);
        importStatus.setTextColor(Color.rgb(255, 140, 120));
        importStatus.setTextSize(14);
        importStatus.setGravity(Gravity.CENTER);
        importStatus.setPadding(0, pad / 2, 0, 0);
        importStatus.setText(message != null ? message : "");
        box.addView(importStatus);
        setContentView(box);
    }

    private String version() {
        try {
            return "v" + getPackageManager().getPackageInfo(getPackageName(), 0).versionName;
        } catch (Exception e) {
            return "";
        }
    }

    private void pickRom() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        try {
            startActivityForResult(i, PICK_ROM);
        } catch (Exception e) {
            importStatus.setText("No file picker on this phone: " + e.getMessage());
        }
    }

    @Override
    protected void onActivityResult(int request, int result, final Intent data) {
        if (request != PICK_ROM || result != RESULT_OK || data == null || data.getData() == null) {
            return;
        }
        importStatus.setTextColor(Color.WHITE);
        importStatus.setText("Checking the ROM...");
        new Thread(new Runnable() {
            public void run() {
                String problem;
                try {
                    String[] why = new String[1];
                    byte[] rom = GameData.readRom(getContentResolver().openInputStream(data.getData()), why);
                    if (rom == null) {
                        problem = why[0];
                    } else {
                        GameData.keepRom(MainActivity.this, rom);
                        problem = null;
                    }
                } catch (Exception e) {
                    problem = "Couldn't read that file: " + e.getMessage();
                }
                final String p = problem;
                runOnUiThread(new Runnable() {
                    public void run() {
                        if (p != null) {
                            importStatus.setTextColor(Color.rgb(255, 140, 120));
                            importStatus.setText(p);
                        } else {
                            startGame();
                        }
                    }
                });
            }
        }, "rom-import").start();
    }

    /* --- The game --- */

    private void startGame() {
        ((android.hardware.input.InputManager) getSystemService(INPUT_SERVICE)).registerInputDeviceListener(devices, null);
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
            File data = null;
            for (String abi : abis()) {
                data = GameData.dataFor(this, abi, CORES[index]);
                if (data != null) {
                    break;
                }
            }
            if (data == null) {
                throw new IOException("no game data package for " + CORES[index]);
            }
            ProcessBuilder pb = new ProcessBuilder(exe.getAbsolutePath(), "--stream", "--save", save.getAbsolutePath());
            /* glibc's restartable sequences: not every Android kernel lets
             * an app use them (musl's core has none). */
            pb.environment().put("GLIBC_TUNABLES", "glibc.pthread.rseq=0");
            pb.environment().put("CRASHXS_DIR", getFilesDir().getAbsolutePath());
            pb.environment().put("CRASHXS_DATA", data.getAbsolutePath());
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
        send(5, displayHz(), 0);
        send(6, padStyle, 0);
        sendControllers();
        send(9, layoutSent[0], layoutSent[1], layoutSent[2]);
        send(10, layoutSent[3], layoutSent[4], 0);
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
                if (n < 0 || n > 4 << 20 || type < 1 || type > 9) {
                    throw new IOException("bad message from the game: type " + type + ", " + n + " bytes");
                }
                byte[] data = new byte[n];
                in.readFully(data);
                if (type == 1) {
                    int w;
                    if (n == GameView.W * GameView.H * 2) {
                        w = GameView.W;
                    } else if (n == GameView.W * GameView.H * 8) {
                        w = GameView.W * 2;
                    } else {
                        throw new IOException("bad picture from the game: " + n + " bytes");
                    }
                    gotPicture = true;
                    view.setFrame(data, w);
                } else if (type == 3) {
                    view.setMode(data[0]);
                } else if (type == 4) {
                    final int[] v = new int[n / 4];
                    for (int i = 0; i < v.length; i++) {
                        v[i] = (data[4 * i] & 0xFF) | (data[4 * i + 1] & 0xFF) << 8 | (data[4 * i + 2] & 0xFF) << 16
                                | (data[4 * i + 3] & 0xFF) << 24;
                    }
                    view.setSettings(v);
                    runOnUiThread(new Runnable() {
                        public void run() {
                            frameRate(v[6]);
                        }
                    });
                } else if (type == 6) {
                    int[] h = new int[4];
                    for (int i = 0; i < 4; i++) {
                        h[i] = (data[4 * i] & 0xFF) | (data[4 * i + 1] & 0xFF) << 8 | (data[4 * i + 2] & 0xFF) << 16
                                | (data[4 * i + 3] & 0xFF) << 24;
                    }
                    if (n == 16 + h[2] * h[3] * 2) {
                        view.setBar(h[0], h[1], h[2], h[3], data, 16);
                    }
                } else if (type == 9) {
                    if (n >= 4) {
                        vibrate(data[0] & 0xFF, (data[2] & 0xFF) | (data[3] & 0xFF) << 8);
                    }
                } else if (type == 7) {
                    view.setIcons(data);
                } else if (type == 8) {
                    runOnUiThread(new Runnable() {
                        public void run() {
                            view.startEditing();
                        }
                    });
                } else if (type == 5) {
                    final String t = new String(data, "UTF-8");
                    runOnUiThread(new Runnable() {
                        public void run() {
                            Toast.makeText(MainActivity.this, t, Toast.LENGTH_SHORT).show();
                        }
                    });
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
        view.setError("The game stopped (" + version() + ").\n" + report
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

    private void sendKeys() {
        int keys = touchKeys | padKeys | stickKeys;

        if (keys == sentKeys || coreIn == null) {
            return;
        }
        sentKeys = keys;
        send(1, keys, 0);
    }

    /** A message to the core (stream.c): its type and two values. */
    private void send(int type, int a, int b) {
        send(type, a, b, 0);
    }

    private synchronized void send(int type, int a, int b, int c) {
        if (coreIn == null) {
            return;
        }
        try {
            coreIn.write(new byte[] { (byte) type, 0, (byte) a, (byte) (a >> 8), (byte) b, (byte) (b >> 8), (byte) c,
                    (byte) (c >> 8) });
            coreIn.flush();
        } catch (IOException e) {
            /* the game has stopped: the reader says so */
        }
    }

    void sendTap(int x, int y) {
        send(2, x, y);
    }

    void sendBack() {
        send(3, 0, 0);
    }

    void openSettings() {
        send(4, 0, 0);
    }

    void sendScroll(int steps) {
        send(7, steps, 0);
    }

    private int[] layoutSent = new int[5];

    /** The room around the picture, for the borders (stream.c's 9 and 10). */
    void sendLayout(int left, int right, int height, int pictureW, int top) {
        layoutSent = new int[] { left, right, height, pictureW, top };
        send(9, left, right, height);
        send(10, pictureW, top, 0);
    }

    /* --- Controllers connected: the warp room's hint says "tap" without --- */

    private boolean controllerConnected() {
        for (int id : InputDevice.getDeviceIds()) {
            InputDevice d = InputDevice.getDevice(id);
            if (d == null || (Build.VERSION.SDK_INT >= 16 && d.isVirtual())) {
                continue;
            }
            int s = d.getSources();
            if ((s & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
                    || (s & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK) {
                return true;
            }
        }
        return false;
    }

    private void sendControllers() {
        send(8, controllerConnected() ? 1 : 0, 0);
    }

    private final android.hardware.input.InputManager.InputDeviceListener devices =
            new android.hardware.input.InputManager.InputDeviceListener() {
        public void onInputDeviceAdded(int id) {
            sendControllers();
        }

        public void onInputDeviceRemoved(int id) {
            sendControllers();
        }

        public void onInputDeviceChanged(int id) {
            sendControllers();
        }
    };

    /* --- The phone's back button: the game's B, pause or the menu's back --- */

    @Override
    public void onBackPressed() {
        if (view != null && view.isEditing()) {
            view.finishEditing(); /* the layout editor: done */
        } else if (view != null && core != null) {
            sendBack();
        } else {
            super.onBackPressed();
        }
    }

    /* --- The button prompts: the controller in use, or the touch screen --- */

    static final int PROMPTS_GBA = 1, PROMPTS_XBOX = 2, PROMPTS_PLAYSTATION = 3, PROMPTS_NINTENDO = 4;
    private int padStyle = PROMPTS_GBA;

    private void setPadStyle(int style) {
        if (style != padStyle) {
            padStyle = style;
            send(6, style, 0);
        }
    }

    void touchUsed() {
        lastPad = null;
        setPadStyle(PROMPTS_GBA);
    }

    /* --- Vibration (Touch's vibration; the core's message 9, rumble.c) --- */

    /** The controller used last, or null for the touch screen. */
    private volatile InputDevice lastPad;

    /** A buzz: the controller in use if it can, else the phone. */
    private void vibrate(int strength, int ms) {
        android.os.Vibrator v = null;
        InputDevice pad = lastPad;

        if (pad != null && Build.VERSION.SDK_INT >= 16) {
            android.os.Vibrator pv = pad.getVibrator();
            if (pv != null && pv.hasVibrator()) {
                v = pv;
            }
        }
        if (v == null) {
            v = (android.os.Vibrator) getSystemService(VIBRATOR_SERVICE);
        }
        if (v == null || !v.hasVibrator() || ms <= 0) {
            return;
        }
        try {
            if (Build.VERSION.SDK_INT >= 26) {
                /* VibrationEffect is newer than the API this is built
                 * against (build.sh's android-23): by reflection */
                Class<?> effect = Class.forName("android.os.VibrationEffect");
                boolean amplitude = (Boolean) android.os.Vibrator.class.getMethod("hasAmplitudeControl").invoke(v);
                int amp = amplitude ? Math.max(1, Math.min(255, strength)) : -1; /* -1: DEFAULT_AMPLITUDE */
                Object one = effect.getMethod("createOneShot", long.class, int.class).invoke(null, (long) ms, amp);
                android.os.Vibrator.class.getMethod("vibrate", effect).invoke(v, one);
            } else {
                /* no strength: a weak one is shorter */
                v.vibrate(Math.max(20, ms * strength / 255));
            }
        } catch (Exception e) {
            /* a vibrator that went away */
        }
    }

    private void controllerUsed(InputDevice d) {
        lastPad = d;
        int style = PROMPTS_XBOX;
        if (d != null && Build.VERSION.SDK_INT >= 19) {
            int vendor = d.getVendorId();
            if (vendor == 0x054c) {
                style = PROMPTS_PLAYSTATION;
            } else if (vendor == 0x057e) {
                style = PROMPTS_NINTENDO;
            }
        }
        setPadStyle(style);
        view.controllerUsed();
    }

    /* --- The display: 60 or 120 a second --- */

    private Display.Mode fastest() {
        if (Build.VERSION.SDK_INT < 23) {
            return null;
        }
        Display d = getWindowManager().getDefaultDisplay();
        Display.Mode now = d.getMode();
        Display.Mode best = null;
        for (Display.Mode m : d.getSupportedModes()) {
            if (m.getPhysicalWidth() == now.getPhysicalWidth() && m.getPhysicalHeight() == now.getPhysicalHeight()
                    && (best == null || m.getRefreshRate() > best.getRefreshRate())) {
                best = m;
            }
        }
        return best;
    }

    private int displayHz() {
        Display.Mode m = fastest();
        if (m != null) {
            return Math.round(m.getRefreshRate());
        }
        return Math.round(getWindowManager().getDefaultDisplay().getRefreshRate());
    }

    /** At 120 the screen's fastest mode; at 60 the phone's own choice. */
    private void frameRate(int fps) {
        if (Build.VERSION.SDK_INT < 23) {
            return;
        }
        Display.Mode m = fastest();
        WindowManager.LayoutParams lp = getWindow().getAttributes();
        int want = fps >= 120 && m != null ? m.getModeId() : 0;
        if (lp.preferredDisplayModeId != want) {
            lp.preferredDisplayModeId = want;
            getWindow().setAttributes(lp);
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

    /** The phone's ABIs, the APK's packages are in assets/ABI/. */
    private static String[] abis() {
        if (Build.VERSION.SDK_INT >= 21 && Build.SUPPORTED_ABIS.length > 0) {
            return Build.SUPPORTED_ABIS;
        }
        return new String[] { Build.CPU_ABI };
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent e) {
        int bit = keyBit(e.getKeyCode());

        /* a controller's Guide button: the settings menu, as on the PC */
        if (view != null && e.getKeyCode() == KeyEvent.KEYCODE_BUTTON_MODE) {
            if (e.getAction() == KeyEvent.ACTION_DOWN && e.getRepeatCount() == 0) {
                openSettings();
            }
            return true;
        }

        if (bit == 0) {
            return super.dispatchKeyEvent(e);
        }
        if (view == null) {
            return super.dispatchKeyEvent(e);
        }
        if (e.getAction() == KeyEvent.ACTION_DOWN) {
            padKeys |= bit;
            controllerUsed(e.getDevice());
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
        if (view == null) {
            return super.dispatchGenericMotionEvent(e);
        }
        if (keys != 0) {
            controllerUsed(e.getDevice());
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
