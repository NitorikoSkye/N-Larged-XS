package com.crashbandicootxs.port;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Path;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.view.HapticFeedbackConstants;
import android.view.MotionEvent;
import android.view.View;

import java.io.InputStream;
import java.nio.ByteBuffer;

/**
 * The game's picture and the touch controls (platform/android/README.md).
 *
 * The picture is scaled as the settings say (fit, whole multiples, or
 * stretched), with Display's filter as on PC: pixels, smooth, sharp smooth
 * (whole pixels up, then smoothly down), the scalers (Scale2x, Scale3x, the
 * cartoon xBRZ: the core sends their 2x or 3x pictures, and LCD ghosting
 * blended in) and the scanlines' and LCD grid's mask over it. The core's
 * pictures are 240x160, or 480x320 at 120 frames a second, or the
 * scalers'.
 *
 * What a touch does follows the core's touch mode (stream.c): in a level,
 * the GBA's buttons on the screen (the D-pad on the left, A and B on the
 * right as on the GBA, L and R in the top corners, Select and Start at the
 * bottom; several fingers at once, sliding between buttons, the D-pad's
 * diagonals); anywhere else (the menus, the warp room, the settings menu)
 * they're hidden and a tap goes to the game where it lands, a swipe up or
 * down scrolls. "In menus: always" keeps the buttons in the menus too.
 * The settings are opened from the pause menu's "settings" row or the warp
 * room's cog (the core's, mouse_hooks.cpp), or a controller's Guide
 * button; while they're open a close button sits at the top middle. A controller in use hides
 * the buttons until the screen is touched again. The buttons show what
 * do (icons/*.svg, white), and follow the game (the core's message 7,
 * touch_context.cpp): A jumps, and is the double jump in the air while it
 * can be done; R crouches, slides while running and slams in the air; B
 * spins; L is the turbo run; underwater A swims a stroke, on the polar
 * ride B dashes, with the jetpack A fires and L and R roll. An icon
 * changes with a short fade; a button with nothing to do now is faint.
 * Touch's icons "fixed" keeps jump, spin, turbo and slam. Start pauses,
 * Select shows the counters. Touch's d-pad is the GBA's cross, a
 * joystick (a knob that follows the thumb; eight ways past a small dead
 * zone) or a separate button each way; floating, it appears wherever the
 * left thumb lands on the left half of the screen (and rests, faint, at
 * its place). Touch's vibration buzzes the controller in use, or the
 * phone (the core's message 9; MainActivity). Holding the turbo run's button for a moment
 * locks it on (Auto, for the time trials): it stays held, in levels,
 * until the button is tapped again.
 *
 * The layout editor (Touch's layout; the core's message 8) moves and
 * resizes each control: drag one, pinch it or use - and +; Reset puts
 * them all back, Done (or back) keeps the layout (the app's preferences,
 * "touch_layout": each control's centre as a share of the screen and its
 * size as a multiple of the default, which Touch's size still scales).
 *
 * Beside the picture, the borders: black, ambient (the picture's colours,
 * blurred and darkened, across the screen) or themed (the core's bars,
 * borders.c, as the PC port draws them: the app says how much room there
 * is, the core sends the bars in their own pixels and their size).
 */
public class GameView extends View {
    static final int W = 240, H = 160;
    static final int MODE_PLAY = 0, MODE_MENU = 1, MODE_SETTINGS = 2, MODE_ANY = 3;

    private final MainActivity activity;
    /* The pictures by their scale: 1x, 2x (120 a second, Scale2x, cartoon
     * soft), 3x (Scale3x, cartoon), made as they first come. */
    private final Bitmap[] byScale = new Bitmap[4];
    private Bitmap shown = picture(1);
    private volatile byte[] pending;
    private volatile int pendingW = W;
    private volatile String error;
    private final Paint pixels = new Paint();
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint line = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint errorText = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint errorBack = new Paint();
    private final Rect gameRect = new Rect();
    private final Rect sentRect = new Rect();
    private int sentH;

    /* The settings the core sends (stream.c's settings message). */
    private int touchSize = 1;
    private int touchOpacity = 60;
    private boolean touchInMenus;
    private int haptics = 1;
    private int scaling;
    /* Display's filter (HOST_FILTER_*: 0 pixels, 1 smooth, 2 sharp smooth,
     * 3 Scale2x, 4 Scale3x, 5 cartoon, 6 cartoon soft; the scalers and the
     * ghosting are the core's, the scaling and the masks the app's),
     * scanlines (0-2) and the LCD grid. */
    private int filter;
    private int scanlines;
    private boolean lcdGrid;
    private Bitmap mask;      /* the scanlines' and grid's, at the picture's size */
    private int maskKind = -1;
    private Bitmap sharp;     /* sharp smooth's whole-pixel step */
    private Canvas sharpCanvas;
    private final Paint multiply = new Paint();
    private volatile int mode = MODE_ANY;
    private int borders = 2;
    /* Touch's d-pad: 0 the cross, 1 a joystick, 2 a button each way; and
     * whether it appears where the left thumb lands (floating) */
    static final int DPAD_CROSS = 0, DPAD_STICK = 1, DPAD_BUTTONS = 2;
    private int dpadStyle;
    private boolean dpadFloating;
    /* The finger working the d-pad by its offset from an anchor (the
     * joystick, or any floating style), or -1; where it is. */
    private int padOwner = -1;
    private float anchorX, anchorY, thumbX, thumbY;

    /* The themed borders' bars (0 left, 1 right, 2 above, flipped below)
     * and their pixel sizes; the ambient border's tiny picture. */
    private final Bitmap[] bars = new Bitmap[3];
    private final int[] barUnit = new int[3];
    private final Bitmap glow = Bitmap.createBitmap(8, 5, Bitmap.Config.RGB_565);
    private final Canvas glowCanvas = new Canvas(glow);
    private final Paint soft = new Paint(Paint.FILTER_BITMAP_FLAG);
    private final Paint shade = new Paint();

