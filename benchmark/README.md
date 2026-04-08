# VectorSSD API

## Install pybind11
```bash
pip install pybind11
```

You may want to install it on systems like the following:
```bash
sudo apt update
sudo apt install pybind11-dev
```

## Dependency option for CMake build
It is required to check where the pybind11 has been installed using the following:
```bash
python -m pybind11 --cmakedir
# E.g., /home/dhmin/miniconda3/envs/mdh/lib/python3.9/site-packages/pybind11/share/cmake/pybind11
```

Create makefile using cmake with the dependency options:
```bash
mkdir build
cd build
cmake .. -Dpybind11_DIR=/home/dhmin/miniconda3/env
s/mdh/lib/python3.9/site-packages/pybind11/share/cmake/pybind11
```

After that build using make
```bash
make
```

The python program can access the vectorssd's packages like the following:
```bash
Python 3.9.21
[GCC 11.2.0] :: Anaconda, Inc. on linux
Type "help", "copyright", "credits" or "license" for more information.
>>> import sys
>>> sys.path.append('./build')
>>> import vectorssd
>>>
```
