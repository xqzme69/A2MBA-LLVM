; RUN: env A2MBA_OPTIONS="mode=verified;level=medium;seed=54;functions=all;hybrid=ir;transform=auto;probability=100;depth=8;stats=true" %a2mba_opt -passes=a2mba -disable-output %s 2>&1 | %FileCheck %s --check-prefix=STATS
; RUN: env A2MBA_OPTIONS="mode=verified;level=medium;seed=54;functions=all;hybrid=ir;transform=auto;probability=100;depth=8" %a2mba_opt -passes=a2mba -S %s -o - | %FileCheck %s --check-prefix=IR
; RUN: env A2MBA_OPTIONS="mode=verified;level=medium;seed=54;functions=all;hybrid=ir;hybrid-layers=context-adc;probability=100;depth=8;stats=true" %a2mba_opt -passes=a2mba -disable-output %s 2>&1 | %FileCheck %s --check-prefix=LAYER-STATS
; RUN: env A2MBA_OPTIONS="mode=verified;level=medium;seed=54;functions=all;hybrid=ir;hybrid-layers=context-adc;probability=100;depth=8" %a2mba_opt -passes=a2mba -S %s -o - | %FileCheck %s --check-prefix=LAYER-IR

target triple = "x86_64-unknown-linux-gnu"

define i64 @hybrid_ir(i64 %lhs, i64 %rhs) {
entry:
  %sum = add i64 %lhs, %rhs
  ret i64 %sum
}

; STATS: hybrid IR: 1
; STATS: hybrid native: 0
; LAYER-STATS: context trap: 1
; LAYER-STATS: ADC: 1
; LAYER-STATS: hybrid IR: 1
; IR-LABEL: define i64 @hybrid_ir(i64 %lhs, i64 %rhs) {{.*}}!a2mba.protected
; IR-NEXT: entry:
; IR-NEXT: [[LEFT:%[^ ]+]] = freeze i64 %lhs, !a2mba.generated
; IR-NEXT: [[RIGHT:%[^ ]+]] = freeze i64 %rhs, !a2mba.generated
; IR-COUNT-8: !a2mba.generated
; IR-NOT: %sum = add i64 %lhs, %rhs
; IR: ret i64
; IR: !a2mba.processed = !{
; LAYER-IR: ashr i64
; LAYER-IR: call i64 asm sideeffect