    /* The buttons' icons: by the core's HOST_ICON_* ids (host_ui.h), then
     * Start's and Select's. */
    private static final String[] ICON_NAMES = { null, "jump", "double", "spin", "crouch", "slide", "slam", "turbo",
            "stroke", "shoot", "roll_left", "roll_right", "dash" };
    static final int ICON_JUMP = 1, ICON_SPIN = 3, ICON_SLAM = 6, ICON_TURBO = 7, ICON_DIM = 0x80;
    private final Bitmap[] icons = new Bitmap[ICON_NAMES.length];
    private final Bitmap iconPause, iconCounters;
    /* A, B, L and R's icons now and before, and when they changed (the fade). */
    private static final int FADE_MS = 140;
    private static final int[] DEFAULT_ICONS = { ICON_JUMP, ICON_SPIN, ICON_TURBO, ICON_SLAM };
    private final int[] iconNow = DEFAULT_ICONS.clone();
    private final int[] iconWas = DEFAULT_ICONS.clone();
    private final long[] iconAt = new long[4];
    private final Paint iconPaint = new Paint(Paint.FILTER_BITMAP_FLAG | Paint.ANTI_ALIAS_FLAG);
    private final RectF iconRect = new RectF();

    /* The controls' places (set by layout). */
    static final int C_PAD = 0, C_A = 1, C_B = 2, C_L = 3, C_R = 4, C_SELECT = 5, C_START = 6, CONTROLS = 7;
    private float padX, padY, padR;
    private float aX, aY, aR, bX, bY, bR;
    private float gearX, gearY, gearR;
    private final RectF lRect = new RectF(), rRect = new RectF(), selRect = new RectF(), startRect = new RectF();
    private int keys;        /* sent: the fingers' and the lock's */
    private int touchKeys;   /* the fingers' alone */
    private boolean controllerHidden;

    /* Auto: L locked on by holding its button (TURBO_HOLD_MS). */
    private static final int TURBO_HOLD_MS = 600;
    private boolean turboLocked;
    private final Runnable turboHold = new Runnable() {
        public void run() {
            if ((touchKeys & MainActivity.KEY_L) != 0 && !turboLocked) {
                turboLocked = true;
                performHapticFeedback(HapticFeedbackConstants.LONG_PRESS,
                        HapticFeedbackConstants.FLAG_IGNORE_GLOBAL_SETTING);
                sendKeys();
            }
        }
    };

    /* A finger over the picture with the buttons hidden: a tap or a swipe. */
    private int swipeId = -1;
    private float swipeX, swipeY, swipeLastY;
    private boolean swiped;
    private int gearId = -1;

    public GameView(MainActivity activity) {
        super(activity);
        this.activity = activity;
        line.setStyle(Paint.Style.STROKE);
        text.setTextAlign(Paint.Align.CENTER);
        text.setTypeface(Typeface.DEFAULT_BOLD);
        setBackgroundColor(Color.BLACK);
        setHapticFeedbackEnabled(true);
        shade.setColor(Color.argb(150, 0, 0, 0));
        for (int i = 1; i < ICON_NAMES.length; i++) {
            icons[i] = icon(activity, ICON_NAMES[i]);
        }
        iconPause = icon(activity, "pause");
        iconCounters = icon(activity, "counters");
        loadLayout();
    }

    private static Bitmap icon(MainActivity a, String name) {
        try {
            InputStream in = a.getAssets().open("icons/" + name + ".png");
            try {
                return BitmapFactory.decodeStream(in);
            } finally {
                in.close();
            }
        } catch (Exception e) {
            return null;
        }
    }

    /** A themed border's bar from the core (RGB565, its own pixels). */
    void setBar(int side, int unit, int w, int h, byte[] data, int offset) {
        if (side < 0 || side > 2 || w <= 0 || h <= 0) {
            return;
        }
        Bitmap b = Bitmap.createBitmap(w, h, Bitmap.Config.RGB_565);
        b.copyPixelsFromBuffer(ByteBuffer.wrap(data, offset, w * h * 2));
        bars[side] = b;
        barUnit[side] = unit;
        postInvalidate();
    }

    /** A picture from the game (RGB565, little-endian: the bitmap's own order). */
    void setFrame(byte[] data, int width) {
        pendingW = width;
        pending = data;
        postInvalidateOnAnimation();
    }

    /** The core's icons for A, B, L and R (HOST_ICON_*, HOST_ICON_DIM). */
    void setIcons(final byte[] b) {
        post(new Runnable() {
            public void run() {
                long now = android.os.SystemClock.uptimeMillis();
                for (int i = 0; i < 4 && i < b.length; i++) {
                    int v = b[i] & 0xFF;
                    if ((v & 0x7F) >= ICON_NAMES.length) {
                        continue;
                    }
                    if (v != iconNow[i]) {
                        iconWas[i] = iconNow[i];
                        iconNow[i] = v;
                        iconAt[i] = now;
                    }
                }
                invalidate();
            }
        });
    }

    void setError(String message) {
        error = message;
        postInvalidate();
    }

    void setMode(int m) {
        mode = m;
        post(new Runnable() {
            public void run() {
                if (!controlsShown()) {
                    touchKeys = 0;
                }
                sendKeys();
            }
        });
    }

    /** The keys to the game: the fingers', and L while it is locked on in a
     * level. */
    private void sendKeys() {
        int k = touchKeys;
        if (turboLocked && mode == MODE_PLAY && !controllerHidden) {
            k |= MainActivity.KEY_L;
        }
        if (k != keys) {
            keys = k;
            activity.setTouchKeys(k);
        }
        invalidate();
    }

    void setSettings(int[] v) {
        touchSize = v[0];
        touchOpacity = v[1];
        touchInMenus = v[2] != 0;
        haptics = v[3];
        scaling = v[4];
        filter = v[5];
        scanlines = v.length > 10 ? v[10] : 0;
        lcdGrid = v.length > 11 && v[11] != 0;
        borders = v.length > 7 ? v[7] : 0;
        dpadStyle = v.length > 8 ? Math.max(0, Math.min(2, v[8])) : 0;
        dpadFloating = v.length > 9 && v[9] != 0;
        post(new Runnable() {
            public void run() {
                layout(getWidth(), getHeight());
                invalidate();
            }
        });
    }

    void controllerUsed() {
        if (!controllerHidden) {
            controllerHidden = true;
            post(new Runnable() {
                public void run() {
                    sendKeys();
                }
            });
        }
    }

    private boolean controlsShown() {
        return !controllerHidden && mode != MODE_SETTINGS && (mode == MODE_PLAY || touchInMenus);
    }

    @Override
    protected void onSizeChanged(int w, int h, int ow, int oh) {
        layout(w, h);
    }

