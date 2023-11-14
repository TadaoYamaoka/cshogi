from setuptools import Extension, setup
from setuptools.command.build_ext import build_ext


class my_build_ext(build_ext):
    def build_extensions(self):
        if self.compiler.compiler_type == "unix":
            for e in self.extensions:
                e.extra_compile_args = [
                    "-msse4.2",
                    "-mavx2",
                    "-Wno-enum-constexpr-conversion",
                ]

        build_ext.build_extensions(self)

    def finalize_options(self):
        build_ext.finalize_options(self)
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
        [
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
        define_macros=[],
    ),
    Extension(
        "cshogi.gym_shogi.envs.shogi_env",
        ["cshogi/gym_shogi/envs/shogi_env.pyx"],
        language="c++",
    ),
    Extension(
        "cshogi.gym_shogi.envs.shogi_vec_env",
        ["cshogi/gym_shogi/envs/shogi_vec_env.pyx"],
        language="c++",
    ),
]

setup(
    name="cshogi",
    version="0.5.5",
    setup_requires=["numpy", "Cython"],
    packages=[
        "cshogi",
        "cshogi.usi",
        "cshogi.gym_shogi",
        "cshogi.gym_shogi.envs",
        "cshogi.dlshogi",
        "cshogi.web",
        "cshogi.web.templates",
        "cshogi.web.static",
    ],
    package_data={"cshogi.web.templates": ["*"], "cshogi.web.static": ["*"]},
    ext_modules=ext_modules,
    cmdclass={"build_ext": my_build_ext},
    author="Tadao Yamaoka",
    url="https://github.com/TadaoYamaoka/cshogi",
    description="A fast Python shogi library",
    classifiers=[
        "Programming Language :: Python :: 3",
        "License :: OSI Approved :: GNU General Public License (GPL)",
        "Operating System :: OS Independent",
    ],
<<<<<<< HEAD
=======
    setup_requires=["numpy"],
    install_requires=["numpy"],
    extras_require={"web": ["flask", "portpicker"]},
>>>>>>> 68a7bf6 (Further update.)
)
