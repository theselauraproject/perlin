import org.objectweb.asm.*;
import java.nio.file.*;
import java.util.*;

public class Stubs {
    public static void main(String[] a) throws Exception {
        Path out = Paths.get(a[1]);
        for (String line : Files.readAllLines(Paths.get(a[0]))) {
            String[] p = line.split(" "); if (p.length < 2) continue;
            String name = p[1];
            if (name.startsWith("com/mojang/blaze3d") || name.startsWith("com/mojang/renderpearl") || name.startsWith("com/mojang/realmsclient") || name.startsWith("org/lwjgl")) continue;
            ClassWriter cw = new ClassWriter(0);
            boolean itf = p[0].equals("I");
            cw.visit(Opcodes.V17, Opcodes.ACC_PUBLIC | (itf ? Opcodes.ACC_INTERFACE | Opcodes.ACC_ABSTRACT : Opcodes.ACC_SUPER), name, null, "java/lang/Object", null);
            if (!itf) {
                MethodVisitor m = cw.visitMethod(Opcodes.ACC_PUBLIC, "<init>", "()V", null, null);
                m.visitCode(); m.visitVarInsn(Opcodes.ALOAD, 0); m.visitMethodInsn(Opcodes.INVOKESPECIAL, "java/lang/Object", "<init>", "()V", false);
                m.visitInsn(Opcodes.RETURN); m.visitMaxs(1, 1); m.visitEnd();
            }
            cw.visitEnd();
            Path o = out.resolve(name + ".class"); Files.createDirectories(o.getParent()); Files.write(o, cw.toByteArray());
        }
    }
}