    private void layout(int w, int h) {
        if (w == 0 || h == 0) {
            return;
        }
        float u = Math.min(w, h) / 100f;
        int gw, gh;

        if (scaling == 2) { /* stretch */
            gw = w;
            gh = h;
        } else {
            if ((long) w * H > (long) h * W) {
                gh = h;
                gw = h * W / H;
            } else {
                gw = w;
                gh = w * H / W;
            }
            int k = gh / H;
            if (scaling == 1 && k >= 1) { /* integer */
                gw = W * k;
                gh = H * k;
            } else if (scaling == 0 && k >= 2 && k * H >= gh * 0.9f) {
                /* fit: whole multiples when that loses little */
                gw = W * k;
                gh = H * k;
            }
        }
        gameRect.set((w - gw) / 2, (h - gh) / 2, (w - gw) / 2 + gw, (h - gh) / 2 + gh);
        /* the borders' room, when it changed (not as the editor moves a
         * button) */
        if (!gameRect.equals(sentRect) || h != sentH) {
            sentRect.set(gameRect);
            sentH = h;
            activity.sendLayout(gameRect.left, w - gameRect.right, h, gw, gameRect.top);
        }

        float size = new float[] { 0.8f, 1f, 1.25f, 1.5f }[Math.max(0, Math.min(3, touchSize))];
        float s = u * size;
        padR = 17 * s;
        padX = Math.max(padR + 5 * u, gameRect.left / 2f);
        padY = h - padR - 16 * s;
        float btnR = 8.5f * s;
        float side = (w - gameRect.right) / 2f;
        aX = w - Math.max(btnR + 4 * u, side - 9 * s);
        if (side < 30 * s) {
            aX = w - btnR - 5 * u;
        }
        aY = padY - 7 * s;
        aR = bR = btnR;
        bX = aX - 20 * s;
        bY = padY + 7 * s;
        lRect.set(2 * u, 3 * u, 2 * u + 24 * s, 3 * u + 9 * s);
        rRect.set(w - 2 * u - 24 * s, 3 * u, w - 2 * u, 3 * u + 9 * s);
        selRect.set(w / 2f - 6 * u - 24 * s, h - 2.5f * u - 5.5f * s, w / 2f - 6 * u, h - 2.5f * u);
        startRect.set(w / 2f + 6 * u, h - 2.5f * u - 5.5f * s, w / 2f + 6 * u + 24 * s, h - 2.5f * u);
        applyLayout(w, h);
        gearR = 4.5f * u;
        gearX = w / 2f;
        gearY = 3 * u + gearR;
        line.setStrokeWidth(0.6f * u);
        text.setTextSize(4.2f * s);
        errorText.setTextSize(Math.max(2.4f * u, 11f));
        errorText.setColor(Color.WHITE);
        errorText.setTypeface(Typeface.MONOSPACE);
        errorBack.setColor(Color.argb(215, 0, 0, 0));
    }

    @Override
    protected void onDraw(Canvas c) {
        byte[] p = pending;

        if (p != null) {
            shown = picture(Math.max(1, Math.min(3, pendingW / W)));
            shown.copyPixelsFromBuffer(ByteBuffer.wrap(p));
            pending = null;
        }
        pixels.setFilterBitmap(false);
        drawBorders(c);
        drawPicture(c);
        if (error != null) {
            drawError(c, error);
            return;
        }
        if (editing) {
            drawEditor(c);
            return;
        }
        if (controlsShown()) {
            drawControls(c);
        }
        if (mode == MODE_SETTINGS) {
            drawGear(c); /* the settings menu's close button */
        }
    }

    private Bitmap picture(int k) {
        if (byScale[k] == null) {
            byScale[k] = Bitmap.createBitmap(W * k, H * k, Bitmap.Config.RGB_565);
        }
        return byScale[k];
    }

    /** The picture into its place as Display's filter scales it, then the
     * scanlines' and LCD grid's mask multiplied over it (as on PC, sdl.c). */
    private void drawPicture(Canvas c) {
        int gw = gameRect.width(), gh = gameRect.height();
        boolean linear = filter == 1 || filter == 5 || filter == 6;

        if (filter == 2 && (gw > shown.getWidth() || gh > shown.getHeight())) {
            /* sharp smooth: whole pixels up to at or above the place's
             * size, then smoothly down to it: square pixels, soft edges */
            int k = Math.max(1, (gh + shown.getHeight() - 1) / shown.getHeight());
            int tw = shown.getWidth() * k, th = shown.getHeight() * k;
            if (tw > 4096 || th > 4096) {
                k = Math.max(1, 4096 / shown.getWidth());
                tw = shown.getWidth() * k;
                th = shown.getHeight() * k;
            }
            if (sharp == null || sharp.getWidth() != tw || sharp.getHeight() != th) {
                sharp = Bitmap.createBitmap(tw, th, Bitmap.Config.RGB_565);
                sharpCanvas = new Canvas(sharp);
            }
            pixels.setFilterBitmap(false);
            sharpCanvas.drawBitmap(shown, null, new Rect(0, 0, tw, th), pixels);
            pixels.setFilterBitmap(true);
            c.drawBitmap(sharp, null, gameRect, pixels);
        } else {
            pixels.setFilterBitmap(linear);
            c.drawBitmap(shown, null, gameRect, pixels);
        }
        int kind = scanlines + (lcdGrid ? 3 : 0);
        if (kind != 0 && gw > 0 && gh > 0) {
            if (mask == null || maskKind != kind || mask.getWidth() != gw || mask.getHeight() != gh) {
                mask = makeMask(gw, gh, kind);
                maskKind = kind;
                multiply.setXfermode(new android.graphics.PorterDuffXfermode(android.graphics.PorterDuff.Mode.MULTIPLY));
            }
            c.drawBitmap(mask, gameRect.left, gameRect.top, multiply);
        }
    }

    /** The mask at the picture's size on the screen: each GBA row (and
     * column) gets its line however it is scaled (sdl.c's MaskTexture). */
    private static Bitmap makeMask(int w, int h, int kind) {
        int[] row = new int[w];
        Bitmap m = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
        double[] colGrid = new double[w];

        for (int x = 0; x < w; x++) {
            double fx = (x + 0.5) * W / w;
            colGrid[x] = kind >= 3 && fx - Math.floor(fx) > 0.78 ? 0.80 : 1.0;
        }
        for (int y = 0; y < h; y++) {
            double fy = (y + 0.5) * H / h;
            double ry = fy - Math.floor(fy);
            double v = 1.0;
            if (kind % 3 != 0) {
                /* scanlines: the bottom third of each row, light or dark */
                double depth = kind % 3 == 1 ? 0.30 : 0.55;
                if (ry > 0.67) {
                    v = 1.0 - depth;
                } else if (ry > 0.55) {
                    v = 1.0 - depth / 2;
                }
            }
            if (kind >= 3 && ry > 0.78) {
                v *= 0.80; /* the LCD grid: the gaps at each pixel's right and bottom */
            }
            for (int x = 0; x < w; x++) {
                int g = (int) (v * colGrid[x] * 255 + 0.5);
                row[x] = 0xFF000000 | g << 16 | g << 8 | g;
            }
            m.setPixels(row, 0, w, 0, y, w, 1);
        }
        return m;
    }

