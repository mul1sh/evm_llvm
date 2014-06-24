; RUN: llc < %s -O0 -verify-machineinstrs -fast-isel-abort -mtriple=powerpc64-unknown-linux-gnu -mcpu=pwr7 | FileCheck %s --check-prefix=ELF64
; RUN: llc < %s -O0 -verify-machineinstrs -mtriple=powerpc64-unknown-linux-gnu -mcpu=970 | FileCheck %s --check-prefix=PPC970

;; Tests for 970 don't use -fast-isel-abort because we intentionally punt
;; to SelectionDAG in some cases.

; Test sitofp

define void @sitofp_single_i64(i64 %a, float %b) nounwind ssp {
entry:
; ELF64: sitofp_single_i64
; PPC970: sitofp_single_i64
  %b.addr = alloca float, align 4
  %conv = sitofp i64 %a to float
; ELF64: std
; ELF64: lfd
; ELF64: fcfids
; PPC970: std
; PPC970: lfd
; PPC970: fcfid
; PPC970: frsp
  store float %conv, float* %b.addr, align 4
  ret void
}

define void @sitofp_single_i32(i32 %a, float %b) nounwind ssp {
entry:
; ELF64: sitofp_single_i32
; PPC970: sitofp_single_i32
  %b.addr = alloca float, align 4
  %conv = sitofp i32 %a to float
; ELF64: std
; ELF64: lfiwax
; ELF64: fcfids
; PPC970: std
; PPC970: lfd
; PPC970: fcfid
; PPC970: frsp
  store float %conv, float* %b.addr, align 4
  ret void
}

define void @sitofp_single_i16(i16 %a, float %b) nounwind ssp {
entry:
; ELF64: sitofp_single_i16
; PPC970: sitofp_single_i16
  %b.addr = alloca float, align 4
  %conv = sitofp i16 %a to float
; ELF64: extsh
; ELF64: std
; ELF64: lfd
; ELF64: fcfids
; PPC970: extsh
; PPC970: std
; PPC970: lfd
; PPC970: fcfid
; PPC970: frsp
  store float %conv, float* %b.addr, align 4
  ret void
}

define void @sitofp_single_i8(i8 %a) nounwind ssp {
entry:
; ELF64: sitofp_single_i8
; PPC970: sitofp_single_i8
  %b.addr = alloca float, align 4
  %conv = sitofp i8 %a to float
; ELF64: extsb
; ELF64: std
; ELF64: lfd
; ELF64: fcfids
; PPC970: extsb
; PPC970: std
; PPC970: lfd
; PPC970: fcfid
; PPC970: frsp
  store float %conv, float* %b.addr, align 4
  ret void
}

define void @sitofp_double_i32(i32 %a, double %b) nounwind ssp {
entry:
; ELF64: sitofp_double_i32
; PPC970: sitofp_double_i32
  %b.addr = alloca double, align 8
  %conv = sitofp i32 %a to double
; ELF64: std
; ELF64: lfiwax
; ELF64: fcfid
; PPC970: std
; PPC970: lfd
; PPC970: fcfid
  store double %conv, double* %b.addr, align 8
  ret void
}

define void @sitofp_double_i64(i64 %a, double %b) nounwind ssp {
entry:
; ELF64: sitofp_double_i64
; PPC970: sitofp_double_i64
  %b.addr = alloca double, align 8
  %conv = sitofp i64 %a to double
; ELF64: std
; ELF64: lfd
; ELF64: fcfid
; PPC970: std
; PPC970: lfd
; PPC970: fcfid
  store double %conv, double* %b.addr, align 8
  ret void
}

define void @sitofp_double_i16(i16 %a, double %b) nounwind ssp {
entry:
; ELF64: sitofp_double_i16
; PPC970: sitofp_double_i16
  %b.addr = alloca double, align 8
  %conv = sitofp i16 %a to double
; ELF64: extsh
; ELF64: std
; ELF64: lfd
; ELF64: fcfid
; PPC970: extsh
; PPC970: std
; PPC970: lfd
; PPC970: fcfid
  store double %conv, double* %b.addr, align 8
  ret void
}

