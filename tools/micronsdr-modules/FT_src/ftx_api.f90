! ftx_api - the C interface of libftx.dll: the FT8 / FT4 decoders of WSJT-X, see ftx_api.h
! (WSPR: c/wspr_api.c, wsprd).
!
! The decoders keep state between calls (Fortran SAVE: hash tables of callsigns, the a7
! table of FT8, FFT plans): call from ONE thread only.
module ftx_api
  use, intrinsic :: iso_c_binding
  use ft8_decode, only: ft8_decoder
  use ft4_decode, only: ft4_decoder
  use ft8_a7, only: jseq
  implicit none
  private

  abstract interface
     subroutine ftx_callback(user, snr, dt, freq, msg, nap, qual) bind(C)
       import :: c_ptr, c_int, c_float, c_char
       type(c_ptr), value :: user
       integer(c_int), value :: snr
       real(c_float), value :: dt
       real(c_float), value :: freq
       character(kind=c_char), dimension(*) :: msg
       integer(c_int), value :: nap
       real(c_float), value :: qual
     end subroutine ftx_callback
  end interface

  type(c_funptr), save :: g_cb
  type(c_ptr), save :: g_user
  integer, save :: g_count = 0
  type(ft8_decoder), save :: dec8
  type(ft4_decoder), save :: dec4
  integer(c_short), save :: id2(180000)       ! 15 s at 12000 S/s (FT4: 72576 used)

  public :: ftx_decode, ftx_version, ftx_encode

