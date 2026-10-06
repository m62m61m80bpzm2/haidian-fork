#pragma once
// from net/minecraft/util/Mth.java
//   (decompiled source: /home/z/my-project/mcsrc/net/minecraft/util/Mth.java,
//    Vineflower 1.12.0, official Mojang names)
//
// P0-a translation demo (plan §2 rules): only floor(double) and the four
// clamp(...) overloads, logic 1:1 with the Java bodies quoted at each
// definition. More Mth methods migrate here on demand (plan §3: util/ P0).
//
// Java package net.minecraft.util  ->  namespace mc::util
// Java static methods              ->  free functions in mc::util

namespace mc::util {

// Mth.java L66-68:
//   public static int floor(final double v) { return (int)Math.floor(v); }
// Note: Java's (int) cast saturates for NaN/Inf; C++ static_cast is UB there.
// Upstream callers only pass finite values — same assumption as Java callers.
int floor(double v);

// Mth.java L94-96:
//   public static int clamp(final int value, final int min, final int max) {
//       return Math.min(Math.max(value, min), max);
//   }
int clamp(int value, int min, int max);

// Mth.java L98-100 (long overload):
long clamp(long value, long min, long max);

// Mth.java L102-104:
//   public static float clamp(final float value, final float min, final float max) {
//       return value < min ? min : Math.min(value, max);
//   }
float clamp(float value, float min, float max);

// Mth.java L106-108 (double overload, same body as float):
double clamp(double value, double min, double max);

}  // namespace mc::util
