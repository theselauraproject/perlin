import org.jetbrains.org.objectweb.asm.*;
import org.jetbrains.org.objectweb.asm.commons.*;
import java.nio.file.*;
import java.util.zip.*;
import java.io.*;

public class Relocate {
    public static void main(String[] a) throws Exception {
        final String from = "org/jetbrains/kotlin/it/unimi/", to = "it/unimi/";
        Remapper rm = new Remapper() { public String map(String n) { return n.startsWith(from) ? to + n.substring(from.length()) : n; } };
        Path out = Paths.get(a[1]);
        try (ZipFile z = new ZipFile(a[0])) {
            var en = z.entries();
            int n = 0;
            while (en.hasMoreElements()) {
                ZipEntry e = en.nextElement();
                if (!e.getName().startsWith(from) || !e.getName().endsWith(".class")) continue;
                byte[] b = z.getInputStream(e).readAllBytes();
                ClassReader cr = new ClassReader(b);
                ClassWriter cw = new ClassWriter(0);
                String nm = to + e.getName().substring(from.length());
                java.util.regex.Matcher mm = java.util.regex.Pattern.compile("it/unimi/dsi/fastutil/(doubles|ints|longs|floats)/(Double|Int|Long|Float)List\\.class").matcher(nm);
                ClassVisitor head = new ClassRemapper(cw, rm);
                if (mm.matches()) {
                    final String pkg = mm.group(1), P = mm.group(2);
                    final String prim = P.equals("Double") ? "D" : P.equals("Int") ? "I" : P.equals("Long") ? "J" : "F";
                    final String iface = "it/unimi/dsi/fastutil/" + pkg + "/" + P + "List";
                    final String impl = "it/unimi/dsi/fastutil/" + pkg + "/" + P + "ArrayList";
                    head = new ClassVisitor(Opcodes.ASM9, head) {
                        public void visitEnd() {
                            MethodVisitor m = super.visitMethod(Opcodes.ACC_PUBLIC | Opcodes.ACC_STATIC | Opcodes.ACC_VARARGS, "of", "([" + prim + ")L" + iface + ";", null, null);
                            m.visitCode();
                            m.visitVarInsn(Opcodes.ALOAD, 0);
                            m.visitMethodInsn(Opcodes.INVOKESTATIC, "it/unimi/dsi/fastutil/Helpers", P.toLowerCase() + "Of", "([" + prim + ")L" + iface + ";", false);
                            m.visitInsn(Opcodes.ARETURN); m.visitMaxs(1, 1); m.visitEnd();
                            int tcode = prim.equals("D") ? Opcodes.T_DOUBLE : prim.equals("I") ? Opcodes.T_INT : prim.equals("J") ? Opcodes.T_LONG : Opcodes.T_FLOAT;
                            int load = prim.equals("D") ? Opcodes.DLOAD : prim.equals("I") ? Opcodes.ILOAD : prim.equals("J") ? Opcodes.LLOAD : Opcodes.FLOAD;
                            int store = prim.equals("D") ? Opcodes.DASTORE : prim.equals("I") ? Opcodes.IASTORE : prim.equals("J") ? Opcodes.LASTORE : Opcodes.FASTORE;
                            int w = (prim.equals("D") || prim.equals("J")) ? 2 : 1;
                            for (int n = 0; n <= 3; n++) {
                                StringBuilder d = new StringBuilder("("); for (int i = 0; i < n; i++) d.append(prim); d.append(")L" + iface + ";");
                                MethodVisitor k = super.visitMethod(Opcodes.ACC_PUBLIC | Opcodes.ACC_STATIC, "of", d.toString(), null, null);
                                k.visitCode();
                                k.visitIntInsn(Opcodes.BIPUSH, n); k.visitIntInsn(Opcodes.NEWARRAY, tcode);
                                for (int i = 0; i < n; i++) { k.visitInsn(Opcodes.DUP); k.visitIntInsn(Opcodes.BIPUSH, i); k.visitVarInsn(load, i * w); k.visitInsn(store); }
                                k.visitMethodInsn(Opcodes.INVOKESTATIC, "it/unimi/dsi/fastutil/Helpers", P.toLowerCase() + "Of", "([" + prim + ")L" + iface + ";", false);
                                k.visitInsn(Opcodes.ARETURN); k.visitMaxs(8, 8); k.visitEnd();
                            }
                            super.visitEnd();
                        }
                    };
                }
                if (nm.equals("it/unimi/dsi/fastutil/floats/Float2FloatFunction.class")) {
                    final ClassVisitor prev = head;
                    head = new ClassVisitor(Opcodes.ASM9, prev) {
                        public void visitEnd() {
                            MethodVisitor m = super.visitMethod(Opcodes.ACC_PUBLIC | Opcodes.ACC_STATIC, "identity", "()Lit/unimi/dsi/fastutil/floats/Float2FloatFunction;", null, null);
                            m.visitCode();
                            m.visitTypeInsn(Opcodes.NEW, "it/unimi/dsi/fastutil/IdentityF2F"); m.visitInsn(Opcodes.DUP);
                            m.visitMethodInsn(Opcodes.INVOKESPECIAL, "it/unimi/dsi/fastutil/IdentityF2F", "<init>", "()V", false);
                            m.visitInsn(Opcodes.ARETURN); m.visitMaxs(2, 0); m.visitEnd();
                            super.visitEnd();
                        }
                    };
                }
                cr.accept(head, 0);
                Path o = out.resolve(to + e.getName().substring(from.length()));
                Files.createDirectories(o.getParent());
                Files.write(o, cw.toByteArray()); n++;
            }
            System.out.println(n);
        }
    }
}
