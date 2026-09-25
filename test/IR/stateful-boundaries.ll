; REQUIRES: x86-registered-target
; RUN: env A2MBA_OPTIONS="mode=verified;level=medium;seed=909;functions=regex:^skip_region_;hybrid=ir;hybrid-region=stateful;hybrid-layers=none;probability=100;stats=true" %a2mba_opt -passes=a2mba,verify -S "%s" -o "%t.skip.ll" 2>&1 | %FileCheck "%s" --check-prefix=SKIP-STATS
; RUN: %FileCheck "%s" --input-file="%t.skip.ll" --check-prefix=SKIP-IR --implicit-check-not=a2mba.region.
; RUN: env A2MBA_OPTIONS="mode=verified;level=medium;seed=909;functions=regex:^skip_region_;hybrid=native;hybrid-region=stateful;hybrid-layers=none;probability=100;stats=true" %a2mba_opt -passes=a2mba,verify -disable-output "%s" 2>&1 | %FileCheck "%s" --check-prefix=SKIP-STATS
; RUN: env A2MBA_OPTIONS="mode=verified;level=medium;seed=909;functions=regex:^input_region_;hybrid=ir;hybrid-region=stateful;hybrid-layers=none;probability=100;stats=true" %a2mba_opt -passes=a2mba,verify -S "%s" -o "%t.input.ll" 2>&1 | %FileCheck "%s" --check-prefix=INPUT-STATS
; RUN: %FileCheck "%s" --input-file="%t.input.ll" --check-prefix=INPUT-IR
; RUN: %opt "-passes=default<O3>,verify" -disable-output "%t.input.ll"
; RUN: env A2MBA_OPTIONS="mode=verified;level=medium;seed=909;functions=regex:^input_region_;hybrid=native;hybrid-region=stateful;hybrid-layers=none;probability=100;stats=true" %a2mba_opt -passes=a2mba,verify -disable-output "%s" 2>&1 | %FileCheck "%s" --check-prefix=INPUT-STATS
; RUN: env A2MBA_OPTIONS="mode=verified;level=medium;seed=910;functions=regex:^bounded_region_;hybrid=ir;hybrid-region=stateful;hybrid-layers=none;probability=100;stats=true" %a2mba_opt -passes=a2mba,verify -S "%s" -o "%t.bounded.ll" 2>&1 | %FileCheck "%s" --check-prefix=BOUNDED-STATS
; RUN: %FileCheck "%s" --input-file="%t.bounded.ll" --check-prefix=BOUNDED-IR
; RUN: %opt "-passes=default<O3>,verify" -disable-output "%t.bounded.ll"
; RUN: env A2MBA_OPTIONS="mode=verified;level=medium;seed=910;functions=regex:^bounded_region_;hybrid=native;hybrid-region=stateful;hybrid-layers=none;probability=100;stats=true" %a2mba_opt -passes=a2mba,verify -disable-output "%s" 2>&1 | %FileCheck "%s" --check-prefix=BOUNDED-STATS

target triple = "x86_64-unknown-linux-gnu"

; SKIP-STATS: stateful regions: 0
; INPUT-STATS: stateful regions: 2
; BOUNDED-STATS: stateful regions: 5

define i64 @skip_region_fanout(i64 %x, i64 %y, i64 %z) {
entry:
  %first = add i64 %x, %y
  %left = xor i64 %first, %z
  %right = sub i64 %first, %z
  %result = or i64 %left, %right
  ret i64 %result
}

define i64 @skip_region_double_use(i64 %x, i64 %y) {
entry:
  %first = add i64 %x, %y
  %second = xor i64 %first, %first
  ret i64 %second
}

define i64 @skip_region_cross_block(i64 %x, i64 %y, i64 %z) {
entry:
  %first = add i64 %x, %y
  br label %next
next:
  %second = xor i64 %first, %z
  ret i64 %second
}

define i64 @input_region_phi_entry(i1 %condition, i64 %x, i64 %y, i64 %z) {
entry:
  br i1 %condition, label %left, label %right
left:
  br label %merge
right:
  br label %merge
merge:
  %value = phi i64 [ %x, %left ], [ %y, %right ]
  %first = add i64 %value, %y
  %second = xor i64 %first, %z
  ret i64 %second
}