define void @sitofp_double_i8(i8 %a, double %b) nounwind ssp {
entry:
; ELF64: sitofp_double_i8
; PPC970: sitofp_double_i8
  %b.addr = alloca double, align 8
  %conv = sitofp i8 %a to double
; ELF64: extsb
; ELF64: std
; ELF64: lfd
; ELF64: fcfid
; PPC970: extsb
; PPC970: std
; PPC970: lfd
; PPC970: fcfid
  store double %conv, double* %b.addr, align 8
  ret void
}

; Test uitofp

define void @uitofp_single_i64(i64 %a, float %b) nounwind ssp {
entry:
; ELF64: uitofp_single_i64
; PPC970: uitofp_single_i64
  %b.addr = alloca float, align 4
  %conv = uitofp i64 %a to float
; ELF64: std
; ELF64: lfd
; ELF64: fcfidus
; PPC970-NOT: fcfidus
  store float %conv, float* %b.addr, align 4
  ret void
}

define void @uitofp_single_i32(i32 %a, float %b) nounwind ssp {
entry:
; ELF64: uitofp_single_i32
; PPC970: uitofp_single_i32
  %b.addr = alloca float, align 4
  %conv = uitofp i32 %a to float
; ELF64: std
; ELF64: lfiwzx
; ELF64: fcfidus
; PPC970-NOT: lfiwzx
; PPC970-NOT: fcfidus
  store float %conv, float* %b.addr, align 4
  ret void
}

define void @uitofp_single_i16(i16 %a, float %b) nounwind ssp {
entry:
; ELF64: uitofp_single_i16
; PPC970: uitofp_single_i16
  %b.addr = alloca float, align 4
  %conv = uitofp i16 %a to float
; ELF64: rldicl {{[0-9]+}}, {{[0-9]+}}, 0, 48
; ELF64: std
; ELF64: lfd
; ELF64: fcfidus
; PPC970: rlwinm {{[0-9]+}}, {{[0-9]+}}, 0, 16, 31
; PPC970: std
; PPC970: lfd
; PPC970: fcfid
; PPC970: frsp
  store float %conv, float* %b.addr, align 4
  ret void
}

define void @uitofp_single_i8(i8 %a) nounwind ssp {
entry:
; ELF64: uitofp_single_i8
; PPC970: uitofp_single_i8
  %b.addr = alloca float, align 4
  %conv = uitofp i8 %a to float
; ELF64: rldicl {{[0-9]+}}, {{[0-9]+}}, 0, 56
; ELF64: std
; ELF64: lfd
; ELF64: fcfidus
; PPC970: rlwinm {{[0-9]+}}, {{[0-9]+}}, 0, 24, 31
; PPC970: std
; PPC970: lfd
; PPC970: fcfid
; PPC970: frsp
  store float %conv, float* %b.addr, align 4
  ret void
}

define void @uitofp_double_i64(i64 %a, double %b) nounwind ssp {
entry:
; ELF64: uitofp_double_i64
; PPC970: uitofp_double_i64
  %b.addr = alloca double, align 8
  %conv = uitofp i64 %a to double
; ELF64: std
; ELF64: lfd
; ELF64: fcfidu
; PPC970-NOT: fcfidu
  store double %conv, double* %b.addr, align 8
  ret void
}

define void @uitofp_double_i32(i32 %a, double %b) nounwind ssp {
entry:
; ELF64: uitofp_double_i32
; PPC970: uitofp_double_i32
  %b.addr = alloca double, align 8
  %conv = uitofp i32 %a to double
; ELF64: std
; ELF64: lfiwzx
; ELF64: fcfidu
; PPC970-NOT: lfiwzx
; PPC970-NOT: fcfidu
  store double %conv, double* %b.addr, align 8
  ret void
}

define void @uitofp_double_i16(i16 %a, double %b) nounwind ssp {
entry:
; ELF64: uitofp_double_i16
; PPC970: uitofp_double_i16
  %b.addr = alloca double, align 8
  %conv = uitofp i16 %a to double
; ELF64: rldicl {{[0-9]+}}, {{[0-9]+}}, 0, 48
; ELF64: std
; ELF64: lfd
; ELF64: fcfidu
; PPC970: rlwinm {{[0-9]+}}, {{[0-9]+}}, 0, 16, 31
; PPC970: std
; PPC970: lfd
; PPC970: fcfid
  store double %conv, double* %b.addr, align 8
  ret void
}

