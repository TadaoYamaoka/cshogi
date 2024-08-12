from setuptools import setup
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


packages = [
    "cshogi",
    "cshogi.dlshogi",
    "cshogi.gym_shogi",
    "cshogi.gym_shogi.envs",
    "cshogi.usi",
    "cshogi.web",
]

package_data = {"": ["*"], "cshogi.web": ["static/*", "templates/*"]}

extras_require = {
    ':python_version == "3.6"': ["numpy>=1.19.5,<1.20.0"],
    ':python_version == "3.7"': ["numpy>=1.21.6,<1.22.0"],
    ':python_version >= "3.12" and python_version < "4.0"': ["numpy>=1.26.0,<1.27.0"],
    ':python_version >= "3.8" and python_version < "3.12"': ["numpy"],
    "web": ["flask", "portpicker"],
}

setup_kwargs = {
    "name": "cshogi",
    "version": "0.8.7",
    "description": "A fast Python shogi library",
    "long_description": None,
    "author": "Tadao Yamaoka",
    "author_email": "tadaoyamaoka@gmail.com",
    "maintainer": "Tadao Yamaoka",
    "maintainer_email": "tadaoyamaoka@gmail.com",
    "url": "https://github.com/TadaoYamaoka/cshogi",
    "packages": packages,
    "package_data": package_data,
    "extras_require": extras_require,
    "python_requires": ">=3.6",
    "ext_modules": cythonize(ext_modules, language_level="3"),
    "cmdclass": {"build_ext": MyBuildExt},
}

setup(**setup_kwargs)
