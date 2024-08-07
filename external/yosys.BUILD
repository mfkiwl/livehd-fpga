# Copyright 2020 The XLS Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Yosys is a framework for RTL synthesis tools. It currently has extensive
# Verilog-2005 support and provides a basic set of synthesis algorithms for
# various application domains.

load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")
load("@rules_python//python:defs.bzl", "py_binary")

licenses(["restricted"])

exports_files(["LICENSE"])

genrule(
    name = "version_src",
    outs = ["kernel/version.cc"],
    cmd = "echo 'namespace Yosys { extern const char *yosys_version_str; const char *yosys_version_str=\"Yosys livehd+0.9+\"; }' >$@",
)

cc_library(
    name = "version",
    srcs = ["kernel/version.cc"],
    copts = ["-w"],
    visibility = ["//visibility:public"],
)

genrule(
    name = "techmap",
    srcs = [
        "techlibs/common/techmap.v",
    ],
    outs = ["passes/techmap/techmap.inc"],
    cmd = (
        "echo '// autogenerated from techmap.v' > $@;" +
        "echo 'static char stdcells_code[] = {' >> $@;" +
        "od -v -td1 -An $< | sed -e 's/[0-9][0-9]*/&,/g' >> $@;" +
        "echo '0};' >> $@"
    ),
)

genrule(
    name = "verilog_lexer_cpp",
    srcs = ["frontends/verilog/verilog_lexer.l"],
    outs = ["verilog_lexer.cpp"],
    cmd = "M4=$(M4) $(FLEX) --outfile=$@ $<",
    toolchains = [
        "@rules_flex//flex:current_flex_toolchain",
        "@rules_m4//m4:current_m4_toolchain",
    ],
)

genrule(
    name = "verilog_parser",
    srcs = ["frontends/verilog/verilog_parser.y"],
    outs = [
        "frontends/verilog/verilog_parser.tab.cc",
        "frontends/verilog/verilog_parser.tab.hh",
    ],
    cmd = "M4=$(M4) $(BISON) --defines=$(location frontends/verilog/verilog_parser.tab.hh) --output-file=$(location frontends/verilog/verilog_parser.tab.cc) $<",
    # "--name-prefix=xxx",
    toolchains = [
        "@rules_bison//bison:current_bison_toolchain",
        "@rules_m4//m4:current_m4_toolchain",
    ],
)

genrule(
    name = "rtlil_lexer_cpp",
    srcs = ["frontends/rtlil/rtlil_lexer.l"],
    outs = ["rtlil_lexer.cpp"],
    cmd = "M4=$(M4) $(FLEX) --outfile=$@ $<",
    toolchains = [
        "@rules_flex//flex:current_flex_toolchain",
        "@rules_m4//m4:current_m4_toolchain",
    ],
)

genrule(
    name = "rtlil_parser",
    srcs = ["frontends/rtlil/rtlil_parser.y"],
    outs = [
        "frontends/rtlil/rtlil_parser.tab.cc",
        "frontends/rtlil/rtlil_parser.tab.hh",
    ],
    cmd = "M4=$(M4) $(BISON) --defines=$(location frontends/rtlil/rtlil_parser.tab.hh) --output-file=$(location frontends/rtlil/rtlil_parser.tab.cc) $<",
    # "--name-prefix=xxx",
    toolchains = [
        "@rules_bison//bison:current_bison_toolchain",
        "@rules_m4//m4:current_m4_toolchain",
    ],
)

py_binary(
    name = "cellhelp",
    srcs = ["techlibs/common/cellhelp.py"],
    python_version = "PY3",
    srcs_version = "PY3ONLY",
    deps = [
    ],
)

genrule(
    name = "simlib_help",
    srcs = [
        "techlibs/common/simlib.v",
    ],
    outs = ["techlibs/common/simlib_help.inc"],
    cmd = (
        "python3 $(location :cellhelp) $(SRCS) > $@"
    ),
    tools = [":cellhelp"],
)

genrule(
    name = "simcells_help",
    srcs = [
        "techlibs/common/simcells.v",
    ],
    outs = ["techlibs/common/simcells_help.inc"],
    cmd = (
        "python3 $(location :cellhelp) $(SRCS) > $@"
    ),
    tools = [":cellhelp"],
)

GENERATED_HEADERS = [
    "passes/pmgen/ice40_dsp_pm.h",
    "passes/pmgen/ice40_wrapcarry_pm.h",
    "passes/pmgen/test_pmgen_pm.h",
    "passes/pmgen/peepopt_pm.h",
    "passes/pmgen/xilinx_dsp_cascade_pm.h",
    "passes/pmgen/xilinx_dsp_CREG_pm.h",
    "passes/pmgen/xilinx_dsp_pm.h",
    "passes/pmgen/xilinx_dsp48a_pm.h",
    "passes/pmgen/xilinx_srl_pm.h",
]

