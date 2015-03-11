dnl -*- autoconf -*-
# Macros needed to find dune-ax1 and dependent libraries.  They are called by
# the macros in ${top_src_dir}/dependencies.m4, which is generated by
# "dunecontrol autogen"

# Additional checks needed to build dune-ax1
# This macro should be invoked by every module which depends on dune-ax1, as
# well as by dune-ax1 itself
AC_DEFUN([DUNE_AX1_CHECKS],[

  AC_REQUIRE([DUNE_PATH_HDF5])

])

# Additional checks needed to find dune-ax1
# This macro should be invoked by every module which depends on dune-ax1, but
# not by dune-ax1 itself
AC_DEFUN([DUNE_AX1_CHECK_MODULE],
[
  DUNE_CHECK_MODULES([dune-ax1],[ax1/ax1.hh])
])
