package com.crashbandicootxs.port;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.view.HapticFeedbackConstants;
import android.view.MotionEvent;
import android.view.View;

import java.nio.ByteBuffer;

/**
 * The game's picture and the touch controls (platform/android/README.md).
 *
 * The picture is scaled as the settings say (fit, whole multiples, or
 * stretched; sharp or smooth), from the core's 240x160 pictures or, at 120
 * frames a second, its 480x320 ones.
 *
 * What a touch does follows the core's touch mode (stream.c): in a level,
 * the GBA's buttons on the screen (the D-pad on the left, A and B on the
 * right as on the GBA, L and R in the top corners, Select and Start at the
 * bottom; several fingers at once, sliding between buttons, the D-pad's
 * diagonals); anywhere else (the menus, the warp room, the settings menu)
 * they're hidden and a tap goes to the game where it lands, a swipe up or
 * down scrolls. "In menus: always" keeps the buttons in the menus too.
 * The settings button sits at the top middle. A controller in use hides
 * the buttons until the screen is touched again.
 */
public class GameView extends View {
    static final int W = 240, H = 160;
    static final int MODE_PLAY = 0, MODE_MENU = 1, MODE_SETTINGS = 2, MODE_ANY = 3;

    private final MainActivity activity;
    private final Bitmap small = Bitmap.createBitmap(W, H, Bitmap.Config.RGB_565);
    private final Bitmap large = Bitmap.createBitmap(W * 2, H * 2, Bitmap.Config.RGB_565);
    private Bitmap shown = small;
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

    /* The settings the core sends (stream.c's settings message). */
    private int touchSize = 1;
    private int touchOpacity = 60;
    private boolean touchInMenus;
    private int haptics = 1;
    private int scaling;
    private boolean smooth;
    private volatile int mode = MODE_ANY;

