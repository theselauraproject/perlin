import org.objectweb.asm.*;
import java.nio.file.*;
import java.util.stream.*;

public class Prep {
    static boolean isNeutral(String o) {
        // the biome table dump needs a working Pair (helper/ has one); everything else in the data-fixer library stays a no-op
        if (o.equals("com/mojang/datafixers/util/Pair"))
            return false;
        return o.startsWith("com/mojang/serialization/") || o.startsWith("com/mojang/datafixers/") || o.startsWith("com/mojang/brigadier/") || o.startsWith("com/mojang/authlib/")
            || o.startsWith("io/netty/") || o.startsWith("com/mojang/jtracy/") || o.startsWith("com/mojang/logging/") && false;
    }
    static void pushDefault(MethodVisitor v, Type t) {
        switch (t.getSort()) {
            case Type.VOID: break;
            case Type.LONG: v.visitInsn(Opcodes.LCONST_0); break;
            case Type.FLOAT: v.visitInsn(Opcodes.FCONST_0); break;
            case Type.DOUBLE: v.visitInsn(Opcodes.DCONST_0); break;
            case Type.OBJECT: case Type.ARRAY: v.visitInsn(Opcodes.ACONST_NULL); break;
            default: v.visitInsn(Opcodes.ICONST_0);
        }
    }
    static MethodVisitor neutral(MethodVisitor mv) {
        return new MethodVisitor(Opcodes.ASM9, mv) {
            public void visitMethodInsn(int op, String o, String n, String d, boolean itf) {
                if (o.equals("com/mojang/logging/LogUtils") && n.equals("getLogger")) {
                    super.visitLdcInsn("mc");
                    super.visitMethodInsn(Opcodes.INVOKESTATIC, "org/slf4j/LoggerFactory", "getLogger", "(Ljava/lang/String;)Lorg/slf4j/Logger;", false);
                    return;
                }
                if (!isNeutral(o)) { super.visitMethodInsn(op, o, n, d, itf); return; }
                Type[] args = Type.getArgumentTypes(d);
                for (int i = args.length - 1; i >= 0; i--) super.visitInsn(args[i].getSize() == 2 ? Opcodes.POP2 : Opcodes.POP);
                if (op != Opcodes.INVOKESTATIC) super.visitInsn(Opcodes.POP);
                pushDefault(this, Type.getReturnType(d));
            }
            public void visitInvokeDynamicInsn(String n, String d, Handle bsm, Object... args) {
                if (bsm.getOwner().equals("java/lang/runtime/SwitchBootstraps")) {
                    bsm = new Handle(Opcodes.H_INVOKESTATIC, "jh/Boot", bsm.getName(), "(Ljava/lang/invoke/MethodHandles$Lookup;Ljava/lang/String;Ljava/lang/invoke/MethodType;[Ljava/lang/Object;)Ljava/lang/invoke/CallSite;", false);
                }
                super.visitInvokeDynamicInsn(n, d, bsm, args);
            }
            public void visitFieldInsn(int op, String o, String n, String d) {
                if (!isNeutral(o)) { super.visitFieldInsn(op, o, n, d); return; }
                Type ft = Type.getType(d);
                switch (op) {
                    case Opcodes.GETSTATIC: pushDefault(this, ft); break;
                    case Opcodes.PUTSTATIC: super.visitInsn(ft.getSize() == 2 ? Opcodes.POP2 : Opcodes.POP); break;
                    case Opcodes.GETFIELD: super.visitInsn(Opcodes.POP); pushDefault(this, ft); break;
                    default: super.visitInsn(ft.getSize() == 2 ? Opcodes.POP2 : Opcodes.POP); super.visitInsn(Opcodes.POP);
                }
            }
        };
    }
    public static void main(String[] a) throws Exception {
        Path in = Paths.get(a[0]), out = Paths.get(a[1]);
        try (Stream<Path> s = Files.walk(in)) {
            for (Path p : s.filter(x -> x.toString().endsWith(".class")).collect(Collectors.toList())) {
                byte[] b = Files.readAllBytes(p);
                ClassReader cr = new ClassReader(b);
                ClassWriter cw = new ClassWriter(ClassWriter.COMPUTE_MAXS);
                final String cname = cr.getClassName();
                final boolean dropInit = cname.startsWith("net/minecraft/references/") || cname.equals("net/minecraft/world/level/block/Blocks") || cname.equals("net/minecraft/world/item/Items") || cname.equals("net/minecraft/world/level/block/entity/BlockEntityType") || cname.equals("net/minecraft/world/entity/EntityType");
                cr.accept(new ClassVisitor(Opcodes.ASM9, cw) {
                    public MethodVisitor visitMethod(int acc, String name, String desc, String sig, String[] ex) {
                        if (dropInit && name.equals("<clinit>")) return null;
                        MethodVisitor mv0 = super.visitMethod(acc, name, desc, sig, ex);
                        MethodVisitor mv = neutral(mv0);
                        if (cname.equals("net/minecraft/world/level/levelgen/NoiseRouterData") && name.equals("bootstrap")) {
                            // ore veins need the block registry, which this harness does not have; terrain shape does not depend on them
                            return new MethodVisitor(Opcodes.ASM9, mv) {
                                public void visitMethodInsn(int op, String o, String n, String d, boolean i) {
                                    if (n.equals("registerOreVeins")) { super.visitInsn(Opcodes.POP); return; }
                                    super.visitMethodInsn(op, o, n, d, i);
                                }
                            };
                        }
                        if (!name.equals("<clinit>")) return mv;
                        return new MethodVisitor(Opcodes.ASM9, mv) {
                            Label s, e, h;
                            void open() {
                                s = new Label(); e = new Label(); h = new Label();
                                super.visitTryCatchBlock(s, e, h, null);
                                super.visitLabel(s);
                            }
                            void close() {
                                Label cont = new Label();
                                super.visitLabel(e);
                                super.visitJumpInsn(Opcodes.GOTO, cont);
                                super.visitLabel(h);
                                super.visitInsn(Opcodes.DUP);
                                super.visitMethodInsn(Opcodes.INVOKEVIRTUAL, "java/lang/Throwable", "printStackTrace", "()V", false);
                                super.visitInsn(Opcodes.POP);
                                super.visitLabel(cont);
                            }
                            public void visitCode() { super.visitCode(); open(); }
                            public void visitFieldInsn(int op, String o, String n, String d) {
                                super.visitFieldInsn(op, o, n, d);
                                if (op == Opcodes.PUTSTATIC) { close(); open(); }
                            }
                            public void visitMaxs(int ms, int ml) {
                                super.visitLabel(e);
                                super.visitLabel(h);
                                super.visitInsn(Opcodes.POP);
                                super.visitInsn(Opcodes.RETURN);
                                super.visitMaxs(ms + 2, ml);
                            }
                        };
                    }
                }, ClassReader.SKIP_FRAMES);
                Path o = out.resolve(in.relativize(p)); Files.createDirectories(o.getParent());
                Files.write(o, cw.toByteArray());
            }
        }
    }
}