    private void drawBorders(Canvas c) {
        int w = getWidth(), h = getHeight();

        if (borders == 1) {
            /* the picture's colours, blurred: drawn down to 8x5, then up
             * across the screen, smoothly, and darkened */
            glowCanvas.drawBitmap(shown, null, new Rect(0, 0, 8, 5), soft);
            c.drawBitmap(glow, null, new Rect(0, 0, w, h), soft);
            c.drawRect(0, 0, w, h, shade);
            return;
        }
        if (borders != 2) {
            return;
        }
        Bitmap top = bars[2];
        if (top != null && gameRect.top > 0) {
            int u = barUnit[2];
            int bw = top.getWidth() * u, bh = top.getHeight() * u;
            int x = gameRect.left + (gameRect.width() - bw) / 2;
            if (gameRect.left <= 0) {
                x = 0;
            }
            c.drawBitmap(top, null, new Rect(x, gameRect.top - bh, x + bw, gameRect.top), pixels);
            c.save();
            c.scale(1, -1, 0, gameRect.bottom + bh / 2f);
            c.drawBitmap(top, null, new Rect(x, gameRect.bottom, x + bw, gameRect.bottom + bh), pixels);
            c.restore();
        }
        for (int side = 0; side < 2; side++) {
            Bitmap b = bars[side];
            int room = side == 0 ? gameRect.left : w - gameRect.right;
            if (b == null || room <= 0) {
                continue;
            }
            int u = barUnit[side];
            int bw = b.getWidth() * u, bh = b.getHeight() * u;
            int x = side == 0 ? gameRect.left - bw : gameRect.right;
            int y = (h - bh) / 2;
            c.drawBitmap(b, null, new Rect(x, y, x + bw, y + bh), pixels);
        }
    }

    /** The message, lines wrapped to the screen, on a dark box in the middle. */
    private void drawError(Canvas c, String message) {
        float width = getWidth() * 0.86f;
        java.util.ArrayList<String> lines = new java.util.ArrayList<String>();

        for (String l : message.split("\n")) {
            while (l.length() > 0) {
                int n = errorText.breakText(l, true, width, null);
                if (n <= 0) {
                    break;
                }
                lines.add(l.substring(0, n));
                l = l.substring(n);
            }
        }
        float lh = errorText.getTextSize() * 1.25f;
        float h = lh * lines.size();
        float x = (getWidth() - width) / 2f;
        float y = Math.max(lh, (getHeight() - h) / 2f);
        c.drawRect(x - lh / 2, y - lh / 2, x + width + lh / 2, y + h + lh / 2, errorBack);
        for (int i = 0; i < lines.size(); i++) {
            c.drawText(lines.get(i), x, y + lh * (i + 0.8f), errorText);
        }
    }

    private int alpha(int a) {
        return a * touchOpacity / 100;
    }

    private int fillColor(boolean pressed) {
        return pressed ? Color.argb(alpha(230), 255, 156, 66) : Color.argb(alpha(110), 20, 20, 30);
    }

    private void drawGear(Canvas c) {
        boolean open = mode == MODE_SETTINGS;
        int a = open ? 220 : Math.max(alpha(200), 70);

        fill.setColor(Color.argb(a * 110 / 200, 20, 20, 30));
        c.drawCircle(gearX, gearY, gearR, fill);
        line.setColor(Color.argb(a, 255, 255, 255));
        c.drawCircle(gearX, gearY, gearR, line);
        if (open) {
            /* a cross: close */
            float k = gearR * 0.4f;
            c.drawLine(gearX - k, gearY - k, gearX + k, gearY + k, line);
            c.drawLine(gearX - k, gearY + k, gearX + k, gearY - k, line);
        } else {
            /* a cog: a ring with teeth */
            float r = gearR * 0.42f;
            c.drawCircle(gearX, gearY, r * 0.5f, line);
            for (int i = 0; i < 8; i++) {
                double t = Math.PI * i / 4;
                c.drawLine(gearX + (float) Math.cos(t) * r, gearY + (float) Math.sin(t) * r,
                        gearX + (float) Math.cos(t) * r * 1.5f, gearY + (float) Math.sin(t) * r * 1.5f, line);
            }
            c.drawCircle(gearX, gearY, r, line);
        }
    }

    private void drawControls(Canvas c) {
        float u = padR / 17;

        line.setColor(Color.argb(alpha(230), 255, 255, 255));
        text.setColor(Color.argb(alpha(255), 255, 255, 255));
        drawPad(c, u);
        line.setColor(Color.argb(alpha(230), 255, 255, 255));
        iconPaint.setAlpha(Math.min(255, alpha(255) + 60));
        button(c, aX, aY, aR, 0, "A", (keys & MainActivity.KEY_A) != 0);
        button(c, bX, bY, bR, 1, "B", (keys & MainActivity.KEY_B) != 0);
        pill(c, lRect, 2, "L", (keys & MainActivity.KEY_L) != 0);
        if (turboLocked) {
            /* Auto: an orange rim and the word under the button */
            float rad = lRect.height() / 2;
            Paint rim = new Paint(line);
            rim.setColor(Color.argb(255, 255, 156, 66));
            rim.setStrokeWidth(line.getStrokeWidth() * 2);
            c.drawRoundRect(lRect, rad, rad, rim);
            Paint t = new Paint(text);
            t.setColor(Color.argb(255, 255, 196, 120));
            t.setTextSize(text.getTextSize() * 0.8f);
            c.drawText("AUTO", lRect.centerX(), lRect.bottom + t.getTextSize() * 1.1f, t);
        }
        pill(c, rRect, 3, "R", (keys & MainActivity.KEY_R) != 0);
        pill(c, selRect, -1, "SELECT", (keys & MainActivity.KEY_SELECT) != 0);
        pill(c, startRect, -2, "START", (keys & MainActivity.KEY_START) != 0);
    }

    private static final int[] DIRS = { MainActivity.KEY_UP, MainActivity.KEY_DOWN, MainActivity.KEY_LEFT,
            MainActivity.KEY_RIGHT };

