from pybind11.setup_helpers import Pybind11Extension, build_ext
from setuptools import setup, Extension
import pybind11

# Define the extension module
ext_modules = [
    Pybind11Extension(
        "latentspeed",
        [
            "src/python_bindings.cpp",
            "src/trading_engine_service.cpp",
        ],
        include_dirs=[
            "include",
            "ccapi/include",
            pybind11.get_cmake_dir() + "/../../../include",
        ],
        libraries=["zmq", "ssl", "crypto", "curl"],
        language='c++',
        cxx_std=17,
    ),
]

setup(
    name="latentspeed",
    version="0.1.0",
    author="Your Name",
    author_email="your.email@example.com",
    description="Latentspeed Trading Engine Python Bindings",
    long_description="High-performance C++ trading engine with Python bindings for algorithmic trading",
    ext_modules=ext_modules,
    cmdclass={"build_ext": build_ext},
    python_requires=">=3.7",
    install_requires=["pybind11>=2.10.0"],
    zip_safe=False,
)
