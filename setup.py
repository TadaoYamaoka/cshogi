from distutils.core import setup

from Cython.Build import cythonize
import numpy

setup(name='cshogi',
      version='0.0.1',
      packages=[''],
      ext_modules=cythonize("cshogi/_cshogi.pyx"),
      include_dirs = ["src", numpy.get_include()])