    /** Where a separate direction button is (0 up, 1 down, 2 left, 3 right)
     * around the d-pad's centre, and its radius. */
    private float dirX(float cx, int i) {
        return cx + (i == 2 ? -1 : i == 3 ? 1 : 0) * padR * 0.62f;
    }

    private float dirY(float cy, int i) {
        return cy + (i == 0 ? -1 : i == 1 ? 1 : 0) * padR * 0.62f;
    }

    /** The d-pad in its style: where the thumb anchored it while in use,
     * else at its place (fainter there when it floats: it shows where it
     * rests, but follows the thumb). */
    private void drawPad(Canvas c, float u) {
        boolean held = padOwner >= 0;
        float cx = held && dpadFloating ? anchorX : padX;
        float cy = held && dpadFloating ? anchorY : padY;
        int was = touchOpacity;

        if (dpadFloating && !held && !editing) {
            touchOpacity = touchOpacity * 55 / 100;
        }
        line.setColor(Color.argb(alpha(230), 255, 255, 255));
        fill.setColor(Color.argb(alpha(255), 255, 255, 255));
        if (dpadStyle == DPAD_STICK) {
            /* a base with a notch each way, and the knob where the thumb is */
            Paint base = new Paint(fill);
            base.setColor(fillColor(false));
            c.drawCircle(cx, cy, padR, base);
            c.drawCircle(cx, cy, padR, line);
            for (int i = 0; i < 4; i++) {
                float k = padR * 0.16f;
                RectF r = new RectF(dirX(cx, i) * 1.25f - cx * 0.25f - k, dirY(cy, i) * 1.25f - cy * 0.25f - k,
                        dirX(cx, i) * 1.25f - cx * 0.25f + k, dirY(cy, i) * 1.25f - cy * 0.25f + k);
                Paint tri = new Paint(fill);
                tri.setColor(Color.argb((keys & DIRS[i]) != 0 ? alpha(255) : alpha(150), 255, 255, 255));
                arrow(c, r, i, tri);
            }
            float kx = cx, ky = cy;
            if (held) {
                float dx = thumbX - anchorX, dy = thumbY - anchorY;
                float d = (float) Math.hypot(dx, dy), max = padR * 0.55f;
                if (d > max) {
                    dx *= max / d;
                    dy *= max / d;
                }
                kx += dx;
                ky += dy;
            }
            Paint knob = new Paint(fill);
            knob.setColor(fillColor((keys & (DIRS[0] | DIRS[1] | DIRS[2] | DIRS[3])) != 0));
            c.drawCircle(kx, ky, padR * 0.42f, knob);
            c.drawCircle(kx, ky, padR * 0.42f, line);
        } else if (dpadStyle == DPAD_BUTTONS) {
            /* a round button each way */
            float r = padR * 0.36f;
            for (int i = 0; i < 4; i++) {
                float x = dirX(cx, i), y = dirY(cy, i);
                Paint f = new Paint(fill);
                f.setColor(fillColor((keys & DIRS[i]) != 0));
                c.drawCircle(x, y, r, f);
                c.drawCircle(x, y, r, line);
                arrow(c, new RectF(x - r * 0.75f, y - r * 0.75f, x + r * 0.75f, y + r * 0.75f), i, fill);
            }
        } else {
            /* the cross, each arm lit while held */
            float arm = padR * 0.36f;
            RectF[] arms = {
                new RectF(cx - arm, cy - padR, cx + arm, cy - arm),
                new RectF(cx - arm, cy + arm, cx + arm, cy + padR),
                new RectF(cx - padR, cy - arm, cx - arm, cy + arm),
                new RectF(cx + arm, cy - arm, cx + padR, cy + arm),
            };
            Paint mid = new Paint(fill);
            mid.setColor(fillColor(false));
            c.drawRect(cx - arm, cy - arm, cx + arm, cy + arm, mid);
            for (int i = 0; i < 4; i++) {
                Paint armFill = new Paint(fill);
                armFill.setColor(fillColor((keys & DIRS[i]) != 0));
                c.drawRoundRect(arms[i], u, u, armFill);
                c.drawRoundRect(arms[i], u, u, line);
                arrow(c, arms[i], i, fill);
            }
        }
        touchOpacity = was;
    }

    /** A white triangle in a D-pad arm, pointing its way (0 up, 1 down,
     * 2 left, 3 right). */
    private void arrow(Canvas c, RectF arm, int dir, Paint paint) {
        float cx = arm.centerX(), cy = arm.centerY();
        float k = Math.min(arm.width(), arm.height()) * 0.28f;
        Path p = new Path();
        switch (dir) {
        case 0: p.moveTo(cx, cy - k); p.lineTo(cx + k, cy + k * 0.6f); p.lineTo(cx - k, cy + k * 0.6f); break;
        case 1: p.moveTo(cx, cy + k); p.lineTo(cx + k, cy - k * 0.6f); p.lineTo(cx - k, cy - k * 0.6f); break;
        case 2: p.moveTo(cx - k, cy); p.lineTo(cx + k * 0.6f, cy - k); p.lineTo(cx + k * 0.6f, cy + k); break;
        default: p.moveTo(cx + k, cy); p.lineTo(cx - k * 0.6f, cy - k); p.lineTo(cx - k * 0.6f, cy + k); break;
        }
        p.close();
        c.drawPath(p, paint);
    }

    private void drawIcon(Canvas c, Bitmap icon, float cx, float cy, float size, int alpha) {
        if (icon == null || alpha <= 0) {
            return;
        }
        iconRect.set(cx - size / 2, cy - size / 2, cx + size / 2, cy + size / 2);
        int was = iconPaint.getAlpha();
        iconPaint.setAlpha(alpha * was / 255);
        c.drawBitmap(icon, null, iconRect, iconPaint);
        iconPaint.setAlpha(was);
    }

    /** The icon a slot (0-3: A, B, L, R) shows now: the game's in a level,
     * the originals elsewhere and in the editor. */
    private int iconOf(int[] set, int slot) {
        return mode == MODE_PLAY && !editing ? set[slot] : DEFAULT_ICONS[slot];
    }

