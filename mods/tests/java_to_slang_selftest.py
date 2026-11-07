import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("java_to_slang", ROOT / "tools/java_to_slang.py")
converter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(converter)


class ConversionTests(unittest.TestCase):
    def test_uniform_dependent_globals_run_after_uniform_feed(self):
        source = "static vec2 view = vec2(viewWidth, viewHeight);\nvoid stageMain() {\n}"
        result = converter._defer_global_initializers(source)
        self.assertIn("static vec2 view;", result)
        self.assertGreater(result.index("view = vec2"), result.index("void stageMain"))

    def test_multiline_function_parameters_are_not_globals(self):
        source = "// comment (\nvoid helper(vec3 a,\nvec3 b) {\n}\nvec3 color = vec3(1.0);"
        result = converter._slang_globals(source)
        self.assertIn("\nvec3 b)", result)
        self.assertIn("static vec3 color", result)

    def test_emission_uses_one_buffer_and_real_matrix_product(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            world = root / "shaders/world0"
            world.mkdir(parents=True)
            (world / "probe.vsh").write_text(
                "uniform mat4 first;\nuniform mat4 second;\n"
                "varying vec2 uv;\nvoid main() {\n"
                "gl_Position = first * second * gl_Vertex;\nuv = first[1].xy;\n}\n")
            (world / "probe.fsh").write_text(
                "varying vec2 uv;\nuniform float viewWidth;\n"
                "vec2 size = vec2(viewWidth);\nvoid main() {\ngl_FragData[0] = vec4(uv / size, 0.0, 1.0);\n}\n")
            output = root / "probe.slang"
            converter.emit_program(root, "probe", "world0", output, fullscreen=True)
            text = output.read_text()
            self.assertNotIn("cbuffer JavaUniforms", text)
            self.assertIn("#define PERLIN_JAVA_UNIFORMS", text)
            self.assertIn("mul(first, second)", text)
            self.assertIn("transpose(first)[1]", text)
            self.assertIn("output.clipPosition = float4(input.a_position.xy, 0.0, 1.0)", text)
            self.assertLess(text.index("fs_stage::viewWidth = u_jviewWidth"), text.index("fs_stage::stageMain();"))


if __name__ == "__main__":
    unittest.main()
