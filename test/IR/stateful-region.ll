; RUN: env A2MBA_OPTIONS="mode=verified;level=medium;seed=904;functions=regex:^stateful_chain$;hybrid=ir;hybrid-region=stateful;hybrid-layers=none;probability=100;depth=8;stats=true" %a2mba_opt -passes=a2mba -disable-output "%s" 2>&1 | %FileCheck "%s" --check-prefix=STATS
; RUN: env A2MBA_OPTIONS="mode=verified;level=medium;seed=904;functions=regex:^stateful_chain$;hybrid=ir;hybrid-region=stateful;hybrid-layers=none;probability=100;depth=8" %a2mba_opt -passes=a2mba -S "%s" -o - | %FileCheck "%s" --check-prefix=IR
; RUN: env A2MBA_OPTIONS="mode=verified;level=medium;seed=904;functions=regex:^stateful_chain$;hybrid=ir;hybrid-region=stateful;hybrid-layers=none;probability=100;depth=8" %a2mba_opt -passes=a2mba -S "%s" -o "%t.pre.ll"
; RUN: %opt "-passes=default<O3>" -S "%t.pre.ll" -o "%t.o3.ll"
; RUN: %opt -passes=verify -disable-output "%t.o3.ll"
; RUN: %FileCheck "%s" --check-prefix=STATE-O3 --input-file="%t.o3.ll"
; RUN: %python "%S/../tools/check_nonlinear_ir.py" "%t.o3.ll" --function=stateful_chain --minimum=12
; RUN: env A2MBA_OPTIONS="mode=verified;level=light;seed=1001;functions=regex:^stateful_seed_fallback$;hybrid=ir;hybrid-region=stateful;hybrid-layers=none;probability=100;stats=true;diagnostics=true" %a2mba_opt -passes=a2mba -disable-output "%s" 2>&1 | %FileCheck "%s" --check-prefix=FALLBACK-STATS
; RUN: env A2MBA_OPTIONS="mode=verified;level=light;seed=1001;functions=regex:^stateful_seed_fallback$;hybrid=ir;hybrid-region=stateful;hybrid-layers=none;probability=100" %a2mba_opt -passes=a2mba -S "%s" -o - | %FileCheck "%s" --check-prefix=FALLBACK-IR
; RUN: env A2MBA_OPTIONS="mode=verified;level=light;seed=1001;functions=regex:^stateful_seed_fallback$;hybrid=ir;hybrid-region=stateful;hybrid-layers=none;probability=100" %a2mba_opt -passes=a2mba -S "%s" -o "%t.fallback.pre.ll"
; RUN: %opt "-passes=default<O3>" -S "%t.fallback.pre.ll" -o "%t.fallback.o3.ll"
; RUN: %opt -passes=verify -disable-output "%t.fallback.o3.ll"
; RUN: %python "%S/../tools/check_nonlinear_ir.py" "%t.fallback.o3.ll" --function=stateful_seed_fallback --minimum=12

target triple = "x86_64-unknown-linux-gnu"

define i64 @stateful_chain(i64 %x, i64 %y, i64 %z) {
entry:
  %first = add i64 %x, %y
  %second = xor i64 %first, %z
  %third = add i64 %second, 17
  ret i64 %third
}

define i64 @stateful_seed_fallback(i64 %x, i64 %y) {
entry:
  %first = xor i64 %x, %x
  %second = sub i64 %first, %y
  ret i64 %second
}

; STATS: hybrid IR: 3
; STATS: hybrid native: 0
; STATS: stateful regions: 1
; IR-LABEL: define i64 @stateful_chain
; IR: {{%[^ ]+}} = freeze i64 %x, !a2mba.generated
; IR-NEXT: {{%[^ ]+}} = freeze i64 %y, !a2mba.generated
; IR-NEXT: {{%[^ ]+}} = freeze i64 %z, !a2mba.generated
; IR-NOT: freeze i64
; IR: a2mba.region.state.initial
; IR: a2mba.region.bias{{[0-9]*}} = mul i64
; IR: a2mba.region.key.product{{[0-9]*}} = mul i64
; IR: a2mba.region.encoded{{[0-9]*}} = mul i64
; IR: a2mba.region.decode.scale{{[0-9]*}} = mul i64
; IR: a2mba.region.decode.exit.scale = mul i64
; IR-NOT: freeze i64
; IR-NOT: %first = add i64 %x, %y
; IR-NOT: %second = xor i64 %first, %z
; IR-NOT: %third = add i64 %second, 17

; STATE-O3-LABEL: define i64 @stateful_chain
; STATE-O3-COUNT-3: a2mba.region.state.product{{[0-9]*}} = mul i64 %{{[^,]+}}, %{{[^,]+}}

; FALLBACK-STATS-NOT: hybrid planning failed
; FALLBACK-STATS: hybrid IR: 2
; FALLBACK-STATS: stateful regions: 1
; FALLBACK-IR-LABEL: define i64 @stateful_seed_fallback
; FALLBACK-IR: {{%[^ ]+}} = freeze i64 %x, !a2mba.generated
; FALLBACK-IR-NEXT: {{%[^ ]+}} = freeze i64 %y, !a2mba.generated
; FALLBACK-IR-NOT: freeze i64
; FALLBACK-IR: a2mba.region.state.initial
; FALLBACK-IR: a2mba.region.decode.exit.scale = mul i64
; FALLBACK-IR-NOT: %first = xor i64 %x, %x
; FALLBACK-IR-NOT: %second = sub i64 %first, %y
