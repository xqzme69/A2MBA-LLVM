; REQUIRES: x86-registered-target
; RUN: env A2MBA_OPTIONS="mode=verified;level=light;seed=41;functions=all;transform=adc;probability=100;depth=1" %a2mba_opt -passes=a2mba -S %s -o - | %FileCheck %s --check-prefixes=IR,%a2mba_redzone_prefix
; RUN: env A2MBA_OPTIONS="mode=verified;level=light;seed=41;functions=all;transform=adc;probability=100;depth=1" %a2mba_opt -passes=a2mba %s -o - | %llc -mtriple=x86_64-unknown-linux-gnu -O0 -o - | %FileCheck %s

target triple = "x86_64-unknown-linux-gnu"

define i32 @adc_i32(i32 %lhs, i32 %rhs) {
entry:
  %sum = add i32 %lhs, %rhs
  ret i32 %sum
}

define i64 @adc_i64(i64 %lhs, i64 %rhs) {
entry:
  %sum = add i64 %lhs, %rhs
  ret i64 %sum
}

; CHECK-LABEL: adc_i32:
; CHECK: pushfq
; CHECK: stc
; CHECK: adcl
; CHECK-COUNT-2: subl
; CHECK: popfq
; CHECK-LABEL: adc_i64:
; CHECK: pushfq
; CHECK: stc
; CHECK: adcq
; CHECK-COUNT-2: subq
; CHECK: popfq

; IR-LABEL: define i32 @adc_i32(i32 %lhs, i32 %rhs)
; IR-DYNAMIC-SAME: #[[ATTR:[0-9]+]]
; IR-SAME: !a2mba.protected
; IR: call i32 asm sideeffect
; IR-STATIC-SAME: leaq -128(%rsp), %rsp
; IR-STATIC-SAME: pushfq
; IR-STATIC-SAME: popfq
; IR-STATIC-SAME: leaq 128(%rsp), %rsp
; IR-SAME: "=&r,0,r,~{memory},~{flags}"
; IR-STATIC-NOT: noredzone
; IR-DYNAMIC: attributes #[[ATTR]] = { {{.*}}noredzone{{.*}} }