YOSYS_COPTS = [
    "-DYOSYS_SRC='\"third_party/yosys\"'",
    "-DYOSYS_DATDIR='\"third_party/yosys\"'",
    "-fexceptions",
    "-w",
    "-Wno-implicit-fallthrough",
    "-Wno-vla",
    "-std=c++11",
]

cc_library(
    name = "kernel",
    srcs = glob(
        [
            "kernel/*.cc",
            "backends/**/*.cc",
            "frontends/**/*.cc",
            "passes/**/*.cc",
            "techlibs/**/*.cc",
        ],
        exclude = [
            "kernel/driver.cc",
            "backends/protobuf/*.cc",
            "frontends/verific/*.cc",
            "passes/techmap/filterlib.cc",
        ],
    ) + [
        ":verilog_lexer_cpp",
        ":verilog_parser",
        ":rtlil_lexer_cpp",
        ":rtlil_parser",
        #":ilang_lexer",
        #":ilang_parser",
        "passes/techmap/techmap.inc",
        "techlibs/common/simcells_help.inc",
        "techlibs/common/simlib_help.inc",
    ],
    hdrs = glob(
        [
            "kernel/*.h",
            "kernel/*.inc",
            "backends/**/*.h",
            "frontends/**/*.h",
            "passes/**/*.h",
            "techlibs/**/*.h",
        ],
        exclude = [
            "backends/protobuf/*.h",
        ],
    ) + GENERATED_HEADERS,
    copts = YOSYS_COPTS,
    defines = [
        "_YOSYS_",
        # "YOSYS_ENABLE_PLUGINS",
        # "YOSYS_ENABLE_READLINE",
        "YOSYS_ENABLE_GLOB",
        #"YOSYS_ENABLE_TCL",
        "YOSYS_ENABLE_ABC",
        "YOSYS_ENABLE_COVER",
        'ABCEXTERNAL=\\"abc\\"',
    ],
    #features = ["-use_header_modules"],
    includes = [
        ".",
        "backends/ilang",
        "frontends/ilang",
        "frontends/rtlil",
        "frontends/verilog",
        "passes/techmap",
        "techlibs/common",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":bigint",
        ":ezsat",
        ":json11",
        ":minisat",
        ":sha1",
        ":subcircuit",
        # "@dk_thrysoee_libedit//:pretend_to_be_gnu_readline_system",
        # "@edu_berkeley_abc//:abc-lib",
        "@abc",
        #"@edu_berkeley_abc//:abc-lib",
        # "@org_sourceware_libffi//:libffi",
        #"@tk_tcl_tcl//:tcl",
    ],
    alwayslink = True,
)

cc_test(
    name = "bigint_sample",
    srcs = [
        "libs/bigint/sample.cc",
    ],
    copts = [
        "-fexceptions",
        "-w",
    ],
    features = ["-use_header_modules"],
    licenses = ["notice"],
    deps = [":bigint"],
)

cc_library(
    name = "bigint",
    srcs = [
        "libs/bigint/BigInteger.cc",
        "libs/bigint/BigIntegerAlgorithms.cc",
        "libs/bigint/BigIntegerUtils.cc",
        "libs/bigint/BigUnsigned.cc",
        "libs/bigint/BigUnsignedInABase.cc",
    ],
    hdrs = [
        "libs/bigint/BigInteger.hh",
        "libs/bigint/BigIntegerAlgorithms.hh",
        "libs/bigint/BigIntegerLibrary.hh",
        "libs/bigint/BigIntegerUtils.hh",
        "libs/bigint/BigUnsigned.hh",
        "libs/bigint/BigUnsignedInABase.hh",
        "libs/bigint/NumberlikeArray.hh",
    ],
    copts = [
        "-fexceptions",
        "-w",
    ],
    features = ["-use_header_modules"],
    licenses = ["notice"],
)

cc_test(
    name = "ezsat_demo_vec",
    srcs = [
        "libs/ezsat/demo_vec.cc",
    ],
    licenses = ["notice"],
    deps = [":ezsat"],
)

cc_test(
    name = "ezsat_demo_bit",
    srcs = [
        "libs/ezsat/demo_bit.cc",
    ],
    licenses = ["notice"],
    deps = [":ezsat"],
)