    /** A slot's icon, or Start's (-2) or Select's (-1): the one it changed
     * from fading out as the new one fades in; a faint one when it has
     * nothing to do now. Without its picture, the button's name. */
    private void slotIcon(Canvas c, int slot, String label, float cx, float cy, float size) {
        if (slot < 0) {
            Bitmap b = slot == -2 ? iconPause : iconCounters;
            if (b != null) {
                drawIcon(c, b, cx, cy, size, 255);
            } else {
                c.drawText(label, cx, cy + text.getTextSize() * 0.36f, text);
            }
            return;
        }
        int now = iconOf(iconNow, slot);
        int was = iconOf(iconWas, slot);
        float t = 1;
        if (mode == MODE_PLAY && !editing) {
            t = Math.min(1f, (android.os.SystemClock.uptimeMillis() - iconAt[slot]) / (float) FADE_MS);
            if (t < 1) {
                postInvalidateOnAnimation();
            }
        }
        if (icons[now & 0x7F] == null) {
            c.drawText(label, cx, cy + text.getTextSize() * 0.36f, text);
            return;
        }
        if (t < 1 && was != now) {
            drawIcon(c, icons[was & 0x7F], cx, cy, size, (int) ((1 - t) * ((was & ICON_DIM) != 0 ? 80 : 255)));
        }
        drawIcon(c, icons[now & 0x7F], cx, cy, size, (int) (t * ((now & ICON_DIM) != 0 ? 80 : 255)));
    }

    private void button(Canvas c, float x, float y, float r, int slot, String label, boolean pressed) {
        fill.setColor(fillColor(pressed));
        c.drawCircle(x, y, r, fill);
        c.drawCircle(x, y, r, line);
        slotIcon(c, slot, label, x, y, r * 1.45f);
    }

    private void pill(Canvas c, RectF r, int slot, String label, boolean pressed) {
        float rad = r.height() / 2;

        fill.setColor(fillColor(pressed));
        c.drawRoundRect(r, rad, rad, fill);
        c.drawRoundRect(r, rad, rad, line);
        slotIcon(c, slot, label, r.centerX(), r.centerY(), r.height() * 0.95f);
    }

    /* --- The layout: each control's place and size, and its editor --- */

    private static final String PREFS = "touch_layout";
    private static final float MIN_SCALE = 0.6f, MAX_SCALE = 2.0f;
    /* the player's: a centre as a share of the screen (-1: the default's)
     * and a size as a multiple of the default */
    private final float[] fx = new float[CONTROLS], fy = new float[CONTROLS], fs = new float[CONTROLS];
    private boolean editing;
    private int selected = -1;
    private int dragId = -1;
    private float dragDX, dragDY;
    private float pinchStart, pinchScale;
    private final RectF[] tools = { new RectF(), new RectF(), new RectF(), new RectF() };
    private static final String[] TOOL_NAMES = { "RESET", "-", "+", "DONE" };

    private void loadLayout() {
        android.content.SharedPreferences p = activity.getSharedPreferences(PREFS, 0);
        for (int i = 0; i < CONTROLS; i++) {
            fx[i] = p.getFloat("x" + i, -1);
            fy[i] = p.getFloat("y" + i, -1);
            fs[i] = Math.max(MIN_SCALE, Math.min(MAX_SCALE, p.getFloat("s" + i, 1)));
        }
    }

    private void saveLayout() {
        android.content.SharedPreferences.Editor e = activity.getSharedPreferences(PREFS, 0).edit();
        for (int i = 0; i < CONTROLS; i++) {
            e.putFloat("x" + i, fx[i]);
            e.putFloat("y" + i, fy[i]);
            e.putFloat("s" + i, fs[i]);
        }
        e.apply();
    }

    private RectF rectOf(int i) {
        return i == C_L ? lRect : i == C_R ? rRect : i == C_SELECT ? selRect : startRect;
    }

    private float centerX(int i) {
        return i == C_PAD ? padX : i == C_A ? aX : i == C_B ? bX : rectOf(i).centerX();
    }

    private float centerY(int i) {
        return i == C_PAD ? padY : i == C_A ? aY : i == C_B ? bY : rectOf(i).centerY();
    }

    /** The defaults (just set by layout) moved and sized as the player
     * left them, kept on the screen. */
    private void applyLayout(int w, int h) {
        for (int i = 0; i < CONTROLS; i++) {
            float k = fs[i];
            float cx = fx[i] >= 0 ? fx[i] * w : centerX(i);
            float cy = fy[i] >= 0 ? fy[i] * h : centerY(i);
            float hw, hh;
            if (i == C_PAD) {
                padR *= k;
                hw = hh = padR;
            } else if (i == C_A) {
                aR *= k;
                hw = hh = aR;
            } else if (i == C_B) {
                bR *= k;
                hw = hh = bR;
            } else {
                RectF r = rectOf(i);
                hw = r.width() / 2 * k;
                hh = r.height() / 2 * k;
            }
            cx = Math.max(hw, Math.min(w - hw, cx));
            cy = Math.max(hh, Math.min(h - hh, cy));
            if (i == C_PAD) {
                padX = cx;
                padY = cy;
            } else if (i == C_A) {
                aX = cx;
                aY = cy;
            } else if (i == C_B) {
                bX = cx;
                bY = cy;
            } else {
                rectOf(i).set(cx - hw, cy - hh, cx + hw, cy + hh);
            }
        }
    }

    boolean isEditing() {
        return editing;
    }

    void startEditing() {
        editing = true;
        padOwner = -1;
        selected = -1;
        dragId = -1;
        touchKeys = 0;
        sendKeys();
        invalidate();
    }

    void finishEditing() {
        if (!editing) {
            return;
        }
        editing = false;
        selected = -1;
        dragId = -1;
        saveLayout();
        invalidate();
    }

    /** The control under (x, y) in the editor, or -1. */
    private int controlAt(float x, float y) {
        if (Math.hypot(x - aX, y - aY) < aR * 1.2f) return C_A;
        if (Math.hypot(x - bX, y - bY) < bR * 1.2f) return C_B;
        for (int i = C_L; i <= C_START; i++) {
            if (grown(rectOf(i)).contains(x, y)) return i;
        }
        if (Math.hypot(x - padX, y - padY) < padR * 1.15f) return C_PAD;
        return -1;
    }

    private void scaleSelected(float k) {
        int w = getWidth(), h = getHeight();
        if (selected < 0) {
            return;
        }
        if (fx[selected] < 0) {
            fx[selected] = centerX(selected) / w;
            fy[selected] = centerY(selected) / h;
        }
        fs[selected] = Math.max(MIN_SCALE, Math.min(MAX_SCALE, k));
        layout(w, h);
        invalidate();
    }

