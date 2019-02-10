from distutils.core import setup

from Cython.Build import cythonize

setup(name='cython-shogi',
      ext_modules=cythonize("cshogi.pyx"))
