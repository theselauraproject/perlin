package jh;
import java.lang.invoke.*;
public class Boot {
    public static CallSite typeSwitch(MethodHandles.Lookup l, String name, MethodType type, Object... labels) throws Exception {
        MethodHandle mh = MethodHandles.lookup().findStatic(Boot.class, "doSwitch", MethodType.methodType(int.class, Object[].class, Object.class, int.class));
        mh = MethodHandles.insertArguments(mh, 0, (Object) labels);
        return new ConstantCallSite(mh.asType(type));
    }
    public static CallSite enumSwitch(MethodHandles.Lookup l, String name, MethodType type, Object... labels) throws Exception { return typeSwitch(l, name, type, labels); }
    static int doSwitch(Object[] labels, Object target, int restart) {
        if (target == null) return -1;
        for (int i = restart; i < labels.length; i++) {
            Object lb = labels[i];
            if (lb instanceof Class<?> c) { if (c.isInstance(target)) return i; }
            else if (lb instanceof Integer n) { if ((target instanceof Number t && t.intValue() == n) || (target instanceof Character ch && (int) ch.charValue() == n.intValue())) return i; }
            else if (lb instanceof String s) { if (s.equals(target)) return i; }
            else if (lb instanceof Enum<?> e) { if (e == target) return i; }
            else if (lb != null && lb.getClass().getSimpleName().equals("EnumDesc") && target instanceof Enum<?> en && en.name().equals(String.valueOf(lb).replaceAll(".*\\.", "").replace("EnumDesc[", "").replace("]", ""))) return i;
        }
        return labels.length;
    }
}
