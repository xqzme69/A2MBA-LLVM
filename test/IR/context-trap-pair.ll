; RUN: env A2MBA_OPTIONS="mode=verified;level=heavy;seed=31;functions=regex:^agt_i32$;transform=context-trap;probability=100;depth=1" %a2mba_opt -passes=a2mba -S %s -o - | %FileCheck %s --check-prefix=I32
; RUN: env A2MBA_OPTIONS="mode=verified;level=heavy;seed=31;functions=regex:^agt_i64$;transform=context-trap;probability=100;depth=1" %a2mba_opt -passes=a2mba -S %s -o - | %FileCheck %s --check-prefix=I64
; RUN: env A2MBA_OPTIONS="mode=verified;level=heavy;seed=1;functions=regex:^agt_i32$;transform=context-trap;probability=100;depth=1" %a2mba_opt -passes=a2mba -S %s -o - | %FileCheck %s --check-prefix=ADD-SUB-OR
; RUN: env A2MBA_OPTIONS="mode=verified;level=heavy;seed=8;functions=regex:^agt_i32$;transform=context-trap;probability=100;depth=1" %a2mba_opt -passes=a2mba -S %s -o - | %FileCheck %s --check-prefix=SUB-ADD-XOR
; RUN: env A2MBA_OPTIONS="mode=verified;level=heavy;seed=12;functions=regex:^agt_i32$;transform=context-trap;probability=100;depth=1" %a2mba_opt -passes=a2mba -S %s -o - | %FileCheck %s --check-prefix=XOR-CHAIN-ADD
; RUN: env A2MBA_OPTIONS="mode=verified;level=heavy;seed=2;functions=regex:^agt_i32$;transform=context-trap;probability=100;depth=1" %a2mba_opt -passes=a2mba -S %s -o - | %FileCheck %s --check-prefix=OR-XOR
; RUN: env A2MBA_OPTIONS="mode=verified;level=heavy;seed=16;functions=regex:^agt_i32$;transform=context-trap;probability=100;depth=1" %a2mba_opt -passes=a2mba -S %s -o - | %FileCheck %s --check-prefix=TRIGGER-CANCEL-OR
; RUN: env A2MBA_OPTIONS="mode=verified;level=heavy;seed=5;functions=regex:^agt_i32$;transform=context-trap;probability=100;depth=1" %a2mba_opt -passes=a2mba -S %s -o - | %FileCheck %s --check-prefix=TRAP-DELTA-ADD

target triple = "x86_64-unknown-linux-gnu"

define i32 @agt_i32(i32 %lhs, i32 %rhs) {
entry:
  %sum = add i32 %lhs, %rhs
  ret i32 %sum
}

define i64 @agt_i64(i64 %lhs, i64 %rhs) {
entry:
  %sum = add i64 %lhs, %rhs
  ret i64 %sum
}

