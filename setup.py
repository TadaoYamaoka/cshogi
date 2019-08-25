from distutils.core import setup

from Cython.Build import cythonize
import numpy

setup(name='cshogi',
      ext_modules=cythonize("cshogi/_cshogi.pyx"),
      include_dirs = ["src", numpy.get_include()])