; INPUT-IR-LABEL: define i64 @input_region_phi_entry
; INPUT-IR: a2mba.region.state.initial = add i64

define i64 @skip_region_phi_exit(i1 %condition, i64 %x, i64 %y, i64 %z) {
entry:
  br i1 %condition, label %left, label %right
left:
  %first = add i64 %x, %y
  %second = xor i64 %first, %z
  br label %merge
right:
  br label %merge
merge:
  %value = phi i64 [ %second, %left ], [ %x, %right ]
  ret i64 %value
}

define i64 @input_region_loaded_seed(ptr %source, i64 %x, i64 %y) {
entry:
  %value = load volatile i64, ptr %source
  %first = add i64 %value, %x
  %second = xor i64 %first, %y
  ret i64 %second
}

; INPUT-IR-LABEL: define i64 @input_region_loaded_seed
; INPUT-IR: load volatile i64
; INPUT-IR: a2mba.region.state.initial = add i64

define i64 @skip_region_mid_definition(i64 %x, i64 %y, i64 %z) {
entry:
  %first = add i64 %x, %y
  %narrow = trunc i64 %z to i32
  %late = zext i32 %narrow to i64
  %second = xor i64 %first, %late
  ret i64 %second
}

define i64 @skip_region_nsw(i64 %x, i64 %y, i64 %z) {
entry:
  %first = add nsw i64 %x, %y
  %second = xor i64 %first, %z
  ret i64 %second
}

; SKIP-IR-LABEL: define i64 @skip_region_nsw
; SKIP-IR: %first = add nsw i64 %x, %y

define i64 @skip_region_nuw(i64 %x, i64 %y, i64 %z) {
entry:
  %first = sub nuw i64 %x, %y
  %second = xor i64 %first, %z
  ret i64 %second
}

; SKIP-IR-LABEL: define i64 @skip_region_nuw
; SKIP-IR: %first = sub nuw i64 %x, %y

define i64 @skip_region_disjoint(i64 %x, i64 %y, i64 %z) {
entry:
  %first = or disjoint i64 %x, %y
  %second = xor i64 %first, %z
  ret i64 %second
}

; SKIP-IR-LABEL: define i64 @skip_region_disjoint
; SKIP-IR: %first = or disjoint i64 %x, %y

define i64 @bounded_region_flagged_exit(i64 %x, i64 %y, i64 %z) {
entry:
  %first = add i64 %x, %y
  %second = xor i64 %first, %z
  %flagged = add nsw i64 %second, %x
  ret i64 %flagged
}

; BOUNDED-IR-LABEL: define i64 @bounded_region_flagged_exit
; BOUNDED-IR: a2mba.region.decode.exit = sub i64
; BOUNDED-IR: %flagged = add nsw i64 %a2mba.region.decode.exit, %x

define i64 @bounded_region_fanout_exit(i64 %x, i64 %y, i64 %z) {
entry:
  %first = add i64 %x, %y
  %second = xor i64 %first, %z
  %left = mul i64 %second, 3
  %right = sub i64 %z, %second
  %result = or i64 %left, %right
  ret i64 %result
}

; BOUNDED-IR-LABEL: define i64 @bounded_region_fanout_exit
; BOUNDED-IR: a2mba.region.decode.exit = sub i64

define i64 @bounded_region_two_chains(i64 %x, i64 %y, i64 %z) {
entry:
  %left.first = add i64 %x, %y
  %left = xor i64 %left.first, %z
  %right.first = sub i64 %x, %z
  %right = and i64 %right.first, %y
  %result = mul i64 %left, %right
  ret i64 %result
}

; BOUNDED-IR-LABEL: define i64 @bounded_region_two_chains
; BOUNDED-IR-COUNT-2: a2mba.region.decode.exit{{[0-9]*}} = sub i64

define i64 @bounded_region_store(ptr %destination, i64 %x, i64 %y, i64 %z) {
entry:
  %first = add i64 %x, %y
  store volatile i64 %x, ptr %destination
  %second = xor i64 %first, %z
  ret i64 %second
}

; BOUNDED-IR-LABEL: define i64 @bounded_region_store
; BOUNDED-IR: a2mba.region.state.initial = add i64
; BOUNDED-IR: store volatile i64 %x, ptr %destination
; BOUNDED-IR: a2mba.region.decode.exit = sub i64