; I32-LABEL: define i32 @agt_i32(i32 %lhs, i32 %rhs) {{.*}}!a2mba.protected
; I32-NEXT: entry:
; I32-NEXT: [[ORIGINAL:%[^ ]+]] = add i32 %lhs, %rhs, !a2mba.generated ![[GENERATED:[0-9]+]]
; I32-NEXT: [[LOW:%[^ ]+]] = and i32 [[ORIGINAL]], 1, !a2mba.generated ![[GENERATED]]
; I32-NEXT: [[HIGH:%[^ ]+]] = and i32 [[ORIGINAL]], -2, !a2mba.generated ![[GENERATED]]
; I32-NEXT: [[TRAP_INPUT:%[^ ]+]] = and i32 [[ORIGINAL]], 268435455, !a2mba.generated ![[GENERATED]]
; I32-NEXT: [[TRAP_SHIFTED:%[^ ]+]] = shl i32 [[TRAP_INPUT]], 3, !a2mba.generated ![[GENERATED]]
; I32-NEXT: [[TRAP_RESTORED:%[^ ]+]] = ashr i32 [[TRAP_SHIFTED]], 3, !a2mba.generated ![[GENERATED]]
; I32-NEXT: [[TRAP_PROJECTED:%[^ ]+]] = and i32 [[TRAP_RESTORED]], 536870913, !a2mba.generated ![[GENERATED]]
; I32-NEXT: [[TRIGGER_INPUT:%[^ ]+]] = or i32 [[ORIGINAL]], 268435456, !a2mba.generated ![[GENERATED]]
; I32-NEXT: [[TRIGGER_SHIFTED:%[^ ]+]] = shl i32 [[TRIGGER_INPUT]], 3, !a2mba.generated ![[GENERATED]]
; I32-NEXT: [[TRIGGER_RESTORED:%[^ ]+]] = ashr i32 [[TRIGGER_SHIFTED]], 3, !a2mba.generated ![[GENERATED]]
; I32-NEXT: [[TRIGGER_PROJECTED:%[^ ]+]] = and i32 [[TRIGGER_RESTORED]], 536870913, !a2mba.generated ![[GENERATED]]
; I32-NEXT: [[TRIGGER_LOW:%[^ ]+]] = xor i32 [[TRIGGER_PROJECTED]], 536870912, !a2mba.generated ![[GENERATED]]
; I32-NEXT: [[DELTA:%[^ ]+]] = sub i32 [[TRIGGER_LOW]], [[LOW]], !a2mba.generated ![[GENERATED]]
; I32-NEXT: [[PROTECTED_LOW:%[^ ]+]] = add i32 [[TRAP_PROJECTED]], [[DELTA]], !a2mba.generated ![[GENERATED]]
; I32-NEXT: [[RESULT:%[^ ]+]] = add i32 [[HIGH]], [[PROTECTED_LOW]], !a2mba.generated ![[GENERATED]]
; I32-NEXT: ret i32 [[RESULT]]

; I64-LABEL: define i64 @agt_i64(i64 %lhs, i64 %rhs) {{.*}}!a2mba.protected
; I64-NEXT: entry:
; I64-NEXT: [[ORIGINAL:%[^ ]+]] = add i64 %lhs, %rhs, !a2mba.generated ![[GENERATED:[0-9]+]]
; I64-NEXT: [[LOW:%[^ ]+]] = and i64 [[ORIGINAL]], 1, !a2mba.generated ![[GENERATED]]
; I64-NEXT: [[HIGH:%[^ ]+]] = and i64 [[ORIGINAL]], -2, !a2mba.generated ![[GENERATED]]
; I64-NEXT: [[TRAP_INPUT:%[^ ]+]] = and i64 [[ORIGINAL]], 1152921504606846975, !a2mba.generated ![[GENERATED]]
; I64-NEXT: [[TRAP_SHIFTED:%[^ ]+]] = shl i64 [[TRAP_INPUT]], 3, !a2mba.generated ![[GENERATED]]
; I64-NEXT: [[TRAP_RESTORED:%[^ ]+]] = ashr i64 [[TRAP_SHIFTED]], 3, !a2mba.generated ![[GENERATED]]
; I64-NEXT: [[TRAP_PROJECTED:%[^ ]+]] = and i64 [[TRAP_RESTORED]], 2305843009213693953, !a2mba.generated ![[GENERATED]]
; I64-NEXT: [[TRIGGER_INPUT:%[^ ]+]] = or i64 [[ORIGINAL]], 1152921504606846976, !a2mba.generated ![[GENERATED]]
; I64-NEXT: [[TRIGGER_SHIFTED:%[^ ]+]] = shl i64 [[TRIGGER_INPUT]], 3, !a2mba.generated ![[GENERATED]]
; I64-NEXT: [[TRIGGER_RESTORED:%[^ ]+]] = ashr i64 [[TRIGGER_SHIFTED]], 3, !a2mba.generated ![[GENERATED]]
; I64-NEXT: [[TRIGGER_PROJECTED:%[^ ]+]] = and i64 [[TRIGGER_RESTORED]], 2305843009213693953, !a2mba.generated ![[GENERATED]]
; I64-NEXT: [[TRIGGER_LOW:%[^ ]+]] = xor i64 [[TRIGGER_PROJECTED]], 2305843009213693952, !a2mba.generated ![[GENERATED]]
; I64-NEXT: [[DELTA:%[^ ]+]] = sub i64 [[TRIGGER_LOW]], [[LOW]], !a2mba.generated ![[GENERATED]]
; I64-NEXT: [[PROTECTED_LOW:%[^ ]+]] = add i64 [[TRAP_PROJECTED]], [[DELTA]], !a2mba.generated ![[GENERATED]]
; I64-NEXT: [[RESULT:%[^ ]+]] = add i64 [[HIGH]], [[PROTECTED_LOW]], !a2mba.generated ![[GENERATED]]
; I64-NEXT: ret i64 [[RESULT]]