cc_test(
    name = "ezsat_demo_cmp",
    srcs = [
        "libs/ezsat/demo_cmp.cc",
    ],
    licenses = ["notice"],
    deps = [":ezsat"],
)

cc_test(
    name = "ezsat_puzzle3d",
    timeout = "eternal",
    srcs = [
        "libs/ezsat/puzzle3d.cc",
    ],
    licenses = ["notice"],
    deps = [":ezsat"],
)

cc_library(
    name = "ezsat",
    srcs = [
        "libs/ezsat/ezminisat.cc",
        "libs/ezsat/ezsat.cc",
    ],
    hdrs = [
        "libs/ezsat/ezminisat.h",
        "libs/ezsat/ezsat.h",
    ],
    copts = [
        "-fexceptions",
        "-w",
    ],
    features = ["-use_header_modules"],
    licenses = ["notice"],
    deps = [
        ":minisat",
    ],
)

cc_library(
    name = "json11",
    srcs = [
        "libs/json11/json11.cpp",
    ],
    hdrs = ["libs/json11/json11.hpp"],
    copts = ["-w"],
    includes = ["libs/json11"],
    licenses = ["notice"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "minisat",
    srcs = [
        "libs/minisat/Options.cc",
        "libs/minisat/SimpSolver.cc",
        "libs/minisat/Solver.cc",
        "libs/minisat/System.cc",
    ],
    hdrs = [
        "libs/minisat/Alg.h",
        "libs/minisat/Alloc.h",
        "libs/minisat/Dimacs.h",
        "libs/minisat/Heap.h",
        "libs/minisat/IntMap.h",
        "libs/minisat/IntTypes.h",
        "libs/minisat/Map.h",
        "libs/minisat/Options.h",
        "libs/minisat/ParseUtils.h",
        "libs/minisat/Queue.h",
        "libs/minisat/Rnd.h",
        "libs/minisat/SimpSolver.h",
        "libs/minisat/Solver.h",
        "libs/minisat/SolverTypes.h",
        "libs/minisat/Sort.h",
        "libs/minisat/System.h",
        "libs/minisat/Vec.h",
        "libs/minisat/XAlloc.h",
    ],
    copts = [
        "-fexceptions",
        "-w",
    ],
    features = ["-use_header_modules"],
    licenses = ["notice"],
)

cc_library(
    name = "sha1",
    srcs = [
        "libs/sha1/sha1.cpp",
    ],
    hdrs = ["libs/sha1/sha1.h"],
    copts = ["-w"],
    licenses = ["notice"],
)

cc_binary(
    name = "subcircuit_shell",
    srcs = [
        "libs/subcircuit/scshell.cc",
    ],
    licenses = ["notice"],
    deps = [":subcircuit"],
)

cc_library(
    name = "subcircuit",
    srcs = [
        "libs/subcircuit/subcircuit.cc",
    ],
    hdrs = ["libs/subcircuit/subcircuit.h"],
    copts = ["-w"],
    licenses = ["notice"],
)

py_binary(
    name = "pmgen",
    srcs = ["passes/pmgen/pmgen.py"],
    python_version = "PY3",
    srcs_version = "PY3ONLY",
)

genrule(
    name = "peepopt_pm_h",
    srcs = [
        "passes/pmgen/ice40_dsp.pmg",
        "passes/pmgen/ice40_wrapcarry.pmg",
        "passes/pmgen/peepopt_muldiv.pmg",
        "passes/pmgen/peepopt_shiftmul.pmg",
        "passes/pmgen/xilinx_dsp48a.pmg",
        "passes/pmgen/xilinx_dsp_cascade.pmg",
        "passes/pmgen/xilinx_dsp_CREG.pmg",
        "passes/pmgen/xilinx_dsp.pmg",
        "passes/pmgen/xilinx_srl.pmg",
    ],
    outs = ["passes/pmgen/peepopt_pm.h"],
    cmd = "python3 $(location :pmgen) -o $(OUTS) -p peepopt $(SRCS)",
    tools = [
        ":pmgen",
    ],
)

[genrule(
    name = "%s_pm_h" % pm,
    srcs = [
        "passes/pmgen/%s.pmg" % pm,
    ],
    outs = ["passes/pmgen/%s_pm.h" % pm],
    cmd = "python3 $(location :pmgen) -o $(OUTS) -p %s $(SRCS)" % pm,
    tools = [
        ":pmgen",
    ],
) for pm in [
    "ice40_dsp",
    "ice40_wrapcarry",
    "test_pmgen",
    "xilinx_dsp",
    "xilinx_dsp48a",
    "xilinx_dsp_CREG",
    "xilinx_dsp_cascade",
    "xilinx_srl",
]]
