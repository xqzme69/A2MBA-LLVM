; RUN: env A2MBA_OPTIONS="mode=verified;level=heavy;seed=9000;functions=all;hybrid=ir;probability=100;depth=12" %a2mba_opt -passes=a2mba,verify -S "%s" -o - | %FileCheck "%s" --check-prefix=GEOMETRIC
; RUN: env A2MBA_OPTIONS="mode=verified;level=heavy;seed=9002;functions=all;hybrid=ir;probability=100;depth=12" %a2mba_opt -passes=a2mba,verify -S "%s" -o - | %FileCheck "%s" --check-prefix=NEWTON

target triple = "x86_64-unknown-linux-gnu"

define i32 @hardening_pair(i32 %lhs, i32 %rhs) {
entry:
  %value = xor i32 %lhs, %rhs
  ret i32 %value
}

; GEOMETRIC-LABEL: define i32 @hardening_pair
; GEOMETRIC: a2mba.inverse.factor = add i32 1
; GEOMETRIC: a2mba.inverse.error.square = mul i32
; GEOMETRIC-NOT: a2mba.inverse.refined

; NEWTON-LABEL: define i32 @hardening_pair
; NEWTON: a2mba.inverse.refined =
; NEWTON-NOT: a2mba.inverse.factor
