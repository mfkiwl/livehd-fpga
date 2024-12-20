#/bin/bash

FIRRTL=$1
LGSHELL=./bazel-bin/main/lgshell
bazel build -c dbg //main:all

rm -rf BM_LNAST_HIF
rm -rf BM_LNAST_LN
rm -rf benchmark/ln/*
rm -rf benchmark/firrtl/*
 
mkdir -p benchmark/firrtl/
mkdir -p benchmark/ln/

cp -f $FIRRTL benchmark/firrtl/test.pb

if [ ! -f $LGSHELL ]; then
  echo "could not find $LGSHELL"
  exit
else
  $LGSHELL "inou.firrtl.tolnast files:$FIRRTL |> lnast.print odir:benchmark/ln/"
fi

BENCH=./bazel-bin/benchmark/bm_format
bazel build -c opt //benchmark:all
if [ ! -f $BENCH ]; then
  echo "could not find $BENCH"
  exit
else
 $BENCH
  echo "Size comparison"
  du -sh $FIRRTL
  du -sh BM_LNAST_HIF
  du -sh BM_LNAST_LN
  rm -rf BM_LNAST_HIF
  rm -rf BM_LNAST_LN
  # rm -rf benchmark/ln/
  # rm -rf benchmark/firrtl/
fi
