from Cython.Build import build_ext, cythonize
from setuptools import Extension


class MyBuildExt(build_ext):
    def build_extensions(self):
        if self.compiler.compiler_type == "unix":
            for e in self.extensions:
                e.extra_compile_args.extend(
                    [
                        "-std=c++11",
                        "-msse4.2",
                        "-mavx2",
                        "-Wno-enum-constexpr-conversion",
                    ]
                )
        elif self.compiler.compiler_type == "msvc":
            for e in self.extensions:
                e.extra_compile_args.extend(
                    [
                        "/std:c11",
                        "/arch:SSE42",
                        "/arch:AVX2",
                    ]
                )

        super(MyBuildExt, self).build_extensions()

    def finalize_options(self):
        super(MyBuildExt, self).finalize_options()
        try:
            __builtins__.__NUMPY_SETUP__ = False
        except:
            import builtins

            builtins.__NUMPY_SETUP__ = False

        import numpy

        self.include_dirs.append(numpy.get_include())


ext_modules = [
    Extension(
        "cshogi._cshogi",
        sources=[
            "cshogi/_cshogi.pyx",
            "src/bitboard.cpp",
            "src/common.cpp",
            "src/generateMoves.cpp",
            "src/hand.cpp",
            "src/init.cpp",
            "src/move.cpp",
            "src/mt64bit.cpp",
            "src/position.cpp",
            "src/search.cpp",
            "src/square.cpp",
            "src/usi.cpp",
            "src/book.cpp",
            "src/mate.cpp",
            "src/dfpn.cpp",
        ],
        language="c++",
        include_dirs=["src"],
        define_macros=[
            ("HAVE_SSE4", None),
            ("HAVE_SSE42", None),
            ("HAVE_AVX2", None),
        ],
    ),
    Extension(
        "cshogi.gym_shogi.envs.shogi_env",
        sources=["cshogi/gym_shogi/envs/shogi_env.pyx"],
        language="c++",
    ),
    Extension(
        "cshogi.gym_shogi.envs.shogi_vec_env",
        sources=["cshogi/gym_shogi/envs/shogi_vec_env.pyx"],
        language="c++",
    ),
]


def build(setup_kwargs):
    setup_kwargs.update(
        {
            "ext_modules": cythonize(ext_modules, language_level="3"),
            "cmdclass": {"build_ext": MyBuildExt},
            "package_data": {"cshogi.web.templates": ["*"], "cshogi.web.static": ["*"]},
            "extras_require": {"web": ["flask", "portpicker"]},
        }
    )
