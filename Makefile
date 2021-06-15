build:
	python3 -m pip install numpy cython
	python3 setup.py build_ext  --inplace