    private void drawEditor(Canvas c) {
        int w = getWidth();
        float u = Math.min(w, getHeight()) / 100f;

        c.drawRect(0, 0, w, getHeight(), shade);
        int was = touchOpacity;
        touchOpacity = Math.max(touchOpacity, 70);
        drawControls(c);
        touchOpacity = was;
        if (selected >= 0) {
            Paint rim = new Paint(line);
            rim.setColor(Color.argb(255, 255, 156, 66));
            rim.setStrokeWidth(line.getStrokeWidth() * 2.5f);
            if (selected == C_PAD) {
                c.drawCircle(padX, padY, padR * 1.12f, rim);
            } else if (selected == C_A || selected == C_B) {
                c.drawCircle(centerX(selected), centerY(selected), (selected == C_A ? aR : bR) * 1.15f, rim);
            } else {
                RectF r = new RectF(rectOf(selected));
                r.inset(-u, -u);
                c.drawRoundRect(r, r.height() / 2, r.height() / 2, rim);
            }
        }
        /* the tools along the top middle */
        float tw = 15 * u, th = 7 * u, gap = 2 * u;
        float x = w / 2f - (4 * tw + 3 * gap) / 2;
        Paint t = new Paint(text);
        t.setTextSize(3.4f * u);
        t.setColor(Color.WHITE);
        for (int i = 0; i < 4; i++) {
            RectF r = tools[i];
            r.set(x, 2.5f * u, x + tw, 2.5f * u + th);
            x += tw + gap;
            boolean off = (i == 1 || i == 2) && selected < 0;
            fill.setColor(i == 3 ? Color.argb(235, 255, 156, 66) : Color.argb(off ? 90 : 200, 20, 20, 30));
            c.drawRoundRect(r, th / 2, th / 2, fill);
            Paint edge = new Paint(line);
            edge.setColor(Color.argb(off ? 90 : 230, 255, 255, 255));
            c.drawRoundRect(r, th / 2, th / 2, edge);
            t.setAlpha(off ? 100 : 255);
            c.drawText(TOOL_NAMES[i], r.centerX(), r.centerY() + t.getTextSize() * 0.36f, t);
        }
        t.setAlpha(230);
        t.setTextSize(3f * u);
        c.drawText(selected < 0 ? "drag a button to move it; pinch or - and + to resize it"
                : "drag to move, pinch or - and + to resize", w / 2f, 2.5f * u + th + 5 * u, t);
    }

