Compiling
=========

The active development is done on a linux operating system (Ubuntu in particular).
The raytracer is platform independent to a large extend and should work for Windows operating systems. However, this was never tested.
Currently, the Ignis project targets Linux only, but Windows and *hopefully* Mac OS support should follow in the future.

Linux
-----

A quick and dirty tutorial to show how to setup AnyDSL and Ignis on a typical Linux OS.
The following will need ``ccmake``, but works also with ``cmake-gui`` or with plain ``cmake`` as well.

 1. Clone AnyDSL from https://github.com/AnyDSL/anydsl

  1. Copy the ``config.sh.template`` to ``config.sh``.
  2. Change ``: ${BUILD_TYPE:=Debug}`` to ``: ${BUILD_TYPE:=Release}`` inside ``config.sh``
  3. Optional: Use ``ninja`` instead of ``make`` as it is superior in all regards.
  4. Run ``./setup.sh`` inside a terminal at the root directory and get a coffee.

 2. Clone Ignis from https://github.com/PearCoding/Ignis

  1. Make sure all the dependencies listed in official top `README.md <https://github.com/PearCoding/Ignis/blob/master/README.md>`_ are installed.
  2. Run ``mkdir build && cd build && cmake ..``
  3. Run ``ccmake .`` inside the ``build/`` directory.
  4. Change ``AnyDSL_runtime_DIR`` to ``ANYDSL_ROOT/runtime/build/share/anydsl/cmake``, with ``ANYDSL_ROOT`` being the path to the root of the AnyDSL framework.
  5. Select the drivers you want to compile. All interesting options regarding Ignis start with ``IG_``
  6. Run ``cmake --build .`` inside the ``build/`` directory and get your second coffee.

 3. Run ``./bin/igview ../scenes/diamond_scene.json`` inside the ``build/`` directory to see if your setup works.
