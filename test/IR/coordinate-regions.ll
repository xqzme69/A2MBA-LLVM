; RUN: env A2MBA_OPTIONS="functions=all;hybrid=ir;hybrid-region=rotated-core;probability=100;seed=7" %a2mba_opt -passes=a2mba,a2mba,verify -S "%s" -o "%t.core"
; RUN: %FileCheck "%s" --check-prefix=CORE < "%t.core"
; RUN: not env A2MBA_OPTIONS="hybrid=native;hybrid-region=rotated-core" %a2mba_opt -passes=a2mba -disable-output "%s" 2>&1 | %FileCheck "%s" --check-prefix=INVALID
; INVALID: rotated-core requires hybrid=ir
target triple = "x86_64-pc-windows-msvc"

; CORE-LABEL: define i32 @diamond(
; CORE: %common = add i32 %derived, %y
; CORE: freeze i32 %common
; CORE-NOT: freeze i32 %common
; CORE: ret i32
define i32 @diamond(i32 %x, i32 noundef %y, i32 noundef %z, i32 noundef %t) {
  %derived = lshr i32 %x, 1
  %common = add i32 %derived, %y
  %a = add i32 %common, %t
  %b = xor i32 %common, %z
  %c = xor i32 %a, %b
  ret i32 %c
}

; CORE-LABEL: define i64 @flagged(
; CORE: %boundary = add nuw i64
; CORE: freeze i64 %boundary
; CORE: a2mba.coord.result
; CORE: ret i64
define i64 @flagged(i64 %x, i64 %y) {
  %boundary = add nuw i64 %x, %y
  %a = add i64 %boundary, %x
  %b = or i64 %a, %y
  ret i64 %b
}

; CORE-LABEL: define i32 @external_user(
; CORE-NOT: a2mba.coord
; CORE: store volatile
; CORE-NOT: a2mba.coord
; CORE: ret i32
define i32 @external_user(i32 %x, i32 %y, ptr %p) {
  %a = add i32 %x, %y
  store volatile i32 %a, ptr %p
  %b = xor i32 %a, %y
  ret i32 %b
}
