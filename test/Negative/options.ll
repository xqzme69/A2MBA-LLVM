; RUN: not env A2MBA_OPTIONS="mode=unsafe" %a2mba_opt -passes=a2mba -disable-output %s 2>&1 | %FileCheck %s --check-prefix=MODE
; RUN: not env A2MBA_OPTIONS="level=extreme" %a2mba_opt -passes=a2mba -disable-output %s 2>&1 | %FileCheck %s --check-prefix=LEVEL
; RUN: not env A2MBA_OPTIONS="seed=minus-one" %a2mba_opt -passes=a2mba -disable-output %s 2>&1 | %FileCheck %s --check-prefix=SEED
; RUN: not env A2MBA_OPTIONS="functions=regex:" %a2mba_opt -passes=a2mba -disable-output %s 2>&1 | %FileCheck %s --check-prefix=FUNCTIONS
; RUN: not env A2MBA_OPTIONS="transform=unknown" %a2mba_opt -passes=a2mba -disable-output %s 2>&1 | %FileCheck %s --check-prefix=TRANSFORM
; RUN: not env A2MBA_OPTIONS="probability=101" %a2mba_opt -passes=a2mba -disable-output %s 2>&1 | %FileCheck %s --check-prefix=PROBABILITY
; RUN: not env A2MBA_OPTIONS="depth=0" %a2mba_opt -passes=a2mba -disable-output %s 2>&1 | %FileCheck %s --check-prefix=DEPTH
; RUN: not env A2MBA_OPTIONS="stats=perhaps" %a2mba_opt -passes=a2mba -disable-output %s 2>&1 | %FileCheck %s --check-prefix=STATS
; RUN: not env A2MBA_OPTIONS="mystery=true" %a2mba_opt -passes=a2mba -disable-output %s 2>&1 | %FileCheck %s --check-prefix=UNKNOWN
; RUN: not env A2MBA_OPTIONS="mode=verified;transform=paper-rcr-rcl" %a2mba_opt -passes=a2mba -disable-output %s 2>&1 | %FileCheck %s --check-prefix=PAPER
; RUN: not env A2MBA_OPTIONS="hybrid=unknown" %a2mba_opt -passes=a2mba -disable-output %s 2>&1 | %FileCheck %s --check-prefix=HYBRID
; RUN: not env A2MBA_OPTIONS="hybrid=ir;transform=rule-explosion" %a2mba_opt -passes=a2mba -disable-output %s 2>&1 | %FileCheck %s --check-prefix=HYBRID-TRANSFORM
; RUN: not env A2MBA_OPTIONS="hybrid=ir;transform=modular-scale" %a2mba_opt -passes=a2mba -disable-output %s 2>&1 | %FileCheck %s --check-prefix=HYBRID-TRANSFORM
; RUN: not env A2MBA_OPTIONS="mode=paper;hybrid=native;transform=paper-rcr-rcl" %a2mba_opt -passes=a2mba -disable-output %s 2>&1 | %FileCheck %s --check-prefix=HYBRID-TRANSFORM
; RUN: not env A2MBA_OPTIONS="hybrid=ir;depth=1" %a2mba_opt -passes=a2mba -disable-output %s 2>&1 | %FileCheck %s --check-prefix=HYBRID-DEPTH
; RUN: not env A2MBA_OPTIONS="hybrid=ir;depth=2" %a2mba_opt -passes=a2mba -disable-output %s 2>&1 | %FileCheck %s --check-prefix=HYBRID-DEPTH
; RUN: not env A2MBA_OPTIONS="hybrid=native;depth=17" %a2mba_opt -passes=a2mba -disable-output %s 2>&1 | %FileCheck %s --check-prefix=HYBRID-DEPTH
; RUN: not env A2MBA_OPTIONS="hybrid-layers=context-random" %a2mba_opt -passes=a2mba -disable-output %s 2>&1 | %FileCheck %s --check-prefix=HYBRID-LAYERS-REQUIRES-HYBRID
; RUN: not env A2MBA_OPTIONS="hybrid=ir;hybrid-layers=unknown" %a2mba_opt -passes=a2mba -disable-output %s 2>&1 | %FileCheck %s --check-prefix=HYBRID-LAYERS
; RUN: not env A2MBA_OPTIONS="hybrid=ir;hybrid-layers=context-adc;transform=sbb" %a2mba_opt -passes=a2mba -disable-output %s 2>&1 | %FileCheck %s --check-prefix=HYBRID-LAYERS-TRANSFORM

target triple = "x86_64-unknown-linux-gnu"

define void @valid_input() {
entry:
  ret void
}

; MODE: invalid A2MBA option mode=unsafe
; LEVEL: invalid A2MBA option level=extreme
; SEED: invalid A2MBA option seed=minus-one
; FUNCTIONS: invalid A2MBA option functions=regex:
; TRANSFORM: invalid A2MBA option transform=unknown
; PROBABILITY: invalid A2MBA option probability=101
; DEPTH: invalid A2MBA option depth=0
; STATS: stats expects true or false
; UNKNOWN: unknown A2MBA option: mystery
; PAPER: paper-rcr-rcl is available only with mode=paper
; HYBRID: invalid A2MBA option hybrid=unknown
; HYBRID-TRANSFORM: hybrid mode accepts only auto, context-trap, adc, or sbb as an outer layer
; HYBRID-DEPTH: hybrid depth must be between 3 and 16
; HYBRID-LAYERS-REQUIRES-HYBRID: hybrid-layers requires hybrid=ir or hybrid=native
; HYBRID-LAYERS: invalid A2MBA option hybrid-layers=unknown
; HYBRID-LAYERS-TRANSFORM: hybrid-layers cannot be combined with an explicit transform
