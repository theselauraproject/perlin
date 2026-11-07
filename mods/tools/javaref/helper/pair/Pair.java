package com.mojang.datafixers.util;
public class Pair<A, B> {
    private final A first; private final B second;
    public Pair(A a, B b) { first = a; second = b; }
    public static <A, B> Pair<A, B> of(A a, B b) { return new Pair<>(a, b); }
    public A getFirst() { return first; }
    public B getSecond() { return second; }
}