contains

  subroutine emit(snr, dt, freq, decoded, nap, qual)
    integer, intent(in) :: snr, nap
    real, intent(in) :: dt, freq, qual
    character(len=37), intent(in) :: decoded
    character(kind=c_char) :: buf(38)
    procedure(ftx_callback), pointer :: f
    integer :: i
    g_count = g_count + 1
    if(.not. c_associated(g_cb)) return
    do i = 1, 37
       buf(i) = decoded(i:i)
    end do
    buf(38) = c_null_char
    call c_f_procpointer(g_cb, f)
    call f(g_user, int(snr, c_int), real(dt, c_float), real(freq, c_float), buf,     &
         int(nap, c_int), real(qual, c_float))
  end subroutine emit

  subroutine cb8(this, sync, snr, dt, freq, decoded, nap, qual)
    class(ft8_decoder), intent(inout) :: this
    real, intent(in) :: sync
    integer, intent(in) :: snr
    real, intent(in) :: dt
    real, intent(in) :: freq
    character(len=37), intent(in) :: decoded
    integer, intent(in) :: nap
    real, intent(in) :: qual
    call emit(snr, dt, freq, decoded, nap, qual)
  end subroutine cb8

  subroutine cb4(this, sync, snr, dt, freq, decoded, nap, qual)
    class(ft4_decoder), intent(inout) :: this
    real, intent(in) :: sync
    integer, intent(in) :: snr
    real, intent(in) :: dt
    real, intent(in) :: freq
    character(len=37), intent(in) :: decoded
    integer, intent(in) :: nap
    real, intent(in) :: qual
    call emit(snr, dt, freq, decoded, nap, qual)
  end subroutine cb4

  ! a C string (up to n characters) into a Fortran string, blank filled
  subroutine cstr(src, n, dst)
    character(kind=c_char), dimension(*), intent(in) :: src
    integer, intent(in) :: n
    character(len=*), intent(out) :: dst
    integer :: i
    dst = ' '
    do i = 1, min(n, len(dst))
       if(src(i) == c_null_char) exit
       dst(i:i) = src(i)
    end do
  end subroutine cstr

  function ftx_version() bind(C, name='ftx_version') result(v)
    integer(c_int) :: v
    v = 121
  end function ftx_version

  ! mode 8 = FT8, 4 = FT4; audio: 12000 S/s, the T/R period from its start
  ! (FT8 15 s, FT4 7.5 s; shorter: zeros after it). Returns the number of decodes.
  function ftx_decode(mode, audio, nsamples, nfa, nfb, nfqso, ndepth, ap, nqso, nutc,        &
       nzhsym, mycall, hiscall, hisgrid, cb, user) bind(C, name='ftx_decode') result(nd)
    integer(c_int), value :: mode, nsamples, nfa, nfb, nfqso, ndepth, ap, nqso, nutc, nzhsym
    integer(c_short), dimension(*), intent(in) :: audio
    character(kind=c_char), dimension(*), intent(in) :: mycall, hiscall, hisgrid
    type(c_funptr), value :: cb
    type(c_ptr), value :: user
    integer(c_int) :: nd

    character(len=12) :: my12, his12
    character(len=6) :: hisgrid6
    logical :: newdat, nagain, lapon, ltry_a8, lapcqonly
    logical(1) :: ldiskdat
    integer :: n, nmax, nz, ndep, nqs, nutc_, nfa_, nfb_, nfqso_
    real :: emedelay

    nd = -1
    select case(mode)
    case(8)
       nmax = 180000
    case(4)
       nmax = 72576
    case default
       return
    end select
    g_cb = cb
    g_user = user
    g_count = 0
    call cstr(mycall, 12, my12)
    call cstr(hiscall, 12, his12)
    call cstr(hisgrid, 6, hisgrid6)
    id2 = 0
    n = max(0, min(int(nsamples), nmax))
    if(n > 0) id2(1:n) = audio(1:n)
    newdat = .true.
    nagain = .false.
    lapon = ap /= 0
    lapcqonly = .false.
    ltry_a8 = .true.
    ldiskdat = .true.            ! no bail-out on the wall clock: the whole decode runs
    emedelay = 0.0
    ndep = max(1, min(3, int(ndepth)))
    nqs = nqso
    nutc_ = nutc
    nfa_ = nfa
    nfb_ = nfb
    nfqso_ = nfqso
    nz = nzhsym
    select case(mode)
    case(8)
       jseq = mod(nutc_/5, 2)       ! even / odd 15 s sequence (hhmmss), as WSJT-X at nzhsym 41
       ! nzhsym 0 (1.21): the whole period in one call - ft8_decode takes the audio in at
       ! nzhsym 41 / 47 only (at 50 alone it decodes its old buffer), so 47 first, then 50
       if(nz <= 0) then
          nz = 50
          if(ndep >= 2) then
             call dec8%decode(cb8, id2, nqs, nfqso_, nfqso_, newdat, nutc_, nfa_, nfb_, 47, &
                  ndep, emedelay, 0, nagain, lapon, ltry_a8, lapcqonly, 50, my12, his12,    &
                  hisgrid6, ldiskdat)
             newdat = .true.
          endif
       endif
       call dec8%decode(cb8, id2, nqs, nfqso_, nfqso_, newdat, nutc_, nfa_, nfb_, nz,       &
            ndep, emedelay, 0, nagain, lapon, ltry_a8, lapcqonly, 50, my12, his12,          &
            hisgrid6, ldiskdat)
    case(4)
       call dec4%decode(cb4, id2, nqs, nfqso_, nfa_, nfb_, ndep, lapcqonly, 0, my12, his12)
    end select
    nd = g_count
    g_cb = c_null_funptr
  end function ftx_decode

  ! the transmitter (1.20): a message -> the channel symbols (tones) of WSJT-X
  ! mode 8 = FT8: 79 tones 0..7 (genft8), 4 = FT4: 103 tones 0..3 (genft4); msgsent: the message
  ! as it will be decoded (37 characters + 0). Returns the number of tones, 0: a bad message
  ! (msgsent "*** bad message ***"), -1: a bad mode.
  function ftx_encode(mode, msg, itone, msgsent) bind(C, name='ftx_encode') result(nt)
    integer(c_int), value :: mode
    character(kind=c_char), dimension(*), intent(in) :: msg
    integer(c_int), dimension(*), intent(out) :: itone
    character(kind=c_char), dimension(*), intent(out) :: msgsent
    integer(c_int) :: nt

    character(len=37) :: m, sent
    integer :: i3, n3, i, it8(79), it4(103)
    integer*1 :: bits(77)

    nt = -1
    call cstr(msg, 37, m)
    m = adjustl(m)
    sent = ' '
    select case(mode)
    case(8)
       call genft8(m, i3, n3, sent, bits, it8)
       itone(1:79) = it8
       nt = 79
    case(4)
       call genft4(m, 0, sent, bits, it4)
       itone(1:103) = it4
       nt = 103
    case default
       return
    end select
    if(sent(1:3) == '***') nt = 0
    do i = 1, 37
       msgsent(i) = sent(i:i)
    end do
    msgsent(38) = c_null_char
  end function ftx_encode

end module ftx_api
