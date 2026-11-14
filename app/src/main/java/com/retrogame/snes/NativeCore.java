package com.retrogame.snes;

/** Funções do núcleo em C++ (libretrogamesnes.so). */
final class NativeCore {
    static { System.loadLibrary("retrogamesnes"); }
    private NativeCore() {}

    static native void init();
    static native void setSurface(Object surface);
    static native void surfaceChanged();
    static native void setForeground(boolean fg);

    static native boolean loadRom(byte[] rom);
    static native void reset();
    static native byte[] getSave();
    static native void setSave(byte[] data);
    static native int saveType();
    static native String chipWarning();
    static native byte[] getState();
    static native boolean setState(byte[] data);
    static native int stateHash();
    static native int setCheats(String text);

    static native void setKeys(int k1, int k2);
    static native void setPad(int player, int mask);
    static native void setRun(boolean run, float speed, boolean audio);
    static native void setExternal(boolean external);
    static native void stepFrame();
    static native void setRewind(boolean held);
    static native void setRewindEnabled(boolean on);
    static native int rewindCount();

    static native void setView(float x, float y, float w, float h, int rot, int filter);
    static native void setColorFix(boolean on);
    static native void setVolume(float v);
    static native boolean takeSaveDirty();
    static native int fps();
    static native byte[] getFrame();
    static native boolean vulkan();
}
