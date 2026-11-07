import org.objectweb.asm.*;
import java.nio.file.*;
import java.util.*;
import java.util.stream.*;

public class Scan {
    static Set<String> refs = new TreeSet<>(), ifaces = new TreeSet<>(), supers = new TreeSet<>();
    public static void main(String[] a) throws Exception {
        try (Stream<Path> s = Files.walk(Paths.get(a[0]))) {
            for (Path p : s.filter(x -> x.toString().endsWith(".class")).collect(Collectors.toList())) {
                ClassReader cr = new ClassReader(Files.readAllBytes(p));
                cr.accept(new ClassVisitor(Opcodes.ASM9) {
                    public void visit(int v, int acc, String name, String sig, String sup, String[] itf) {
                        if (sup != null) { refs.add(sup); supers.add(sup); }
                        if (itf != null) for (String i : itf) { refs.add(i); ifaces.add(i); }
                    }
                    public MethodVisitor visitMethod(int acc, String n, String d, String sg, String[] ex) {
                        for (Type t : Type.getArgumentTypes(d)) add(t); add(Type.getReturnType(d));
                        return new MethodVisitor(Opcodes.ASM9) {
                            public void visitMethodInsn(int op, String o, String n2, String d2, boolean itf) { addName(o); if (op == Opcodes.INVOKEINTERFACE) ifaces.add(o); }
                            public void visitFieldInsn(int op, String o, String n2, String d2) { addName(o); add(Type.getType(d2)); }
                            public void visitTypeInsn(int op, String t) { addName(t); }
                        };
                    }
                    public FieldVisitor visitField(int acc, String n, String d, String sg, Object v) { add(Type.getType(d)); return null; }
                }, ClassReader.SKIP_FRAMES | ClassReader.SKIP_DEBUG);
            }
        }
        for (String r : refs) {
            if (r.startsWith("[")) continue;
            System.out.println((ifaces.contains(r) ? "I " : "C ") + r);
        }
    }
    static void add(Type t) { if (t.getSort() == Type.ARRAY) t = t.getElementType(); if (t.getSort() == Type.OBJECT) refs.add(t.getInternalName()); }
    static void addName(String n) { if (n.startsWith("[")) { add(Type.getType(n)); } else refs.add(n); }
}