; ADD-SUB-OR: [[COMBINED:%a2mba.agt.combined[^ ]*]] = add i32 [[TRIGGER:%[^ ]+]], [[TRAP:%[^ ]+]], !a2mba.generated
; ADD-SUB-OR-NEXT: [[PROTECTED:%[^ ]+]] = sub i32 [[COMBINED]], {{%[^ ]+}}, !a2mba.generated
; ADD-SUB-OR-NEXT: {{%[^ ]+}} = or i32 {{%[^ ]+}}, [[PROTECTED]], !a2mba.generated

; SUB-ADD-XOR: [[DIFFERENCE:%a2mba.agt.difference[^ ]*]] = sub i32 [[TRIGGER:%[^ ]+]], [[TRAP:%[^ ]+]], !a2mba.generated
; SUB-ADD-XOR-NEXT: [[PROTECTED:%[^ ]+]] = add i32 [[DIFFERENCE]], {{%[^ ]+}}, !a2mba.generated
; SUB-ADD-XOR-NEXT: {{%[^ ]+}} = xor i32 {{%[^ ]+}}, [[PROTECTED]], !a2mba.generated

; XOR-CHAIN-ADD: [[PAIRED:%a2mba.agt.paired[^ ]*]] = xor i32 [[TRIGGER:%[^ ]+]], [[TRAP:%[^ ]+]], !a2mba.generated
; XOR-CHAIN-ADD-NEXT: [[PROTECTED:%[^ ]+]] = xor i32 [[PAIRED]], {{%[^ ]+}}, !a2mba.generated
; XOR-CHAIN-ADD-NEXT: {{%[^ ]+}} = add i32 {{%[^ ]+}}, [[PROTECTED]], !a2mba.generated

; OR-XOR: [[PROTECTED:%a2mba.agt.protected[^ ]*]] = or i32 [[TRIGGER:%[^ ]+]], [[TRAP:%[^ ]+]], !a2mba.generated
; OR-XOR-NEXT: {{%[^ ]+}} = xor i32 {{%[^ ]+}}, [[PROTECTED]], !a2mba.generated

; TRIGGER-CANCEL-OR: [[CANCEL:%a2mba.agt.cancel[^ ]*]] = xor i32 [[TRAP:%[^ ]+]], {{%[^ ]+}}, !a2mba.generated
; TRIGGER-CANCEL-OR-NEXT: [[PROTECTED:%[^ ]+]] = add i32 [[TRIGGER:%[^ ]+]], [[CANCEL]], !a2mba.generated
; TRIGGER-CANCEL-OR-NEXT: {{%[^ ]+}} = or i32 {{%[^ ]+}}, [[PROTECTED]], !a2mba.generated

; TRAP-DELTA-ADD: [[DELTA:%a2mba.agt.delta[^ ]*]] = sub i32 [[TRIGGER:%[^ ]+]], {{%[^ ]+}}, !a2mba.generated
; TRAP-DELTA-ADD-NEXT: [[PROTECTED:%[^ ]+]] = add i32 [[TRAP:%[^ ]+]], [[DELTA]], !a2mba.generated
; TRAP-DELTA-ADD-NEXT: {{%[^ ]+}} = add i32 {{%[^ ]+}}, [[PROTECTED]], !a2mba.generated