    /* The controls' places (set by layout). */
    private float padX, padY, padR;
    private float aX, aY, bX, bY, btnR;
    private float gearX, gearY, gearR;
    private final RectF lRect = new RectF(), rRect = new RectF(), selRect = new RectF(), startRect = new RectF();
    private int keys;
    private boolean controllerHidden;

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
    }

    /** A picture from the game (RGB565, little-endian: the bitmap's own order). */
    void setFrame(byte[] data, int width) {
        pendingW = width;
        pending = data;
        postInvalidateOnAnimation();
    }

    void setError(String message) {
        error = message;
        postInvalidate();
    }

    void setMode(int m) {
        mode = m;
        if (!controlsShown()) {
            keys = 0;
            activity.setTouchKeys(0);
        }
        postInvalidate();
    }

    void setSettings(int[] v) {
        touchSize = v[0];
        touchOpacity = v[1];
        touchInMenus = v[2] != 0;
        haptics = v[3];
        scaling = v[4];
        smooth = v[5] != 0;
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
            postInvalidate();
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

        float size = new float[] { 0.8f, 1f, 1.25f, 1.5f }[Math.max(0, Math.min(3, touchSize))];
        float s = u * size;
        padR = 17 * s;
        padX = Math.max(padR + 5 * u, gameRect.left / 2f);
        padY = h - padR - 16 * s;
        btnR = 8.5f * s;
        float side = (w - gameRect.right) / 2f;
        aX = w - Math.max(btnR + 4 * u, side - 9 * s);
        if (side < 30 * s) {
            aX = w - btnR - 5 * u;
        }
        aY = padY - 7 * s;
        bX = aX - 20 * s;
        bY = padY + 7 * s;
        lRect.set(2 * u, 3 * u, 2 * u + 24 * s, 3 * u + 9 * s);
        rRect.set(w - 2 * u - 24 * s, 3 * u, w - 2 * u, 3 * u + 9 * s);
        selRect.set(w / 2f - 6 * u - 24 * s, h - 2.5f * u - 5.5f * s, w / 2f - 6 * u, h - 2.5f * u);
        startRect.set(w / 2f + 6 * u, h - 2.5f * u - 5.5f * s, w / 2f + 6 * u + 24 * s, h - 2.5f * u);
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
            shown = pendingW == W ? small : large;
            shown.copyPixelsFromBuffer(ByteBuffer.wrap(p));
            pending = null;
        }
        pixels.setFilterBitmap(smooth);
        c.drawBitmap(shown, null, gameRect, pixels);
        if (error != null) {
            drawError(c, error);
            return;
        }
        if (controlsShown()) {
            drawControls(c);
        }
        drawGear(c);
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
        /* The D-pad: a cross, each arm lit while held. */
        float arm = padR * 0.36f;
        int[] bits = { MainActivity.KEY_UP, MainActivity.KEY_DOWN, MainActivity.KEY_LEFT, MainActivity.KEY_RIGHT };
        RectF[] arms = {
            new RectF(padX - arm, padY - padR, padX + arm, padY - arm),
            new RectF(padX - arm, padY + arm, padX + arm, padY + padR),
            new RectF(padX - padR, padY - arm, padX - arm, padY + arm),
            new RectF(padX + arm, padY - arm, padX + padR, padY + arm),
        };
        fill.setColor(fillColor(false));
        c.drawRect(padX - arm, padY - arm, padX + arm, padY + arm, fill);
        for (int i = 0; i < 4; i++) {
            fill.setColor(fillColor((keys & bits[i]) != 0));
            c.drawRoundRect(arms[i], u, u, fill);
            c.drawRoundRect(arms[i], u, u, line);
        }
        button(c, aX, aY, "A", (keys & MainActivity.KEY_A) != 0);
        button(c, bX, bY, "B", (keys & MainActivity.KEY_B) != 0);
        pill(c, lRect, "L", (keys & MainActivity.KEY_L) != 0);
        pill(c, rRect, "R", (keys & MainActivity.KEY_R) != 0);
        pill(c, selRect, "SELECT", (keys & MainActivity.KEY_SELECT) != 0);
        pill(c, startRect, "START", (keys & MainActivity.KEY_START) != 0);
    }

    private void button(Canvas c, float x, float y, String label, boolean pressed) {
        fill.setColor(fillColor(pressed));
        c.drawCircle(x, y, btnR, fill);
        c.drawCircle(x, y, btnR, line);
        c.drawText(label, x, y + text.getTextSize() * 0.36f, text);
    }

    private void pill(Canvas c, RectF r, String label, boolean pressed) {
        float rad = r.height() / 2;

        fill.setColor(fillColor(pressed));
        c.drawRoundRect(r, rad, rad, fill);
        c.drawRoundRect(r, rad, rad, line);
        c.drawText(label, r.centerX(), r.centerY() + text.getTextSize() * 0.36f, text);
    }

    /* --- Touch --- */

    private int keysAt(float x, float y) {
        int k = 0;
        float dx = x - padX, dy = y - padY;
        float d = (float) Math.hypot(dx, dy);

        if (d < padR * 1.7f && d > padR * 0.18f) {
            /* Eight ways: each direction a 45-degree sector either side of
             * its axis takes the diagonals with it. */
            double a = Math.toDegrees(Math.atan2(dy, dx));
            if (a > -67.5 && a < 67.5) k |= MainActivity.KEY_RIGHT;
            if (a > 112.5 || a < -112.5) k |= MainActivity.KEY_LEFT;
            if (a > 22.5 && a < 157.5) k |= MainActivity.KEY_DOWN;
            if (a < -22.5 && a > -157.5) k |= MainActivity.KEY_UP;
            return k;
        }
        float da = (float) Math.hypot(x - aX, y - aY);
        float db = (float) Math.hypot(x - bX, y - bY);
        if (da < btnR * 1.45f) k |= MainActivity.KEY_A;
        if (db < btnR * 1.45f) k |= MainActivity.KEY_B;
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

    private boolean onGear(float x, float y) {
        return Math.hypot(x - gearX, y - gearY) < gearR * 1.6f;
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
        if (controllerHidden) {
            controllerHidden = false;
            invalidate();
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
        /* the buttons */
        int k = 0;
        if (shown && action != MotionEvent.ACTION_CANCEL) {
            for (int i = 0; i < e.getPointerCount(); i++) {
                if (!(up && i == index) && e.getPointerId(i) != gearId) {
                    k |= keysAt(e.getX(i), e.getY(i));
                }
            }
        }
        if ((k & ~keys) != 0) {
            buzz();
        }
        if (k != keys) {
            keys = k;
            activity.setTouchKeys(k);
            invalidate();
        }
        /* a finger on the picture that isn't on a button: a tap there, or a
         * swipe that scrolls */
        if (down && swipeId < 0 && (!shown || keysAt(e.getX(index), e.getY(index)) == 0)) {
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
