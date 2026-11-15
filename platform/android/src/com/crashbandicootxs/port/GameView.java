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
 * The game's picture, scaled to fit with its pixels sharp, and the GBA's
 * buttons on the screen: the D-pad on the left, A and B on the right as
 * on the GBA (A up and right of B), L and R in the top corners, Select and
 * Start at the bottom. On a wide screen the buttons sit beside the
 * picture; on a squarer one, over it, see-through. Several fingers at
 * once, sliding from one button to another, the D-pad's diagonals and A
 * and B together (a finger between them) all work. A controller in use
 * hides them until the screen is touched again.
 */
public class GameView extends View {
    private static final int W = 240, H = 160;

    private final MainActivity activity;
    private final Bitmap picture = Bitmap.createBitmap(W, H, Bitmap.Config.RGB_565);
    private volatile byte[] pending;
    private volatile String error;
    private final Paint pixels = new Paint();
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint line = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Rect gameRect = new Rect();
    private final Rect source = new Rect(0, 0, W, H);

    /* The controls' places (set by layout). */
    private float padX, padY, padR;
    private float aX, aY, bX, bY, btnR;
    private final RectF lRect = new RectF(), rRect = new RectF(), selRect = new RectF(), startRect = new RectF();
    private int keys;
    private boolean controlsHidden;

    public GameView(MainActivity activity) {
        super(activity);
        this.activity = activity;
        pixels.setFilterBitmap(false);
        line.setStyle(Paint.Style.STROKE);
        text.setTextAlign(Paint.Align.CENTER);
        text.setTypeface(Typeface.DEFAULT_BOLD);
        setBackgroundColor(Color.BLACK);
        setHapticFeedbackEnabled(true);
    }

    /** A picture from the game (RGB565, little-endian: the bitmap's own order). */
    void setFrame(byte[] data) {
        pending = data;
        postInvalidateOnAnimation();
    }

    void setError(String message) {
        error = message;
        postInvalidate();
    }

    void controllerUsed() {
        if (!controlsHidden) {
            controlsHidden = true;
            postInvalidate();
        }
    }

    @Override
    protected void onSizeChanged(int w, int h, int ow, int oh) {
        float u = Math.min(w, h) / 100f;
        int gw, gh;

        if ((long) w * H > (long) h * W) {
            gh = h;
            gw = h * W / H;
        } else {
            gw = w;
            gh = w * H / W;
        }
        /* Whole multiples of the GBA's pixels when that loses little. */
        int k = gh / H;
        if (k >= 2 && k * H >= gh * 0.9f) {
            gw = W * k;
            gh = H * k;
        }
        gameRect.set((w - gw) / 2, (h - gh) / 2, (w - gw) / 2 + gw, (h - gh) / 2 + gh);

        padR = 17 * u;
        padX = Math.max(padR + 5 * u, gameRect.left / 2f);
        padY = h - padR - 16 * u;
        btnR = 8.5f * u;
        float side = (w - gameRect.right) / 2f;
        aX = w - Math.max(btnR + 4 * u, side - 9 * u) ;
        if (side < 30 * u) {
            aX = w - btnR - 5 * u;
        }
        aY = padY - 7 * u;
        bX = aX - 20 * u;
        bY = padY + 7 * u;
        lRect.set(2 * u, 3 * u, 26 * u, 12 * u);
        rRect.set(w - 26 * u, 3 * u, w - 2 * u, 12 * u);
        selRect.set(w / 2f - 30 * u, h - 8 * u, w / 2f - 6 * u, h - 2.5f * u);
        startRect.set(w / 2f + 6 * u, h - 8 * u, w / 2f + 30 * u, h - 2.5f * u);
        line.setStrokeWidth(0.6f * u);
        text.setTextSize(4.2f * u);
    }

    @Override
    protected void onDraw(Canvas c) {
        byte[] p = pending;

        if (p != null) {
            picture.copyPixelsFromBuffer(ByteBuffer.wrap(p));
        }
        c.drawBitmap(picture, source, gameRect, pixels);
        if (error != null) {
            text.setColor(Color.WHITE);
            c.drawText(error, getWidth() / 2f, getHeight() / 2f, text);
        }
        if (!controlsHidden) {
            drawControls(c);
        }
    }

    private int fillColor(boolean pressed) {
        return pressed ? Color.argb(170, 255, 156, 66) : Color.argb(70, 20, 20, 30);
    }

    private void drawControls(Canvas c) {
        float u = padR / 17;

        line.setColor(Color.argb(150, 255, 255, 255));
        text.setColor(Color.argb(200, 255, 255, 255));
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
        /* A and B. */
        button(c, aX, aY, "A", (keys & MainActivity.KEY_A) != 0);
        button(c, bX, bY, "B", (keys & MainActivity.KEY_B) != 0);
        /* L, R, Select, Start. */
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

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        int action = e.getActionMasked();
        int up = (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP) ? e.getActionIndex() : -1;
        int k = 0;

        if (controlsHidden) {
            controlsHidden = false;
        }
        if (action != MotionEvent.ACTION_CANCEL) {
            for (int i = 0; i < e.getPointerCount(); i++) {
                if (i != up) {
                    k |= keysAt(e.getX(i), e.getY(i));
                }
            }
        }
        if ((k & ~keys) != 0) {
            performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
        }
        if (k != keys) {
            keys = k;
            activity.setTouchKeys(k);
            invalidate();
        }
        return true;
    }
}