define void @uitofp_double_i8(i8 %a, double %b) nounwind ssp {
entry:
; ELF64: uitofp_double_i8
; PPC970: uitofp_double_i8
  %b.addr = alloca double, align 8
  %conv = uitofp i8 %a to double
; ELF64: rldicl {{[0-9]+}}, {{[0-9]+}}, 0, 56
; ELF64: std
; ELF64: lfd
; ELF64: fcfidu
; PPC970: rlwinm {{[0-9]+}}, {{[0-9]+}}, 0, 24, 31
; PPC970: std
; PPC970: lfd
; PPC970: fcfid
  store double %conv, double* %b.addr, align 8
  ret void
}

; Test fptosi

define void @fptosi_float_i32(float %a) nounwind ssp {
entry:
; ELF64: fptosi_float_i32
; PPC970: fptosi_float_i32
  %b.addr = alloca i32, align 4
  %conv = fptosi float %a to i32
; ELF64: fctiwz
; ELF64: stfd
; ELF64: lwa
; PPC970: fctiwz
; PPC970: stfd
; PPC970: lwa
  store i32 %conv, i32* %b.addr, align 4
  ret void
}

define void @fptosi_float_i64(float %a) nounwind ssp {
entry:
; ELF64: fptosi_float_i64
; PPC970: fptosi_float_i64
  %b.addr = alloca i64, align 4
  %conv = fptosi float %a to i64
; ELF64: fctidz
; ELF64: stfd
; ELF64: ld
; PPC970: fctidz
; PPC970: stfd
; PPC970: ld
  store i64 %conv, i64* %b.addr, align 4
  ret void
}

define void @fptosi_double_i32(double %a) nounwind ssp {
entry:
; ELF64: fptosi_double_i32
; PPC970: fptosi_double_i32
  %b.addr = alloca i32, align 8
  %conv = fptosi double %a to i32
; ELF64: fctiwz
; ELF64: stfd
; ELF64: lwa
; PPC970: fctiwz
; PPC970: stfd
; PPC970: lwa
  store i32 %conv, i32* %b.addr, align 8
  ret void
}

define void @fptosi_double_i64(double %a) nounwind ssp {
entry:
; ELF64: fptosi_double_i64
; PPC970: fptosi_double_i64
  %b.addr = alloca i64, align 8
  %conv = fptosi double %a to i64
; ELF64: fctidz
; ELF64: stfd
; ELF64: ld
; PPC970: fctidz
; PPC970: stfd
; PPC970: ld
  store i64 %conv, i64* %b.addr, align 8
  ret void
}

; Test fptoui

define void @fptoui_float_i32(float %a) nounwind ssp {
entry:
; ELF64: fptoui_float_i32
; PPC970: fptoui_float_i32
  %b.addr = alloca i32, align 4
  %conv = fptoui float %a to i32
; ELF64: fctiwuz
; ELF64: stfd
; ELF64: lwz
; PPC970: fctidz
; PPC970: stfd
; PPC970: lwz
  store i32 %conv, i32* %b.addr, align 4
  ret void
}

define void @fptoui_float_i64(float %a) nounwind ssp {
entry:
; ELF64: fptoui_float_i64
; PPC970: fptoui_float_i64
  %b.addr = alloca i64, align 4
  %conv = fptoui float %a to i64
; ELF64: fctiduz
; ELF64: stfd
; ELF64: ld
; PPC970-NOT: fctiduz
  store i64 %conv, i64* %b.addr, align 4
  ret void
}

define void @fptoui_double_i32(double %a) nounwind ssp {
entry:
; ELF64: fptoui_double_i32
; PPC970: fptoui_double_i32
  %b.addr = alloca i32, align 8
  %conv = fptoui double %a to i32
; ELF64: fctiwuz
; ELF64: stfd
; ELF64: lwz
; PPC970: fctidz
; PPC970: stfd
; PPC970: lwz
  store i32 %conv, i32* %b.addr, align 8
  ret void
}

define void @fptoui_double_i64(double %a) nounwind ssp {
entry:
; ELF64: fptoui_double_i64
; PPC970: fptoui_double_i64
  %b.addr = alloca i64, align 8
  %conv = fptoui double %a to i64
; ELF64: fctiduz
; ELF64: stfd
; ELF64: ld
; PPC970-NOT: fctiduz
  store i64 %conv, i64* %b.addr, align 8
  ret void
}