    private boolean onEditorTouch(MotionEvent e) {
        int action = e.getActionMasked();
        int index = e.getActionIndex();
        int id = e.getPointerId(index);
        float x = e.getX(index), y = e.getY(index);
        int w = getWidth(), h = getHeight();

        if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) {
            if (action == MotionEvent.ACTION_DOWN) {
                for (int i = 0; i < 4; i++) {
                    if (tools[i].contains(x, y)) {
                        buzz();
                        if (i == 0) {
                            for (int j = 0; j < CONTROLS; j++) {
                                fx[j] = fy[j] = -1;
                                fs[j] = 1;
                            }
                            layout(w, h);
                        } else if (i == 1) {
                            scaleSelected(selected >= 0 ? fs[selected] - 0.1f : 1);
                        } else if (i == 2) {
                            scaleSelected(selected >= 0 ? fs[selected] + 0.1f : 1);
                        } else {
                            finishEditing();
                        }
                        invalidate();
                        return true;
                    }
                }
            }
            if (dragId < 0) {
                int hit = controlAt(x, y);
                if (hit >= 0) {
                    if (hit != selected) {
                        buzz();
                    }
                    selected = hit;
                    dragId = id;
                    dragDX = centerX(hit) - x;
                    dragDY = centerY(hit) - y;
                }
            } else if (selected >= 0 && e.getPointerCount() >= 2) {
                /* a second finger: a pinch resizes the one held */
                int i0 = e.findPointerIndex(dragId);
                if (i0 >= 0) {
                    pinchStart = (float) Math.hypot(x - e.getX(i0), y - e.getY(i0));
                    pinchScale = fs[selected];
                }
            }
            invalidate();
        } else if (action == MotionEvent.ACTION_MOVE && dragId >= 0 && selected >= 0) {
            int i0 = e.findPointerIndex(dragId);
            if (i0 < 0) {
                return true;
            }
            if (e.getPointerCount() >= 2 && pinchStart > 0) {
                int i1 = i0 == 0 ? 1 : 0;
                float d = (float) Math.hypot(e.getX(i1) - e.getX(i0), e.getY(i1) - e.getY(i0));
                fx[selected] = centerX(selected) / w;
                fy[selected] = centerY(selected) / h;
                scaleSelected(pinchScale * d / pinchStart);
            } else {
                fx[selected] = Math.max(0, Math.min(1, (e.getX(i0) + dragDX) / w));
                fy[selected] = Math.max(0, Math.min(1, (e.getY(i0) + dragDY) / h));
                layout(w, h);
                invalidate();
            }
        } else if (action == MotionEvent.ACTION_POINTER_UP) {
            pinchStart = 0;
            if (id == dragId) {
                dragId = -1;
            }
        } else if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) {
            dragId = -1;
            pinchStart = 0;
        }
        return true;
    }

    /* --- Touch --- */

    /** Eight ways from an offset: each direction a 45-degree sector either
     * side of its axis takes the diagonals with it; nothing inside `dead`. */
    private static int directions(float dx, float dy, float dead) {
        int k = 0;

        if (Math.hypot(dx, dy) <= dead) {
            return 0;
        }
        double a = Math.toDegrees(Math.atan2(dy, dx));
        if (a > -67.5 && a < 67.5) k |= MainActivity.KEY_RIGHT;
        if (a > 112.5 || a < -112.5) k |= MainActivity.KEY_LEFT;
        if (a > 22.5 && a < 157.5) k |= MainActivity.KEY_DOWN;
        if (a < -22.5 && a > -157.5) k |= MainActivity.KEY_UP;
        return k;
    }

    /** The d-pad works by a finger's offset from an anchor (not by where
     * the finger is): the joystick, and any floating style. */
    private boolean padByOffset() {
        return dpadFloating || dpadStyle == DPAD_STICK;
    }

    private int keysAt(float x, float y) {
        int k = 0;
        float dx = x - padX, dy = y - padY;
        float d = (float) Math.hypot(dx, dy);

        if (!padByOffset()) {
            if (dpadStyle == DPAD_BUTTONS) {
                /* each its own button: a finger between two presses both */
                float r = padR * 0.36f * 1.4f;
                for (int i = 0; i < 4; i++) {
                    if (Math.hypot(x - dirX(padX, i), y - dirY(padY, i)) < r) {
                        k |= DIRS[i];
                    }
                }
                if (k != 0) {
                    return k;
                }
            } else if (d < padR * 1.7f && d > padR * 0.18f) {
                return directions(dx, dy, 0);
            }
        }
        float da = (float) Math.hypot(x - aX, y - aY);
        float db = (float) Math.hypot(x - bX, y - bY);
        if (da < aR * 1.45f) k |= MainActivity.KEY_A;
        if (db < bR * 1.45f) k |= MainActivity.KEY_B;
        if (k != 0) return k;
        if (grown(lRect).contains(x, y)) return MainActivity.KEY_L;
        if (grown(rRect).contains(x, y)) return MainActivity.KEY_R;
        if (grown(selRect).contains(x, y)) return MainActivity.KEY_SELECT;
        if (grown(startRect).contains(x, y)) return MainActivity.KEY_START;
        return 0;
    }

    private RectF grown(RectF r) {
        float g = r.height() * 0.6f;
        return new RectF(r.left - g, r.top - g, r.right + g, r.bottom + g);
    }

    /** The close button, while the settings menu is open (the settings are
     * opened from the pause menu, the warp room's cog, or a controller's
     * Guide button). */
    private boolean onGear(float x, float y) {
        return mode == MODE_SETTINGS && Math.hypot(x - gearX, y - gearY) < gearR * 1.6f;
    }

    /** The picture's pixel under (x, y), or null outside it. */
    private int[] pictureAt(float x, float y) {
        if (!gameRect.contains((int) x, (int) y)) {
            return null;
        }
        return new int[] { (int) ((x - gameRect.left) * W / gameRect.width()),
                (int) ((y - gameRect.top) * H / gameRect.height()) };
    }

    private void buzz() {
        if (haptics == 0) {
            return;
        }
        performHapticFeedback(haptics == 1 ? HapticFeedbackConstants.KEYBOARD_TAP : HapticFeedbackConstants.VIRTUAL_KEY,
                HapticFeedbackConstants.FLAG_IGNORE_GLOBAL_SETTING);
    }

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        int action = e.getActionMasked();
        int index = e.getActionIndex();
        int id = e.getPointerId(index);
        boolean down = action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN;
        boolean up = action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP;

        if (error != null) {
            return true;
        }
        if (editing) {
            return onEditorTouch(e);
        }
        if (controllerHidden) {
            controllerHidden = false;
            sendKeys(); /* the lock holds again */
        }
        activity.touchUsed();
        /* the settings button */
        if (down && onGear(e.getX(index), e.getY(index))) {
            gearId = id;
            return true;
        }
        if (up && id == gearId) {
            gearId = -1;
            if (onGear(e.getX(index), e.getY(index))) {
                buzz();
                if (mode == MODE_SETTINGS) {
                    activity.sendBack();
                } else {
                    activity.openSettings();
                }
            }
            return true;
        }
        if (action == MotionEvent.ACTION_CANCEL) {
            gearId = -1;
            swipeId = -1;
        }
        boolean shown = controlsShown();
        /* the d-pad by offset: a finger takes it (the joystick's on it; a
         * floating one anywhere on the left that isn't another button),
         * until it lifts */
        boolean tookPad = false;
        if (shown && down && padByOffset() && padOwner < 0) {
            float x = e.getX(index), y = e.getY(index);
            boolean other = keysAt(x, y) != 0;
            if (dpadFloating ? (x < getWidth() * 0.5f && !other)
                    : Math.hypot(x - padX, y - padY) < padR * 1.3f) {
                padOwner = id;
                anchorX = dpadFloating ? x : padX;
                anchorY = dpadFloating ? y : padY;
                thumbX = x;
                thumbY = y;
                tookPad = true;
            }
        }
        if ((up && id == padOwner) || action == MotionEvent.ACTION_CANCEL || !shown) {
            padOwner = -1;
        }
        /* the buttons */
        int k = 0;
        if (shown && action != MotionEvent.ACTION_CANCEL) {
            for (int i = 0; i < e.getPointerCount(); i++) {
                int pid = e.getPointerId(i);
                if ((up && i == index) || pid == gearId) {
                    continue;
                }
                if (pid == padOwner) {
                    thumbX = e.getX(i);
                    thumbY = e.getY(i);
                    k |= directions(thumbX - anchorX, thumbY - anchorY, padR * 0.22f);
                } else {
                    k |= keysAt(e.getX(i), e.getY(i));
                }
            }
            if (padOwner >= 0) {
                invalidate(); /* the knob, or where the d-pad floats */
            }
        }
        if ((k & ~touchKeys) != 0) {
            buzz();
        }
        int lDown = k & ~touchKeys & MainActivity.KEY_L;
        int lUp = touchKeys & ~k & MainActivity.KEY_L;
        if (lDown != 0) {
            if (turboLocked) {
                turboLocked = false; /* a tap lets it go */
            } else {
                postDelayed(turboHold, TURBO_HOLD_MS);
            }
        }
        if (lUp != 0) {
            removeCallbacks(turboHold);
        }
        if (k != touchKeys) {
            touchKeys = k;
            sendKeys();
        }
        /* a finger on the picture that isn't on a button: a tap there, or a
         * swipe that scrolls */
        if (down && swipeId < 0 && !tookPad && id != padOwner
                && (!shown || keysAt(e.getX(index), e.getY(index)) == 0)) {
            swipeId = id;
            swipeX = e.getX(index);
            swipeY = swipeLastY = e.getY(index);
            swiped = false;
        } else if (action == MotionEvent.ACTION_MOVE && swipeId >= 0) {
            int i = e.findPointerIndex(swipeId);
            if (i >= 0 && shown && keysAt(e.getX(i), e.getY(i)) != 0) {
                swiped = true; /* slid onto a button: a press, not a tap */
            }
            if (i >= 0 && !shown) {
                float step = gameRect.height() / 16f;
                float y = e.getY(i);
                if (Math.abs(y - swipeY) > step * 0.6f || Math.abs(e.getX(i) - swipeX) > step * 0.6f) {
                    swiped = true;
                }
                while (y - swipeLastY > step) {
                    activity.sendScroll(1);
                    swipeLastY += step;
                }
                while (swipeLastY - y > step) {
                    activity.sendScroll(-1);
                    swipeLastY -= step;
                }
            }
        } else if (up && id == swipeId) {
            swipeId = -1;
            if (!swiped) {
                int[] p = pictureAt(swipeX, swipeY);
                if (p != null) {
                    buzz();
                    activity.sendTap(p[0], p[1]);
                }
            }
        }
        return true;
    }
}
