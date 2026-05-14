; B10 Scan Tool for 1990-1994 North American Subaru Legacy Models
; Copyright (c) 2004-2012 Vikash Ravi Goel
;
; This program is free software; you can redistribute it and/or modify
; it under the terms of version 2 of the GNU General Public License as
; published by the Free Software Foundation.
;
; This program is distributed in the hope that it will be useful,
; but WITHOUT ANY WARRANTY; without even the implied warranty of
; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
; General Public License for more details.
;
; You should have received a copy of the GNU General Public License
; along with this program; if not, write to the Free Software
; Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
;
; The full GNU General Public License, version 2, is included in
; this distribution in a file named gpl.txt.
;
; Version 0.07 (6 February 2012):
;  Fixed bug that sometimes caused garbage instructions to prevent
;   ROM ID reading.
;  Added functionality to read trouble codes.
;  Added functionality to clear memory.
;  Changed zeroed throttle position to throttle angle.
;  Removed vestigial Hitachi ECU temperature reporting code.
;  Changed boot code to work with USB or no-emulation CD booting.
;  Added communications code to test with QEMU and an ECU emulator.
;
; Version 0.06 (27 March 2006):
;  Changed name to "B10 Scan Tool."
;  Added home page URL and serial port interface to info screen.
;  Added port selection menu.
;  Added support for serial port connections.
;  Reversed 92-94 NA transmission ID reporting, which apparently was
;   backwards before.
;  Added feature to clear display if current sample is more than a second
;   old.
;  Added zeroed throttle position for 90-91 5MT and 91-94 turbo ECUs.
;  Fixed bug that caused APM detection to fail on some computers.
;  Ported from TASM to NASM.
;  Added a simple bootstrap loader to make scan tool self-booting and
;   eliminate dependency on DOS.
;  Released under GPL.
;
; Version 0.05 (11 May 2004) 
;  Added feature to spin down hard drives upon program startup.
;  Corrected small errors in water temperature readings for 90-91 5MT and
;   91-94 turbo ECUs.
;  Changed manifold pressure units from torr to psi (for boost) and inHg
;   (for vacuum). 
;
; Version 0.04 (30 April 2004) 
;  Fixed bug where 92-94 turbo injector pulse widths were displayed as being
;   16 times longer than they actually were. 
;
; Version 0.03 (30 April 2004) 
;  Fixed bug where ROM ID would sometimes not appear initially. 
;
; Version 0.02 (30 April 2004) 
;  Updated to add digital parameters and support non-turbo models as well as
;   91 turbo models.
;  Changed name to "BC/BF Scan Tool." 
;
; Version 0.01 (25 April 2004) 
;  Original version. Only supported 92-94 turbo models. Named "EJ22T Scan
;  Tool."

cpu 186                                ; pusha/popa, shl by cl, etc
org 100h

bootloader:                            ; start with a bootloader

call .self
.self:
pop ax
mov bx,cs
shl bx,4
add ax,bx                              ; check whether we were started at
cmp ax,07c00h-bootloader+.self         ; 07c00h absolute or not
jz .bootstrap                          ; if so, we need some bootstrapping
 jmp .skiploader                       ; skiploader's too far so we thunk
.bootstrap:

jmp 07b0h:.locateself                  ; BIOS loads us to 07c00h absolute
.locateself:                           ; which is also 07b0:0100, nicely

cli
xor ax,ax
mov ss,ax
mov sp,7c00h
sti                                    ; set up a stack

cmp word [cs:magic],0b105h             ; if magic word is present, we
jnz .read                              ; were booted from USB or CD
cmp word [cs:magic+2],0ca11h           ; and needn't read from disk
jz .skiploader

.read:
 mov si,.readmsg
 mov cx,.readlen
 call .printstr

 xor ah,ah                             ; reset the drive
 int 13h                               ; (BIOS set DL to the drive number)

 push 0fffh
 pop es
 mov bx,100h                           ; we will load to 0fff:0100

 mov ax,0212h                          ; read first 18 sectors off disk
 mov cx,0001h                          ; starting at sector 1 track 0
 xor dh,dh                             ; side 0. this reads entire first
 int 13h                               ; 9-KiB track of a 1.4MB floppy

 jc .read                              ; retry if read failed

 push 0fffh                            ; then read the other side of the
 pop es                                ; track, loading 18-KiB total
 mov bx,2500h
 mov ax,0212h
 mov cx,0001h
 mov dh,1
 int 13h

 jc .read                              ; retry if read failed

 mov byte [es:0000h],0e8h              ; set up an intermediary thunk
 mov word [es:0001h],0100h-3           ; "call 100h"
 mov byte [es:0003h],0cbh              ; "retf"

 mov ax,0fffh
 mov es,ax
 mov ds,ax
 call word 0fffh:0000h                 ; call the thunk

 mov si,.waitmsg
 mov cx,.waitlen
 call .printstr

 xor ah,ah
 int 16h                               ; wait for a keystroke

 push 40h
 pop ds
 mov word [ds:72h],0
 jmp word 0ffffh:0000h                 ; cold boot

.skiploader:                           ; stepping stone for the short jump
 jmp scantool

.printstr:                             ; prints string at cs:[si], length cx
 pusha
 push cs
 pop ds
 mov ah,0eh                            ; teletype output
 mov bx,0007h                          ; gray-on-black
 .printloop:
  lodsb
  int 10h
  loop .printloop                      ; print the entire string
 popa
 ret

.readmsg:
 db 0dh,0ah,'Loading scantool from disk...',0dh,0ah
.readlen equ $-.readmsg

.waitmsg:
 db 0dh,0ah,'Press a key to reboot...'
.waitlen equ $-.waitmsg

times 512-($-bootloader)-2 db 0        ; pad bootloader out to 512 bytes
dw 0aa55h                              ; (including this magic number)

scantool:                              ; the actual scantool starts here

call spindown                          ; power down all the drives

cli
in al,21h                              ; record interrupt mask
mov [cs:oldintmask],al
or al,2                                ; mask keyboard interrupt
out 21h,al                             ; (leave others in case APM needs 'em)
sti

call showinfo

mov ax,1                               ; 40x25
int 10h

call chooseport

or al,al
jnz .gotport
 call goodbye
 ret
.gotport:

mov al,0feh
out 21h,al                             ; now mask the rest; APM should be done

xor ax,ax
mov es,ax
mov ax,word [es:8*4]
mov word [cs:oldhandler],ax
mov ax,word [es:8*4+2]
mov word [cs:oldhandler+2],ax          ; save original isr
cli
mov word [es:8*4],handler
mov word [es:8*4+2],cs                 ; install new isr
mov al,00110100b
out 43h,al                             ; program ctc channel 0 for mode 2
mov al,204
out 40h,al
xor al,al
out 40h,al                             ; frequency is about 3 times bitrate
sti

push 0c000h                            ; crude check for QEMU
pop es                                 ; look for the Plex86 video BIOS
cmp word [es:22h],'Pl'                 ; signature - location may change in
jnz .noqemu                            ; future versions!
cmp word [es:24h],'ex'
jnz .noqemu
cmp word [es:26h],'86'
jz .qemu
.noqemu:
 mov word [cs:byteready],0490h          ; turns JMP 00xx into
 mov word [cs:readbyte],0490h           ; NOP and ADD AL,00
 mov word [cs:sendbyte],0490h
.qemu:

call getid                             ; get ecu rom id
or al,al
jz quit

call handleid                          ; figure out how to talk to it

call nextparam                         ; get started
call initparam

mov word [cs:cursor_loc],(40*12+5)*2   ; preemptively write the ROM ID
call write_romid

mainloop:
 call checkread                        ; see if we got interesting info
 call checkstale                       ; or if our info is old
 in al,64h
 test al,1                             ; check if there's a scancode ready
 jz nokey
  in al,60h                            ; read the scancode
  test al,80h                          ; and see if it's a key-up
  jz nokey
   cmp al,39h+80h                      ; is it space?
   jne notspace
    call nextparam
   notspace:
   cmp al,0eh+80h                      ; is it backspace?
   jne notbksp
    call lastparam
   notbksp:
   cmp al,01h+80h                      ; is it escape?
   je quit
   cmp al,2eh+80h                      ; is it C?
   jne notc
    mov byte [cs:clearrequest],1
    jmp nokey                          ; a hack to allow memory clearing
   notc:
   call initparam
 nokey:
 jmp mainloop                          ; loop back
quit:

call clrscr

cli
mov al,00110110b                       ; restore standard timer settings
out 43h,al
xor al,al
out 40h,al
out 40h,al
xor ax,ax
mov es,ax
mov ax,word [cs:oldhandler]
mov word [es:8*4],ax
mov ax,word [cs:oldhandler+2]
mov word [es:8*4+2],ax                 ; restore original isr
sti

call goodbye

ret                                    ; back to OS or bootloader

goodbye:
 mov al,[cs:oldintmask]                ; restore interrupt mask
 out 21h,al                            
 mov ax,3                              ; back to 80x25 mode
 int 10h
 mov word [cs:cursor_loc],80*24*2
 mov bx,byline
 call putstring
 cmp byte [cs:apmpresent],0
 jnz .noapm
  mov ax,5304h                          
  xor bx,bx
  int 15h                              ; disconnect APM interface
 .noapm:
 ret

chooseport:                            ; let user choose a port, success in al

 pusha
 call listports

 mov word [cs:cursor_loc],(40*18)*2
 mov bx,.instructions
 call putstring

 mov cl,8                              ; current line (8-10 lp, 12-15 serial)

 mov ax,0b800h
 mov es,ax

 .menuloop:

  mov al,40*2
  mul cl
  mov bx,ax
  add bx,6

  cmp byte [es:bx+40],'a'              ; look for the 'at' on screen
  jz .selectionok
   add cl,[cs:.dir]                    ; skip ahead if it's not there
   and cl,0fh
   jmp .menuloop
  .selectionok:

  mov word [cs:cursor_loc],bx
  mov bx,.arrow
  call putstring

  .keyloop:
   in al,64h
   test al,1
   jz .keyloop
   in al,60h
   test al,80h
   jz .keyloop                         ; wait for a key-up

  sub word [cs:cursor_loc],6
  mov bx,.noarrow
  call putstring

  cmp al,80h+1                         ; see if it's escape
  jnz .notesc
   popa
   xor al,al
   ret
  .notesc:
  cmp al,80h+48h                       ; see if it's up
  jnz .notup
   mov byte [cs:.dir],-1
   dec cl
  .notup:
  cmp al,80h+50h                       ; see if it's down
  jnz .notdown
   mov byte [cs:.dir],1
   inc cl
  .notdown:
  cmp al,80h+1ch                       ; see if it's enter
  jnz .menuloop
   push 40h
   pop es
   cmp cl,10
   ja .serial
    sub cl,8
    xor bh,bh
    mov bl,cl
    shl bx,1
    add bx,8
    mov ax,word [es:bx]
    mov word [cs:baseport],ax
    inc ax
    mov word [cs:recvport],ax
    inc ax
    mov word [cs:xmitport],ax
    mov word [cs:portstr],.parallelstr
    popa
    mov al,1
    ret
   .serial:
    sub cl,12
    xor bh,bh
    mov bl,cl
    shl bx,1
    mov ax,word [es:bx]
    mov word [cs:baseport],ax
    add ax,4
    mov word [cs:xmitport],ax
    add ax,2
    mov word [cs:recvport],ax
    mov byte [cs:xmitxor],0            ; neutralize inversion of output
    mov byte [cs:recvshl],0            ; neutralize shifting of input
    mov word [cs:portstr],.serialstr
    popa
    mov al,1
    ret
 .arrow db 14,4,196,196,175,0
 .noarrow db 7,7,'   ',0
 .parallelstr db 'Parallel port at ',0
 .serialstr db 'Serial port at ',0
 .instructions:
  db 1,'   Up/Down to scroll, Enter to select   '
  db 16,13,'Escape to quit',0
 .dir db 0                             ; direction to go in the menu

listports:                             ; list serial and parallel ports
 pusha

 call clrscr
 mov word [cs:cursor_loc],(40*4)*2+24
 mov bx,.headstr
 call putstring
 
 mov word [cs:cursor_loc],(40*8)*2

 mov ax,40h
 mov ds,ax

 mov si,8
 mov cx,.parstr
 .portloop:
  mov bx,cx
  call putstring
  mov bx,.portnum
  call putstring
  mov ax,word [ds:si]
  or ax,ax
  jnz .portfound
   mov bx,.notfoundstr
   call putstring
   jmp .nextport
  .portfound:
   mov bx,.atstr
   call putstring
   call puthexword
  .nextport:  
  call cleartoend
  inc byte [cs:.portnum]
  add si,2
  cmp si,0eh
  looplimit equ $-2                    ; so we can use the loop code twice
  jne .portloop
  cmp cx,.serstr
  je .portslisted
   mov cx,.serstr                      ; run the loop again for serial ports
   mov byte [cs:.portnum],'0'
   mov byte [cs:looplimit],8
   add word [cs:cursor_loc],40*2
   xor si,si
   jmp .portloop
  .portslisted:
 mov byte [cs:.portnum],'0'
 popa
 ret
 .headstr db 1,'Select port...',0
 .parstr db 16,7,'Parallel port ',0
 .serstr db 16,7,' Serial port  ',0
 .portnum db '0 ',0
 .atstr db 'at ',0
 .notfoundstr db 8,7,'(not found)',0

spindown:                              ; spindown disk drives if possible
 mov ax,5300h
 xor bx,bx
 int 15h                               ; check for APM 1.0
 jc .abort
 cmp ah,86h
 jz .abort
 cmp bx,'PM'
 jz .abort
 mov ax,5301h                          
 xor bx,bx
 int 15h                               ; connect to APM interface
 jc .abort
 mov ax,5307h                          
 mov bx,02ffh
 mov cx,0001h
 int 15h                               ; standby secondary storage
 mov byte [cs:apmpresent],1            ; remember to disconnect from APM
 .abort:
 ret

showinfo:
 pusha
 mov ax,3                              ; set 80x25 text mode
 int 10h
 mov word [cs:cursor_loc],0
 mov bx,info
 call putstring
 mov ah,02h
 xor bh,bh
 mov dx,134bh                          ; set the cursor to (76,15)
 int 10h
 .keyloop1:
  in al,64h
  test al,1
  jz .keyloop1
  in al,60h
  test al,80h
  jnz .keyloop1                        ; wait for a key-down
 .keyloop2:
  in al,64h
  test al,1
  jz .keyloop2
  in al,60h
  test al,80h
  jz .keyloop2                         ; wait for a key-up
 .lcmp cmp al,26h+80h                  ; check if it's L
 jnz .done
  mov word [cs:.lcmp],003ch            ; change to cmp al,00h
  mov ax,3
  int 10h
  mov word [cs:cursor_loc],0
  mov bx,license
  call putstring
  mov ah,02h
  xor bh,bh
  mov dx,140eh                         ; (15,21)
  int 10h
  jmp .keyloop2
 .done:
 popa
 ret

handleid:                              ; interpret the rom id
 cmp word [cs:romid],3671h
 jnz .notturbo1
  mov word [cs:romstr],.early_turbo
  mov word [cs:params],early_turbo_params
 .notturbo1:
 cmp word [cs:romid],3672h
 jnz .notturbo2
  mov word [cs:romstr],.turbo
  mov word [cs:params],turbo_params
 .notturbo2:
 cmp word [cs:romid],3170h
 jnz .nothitachi
  mov word [cs:romstr],.hitachi
  mov word [cs:params],hitachi_params
 .nothitachi:
 cmp word [cs:romid],3270h
 jnz .notjecs1
  mov word [cs:romstr],.jecs1
  mov word [cs:params],jecs1_params
 .notjecs1:
 cmp word [cs:romid],3272h
 jnz .notjecs2
  mov word [cs:romstr],.jecs2
  mov word [cs:params],jecs2_params
 .notjecs2:
 cmp word [cs:romid],3273h
 jnz .notjecs3
  mov word [cs:romstr],.jecs3
  mov word [cs:params],jecs3_params
 .notjecs3:
 ret
 .early_turbo db '91 Turbo',0
 .turbo db '91-94 Turbo',0
 .hitachi db '90-91 5MT NA',0
 .jecs1 db '90-91 4EAT NA',0
 .jecs2 db '92 NA',0
 .jecs3 db '93-94 NA',0

getid:                                 ; try to get rom id; al=0 on failure
 pusha
 call clrscr
 mov word [cs:cursor_loc],40*2
 mov bx,word [cs:portstr]
 call putstring
 mov ax,word [cs:baseport]
 call puthexword
 mov al,'.'
 call putchar
 mov word [cs:cursor_loc],80*2
 mov bx,.readstr
 call putstring
 xor bx,bx                             ; offset in commands
 xor dl,dl                             ; how many bytes we've read
 .idloop:
  mov al,byte [cs:.commands+bx]        ; send the next byte
  call sendbyte
  inc bl
  and bl,7                             ; keep walking through commands
  call byteready
  jz .done
   call readbyte                       ; expect ff,fe,??,romid
   or dl,dl
   jz .expectff
   cmp dl,1
   je .expectfe
   cmp dl,2
   je .expectany
   cmp dl,3
   je .expectromid1
   cmp dl,4
   je .expectromid2
   mov byte [cs:romid+2],al            ; last byte of rom id
   jmp .gotid
  .expectff:
   cmp al,0ffh
   jne .notff
   mov dl,1
   jmp .done
   .notff:
    xor dl,dl
    jmp .done
  .expectfe:
   cmp al,0feh
   jne .expectff
   inc dl
   jmp .done
  .expectany:
   inc dl
   jmp .done
  .expectromid1:
   cmp al,0ffh
   je .expectff
   mov byte [cs:romid],al
   inc dl
   jmp .done
  .expectromid2: 
   mov byte [cs:romid+1],al
   inc dl
  .done:
  in al,64h
  test al,1                            ; check if there's a scancode ready
  jz .idloop
   in al,60h                           ; read the scancode
   cmp al,81h                          ; and see if it's escape-up
   jne .idloop
  popa
  xor al,al
  ret
  .gotid:
   mov al,12h                          ; send a 'reset' message
   call sendbyte
   xor al,al
   call sendbyte
   call sendbyte
   call sendbyte
   popa
   mov al,1
   ret
 .commands:
  db 78h,0ffh,0feh,00h                 ; read 0fffeh
  db 00h,0ffh,0ffh,00h                 ; request rom id
 .readstr db 'Reading ROM ID...',0

initparam:                             ; request parameter from ECU
 pusha
 mov byte [cs:clearrequest],0          ; yes, it's kind of a hack
 mov al,78h                            ; make the request
 call sendbyte
 mov al,byte [cs:si+1]
 call sendbyte
 mov al,byte [cs:si]
 call sendbyte
 mov al,00h
 call sendbyte
 call clrscr
 mov bx,word [cs:si+4+6]
 mov word [cs:cursor_loc],(40*11+2)*2  ; write the parameter name
 call putstring
 popa
 ret

nextparam:                             ; switch to the next parameter
 push ax
 push bx
 inc byte [cs:current_param]
 xor bh,bh
 mov bl,byte [cs:current_param]
 shl bx,1
 add bx,[cs:params]
 mov si,word [cs:bx]                   ; the pointer to the addr goes in si
 or si,si
 jnz .nowrap
  mov byte [cs:current_param],0
  mov si,[cs:params]
  mov si,word [cs:si]
 .nowrap:
 pop bx
 pop ax
 ret

lastparam:                             ; switch to the last parameter
 push bx
 mov bl,byte [cs:current_param]
 xor bh,bh
 shl bx,1
 add bx,[cs:params]
 sub byte [cs:current_param],1
 jnc .nowrap
  .wraploop:
   inc byte [cs:current_param]
   add bx,2
   cmp word [cs:bx],0
   jne .wraploop
 .nowrap:
 sub bx,2
 mov si,[cs:bx]
 pop bx
 ret

cleartoend:                            ; clear the rest of the current line
 pusha
 mov ax,word [cs:cursor_loc]
 shr ax,1
 mov bl,40
 div bl
 mov cl,40
 sub cl,ah
 xor ch,ch
 mov al,' '
 .clearloop:
  call putchar
  loop .clearloop
 popa

checkread:                             ; handle incoming bytes
 pusha
 call byteready
 jz .nobyte
  call readbyte
  cmp byte [cs:.index],0               ; see which byte to expect
  je .msb
  cmp byte [cs:.index],1
  je .lsb

  mov word [cs:cursor_loc],(40*12+5)*2

  xor dx,dx
  xor ah,ah
  call word [cs:si+2]                  ; call the display
  mov al,' '
  call putchar
  mov bx,si
  add bx,4
  call putstring                       ; print the units
  call cleartoend                      ; clear the rest of the line

  mov word [cs:hugetickcount],0        ; reset the ageing counter

  mov byte [cs:.index],0
  popa
  ret
  .lsb:
   cmp al,byte [cs:si]
   jne .msb                            ; if it's not the LSB, try MSB
    mov byte [cs:.index],2
    popa
    ret
  .msb:
   mov byte [cs:.index],0              ; if we fell from LSB we need this
   cmp al,byte [cs:si+1]               ; check if it's the MSB
   jne .nobyte
    mov byte [cs:.index],1
    popa
    ret
 .nobyte:
 popa
 ret
 .index db 0                           ; how many bytes we've seen

checkstale:                            ; check if our last sample is stale
 pusha
 cmp word [cs:hugetickcount],18        ; see if about a second has passed
 jle .notstale
  mov al,byte [cs:current_param]
  or al,al
  jz .notstale                         ; ROM ID never gets stale
  mov word [cs:cursor_loc],(40*12+5)*2
  call cleartoend                      ; clear the rest of the line
  mov word [cs:hugetickcount],0        ; don't want overflow
 .notstale:
 popa
 ret

sleep:                                 ; delay for approx number of ms in ax
 pusha
 mov word [cs:bigtickcount],0
 .sleeploop:
  cmp ax,word [cs:bigtickcount]
  ja .sleeploop
 popa
 ret

byteready:                             ; clear zero flag if a byte is ready
 jmp byteready_qemu                    ; this gets patched away
 push bx
 mov bl,byte [cs:in_start]
 cmp bl,[cs:in_end]
 pop bx
 ret

readbyte:                              ; read a byte into al
 jmp readbyte_qemu
 push bx
 .wait:
  call byteready
  jz .wait
 xor bh,bh
 mov bl,byte [cs:in_start]
 mov al,byte [cs:in_buf+bx]
 inc byte [cs:in_start]
 and byte [cs:in_start],0fh
 pop bx
 ret

sendbyte:                              ; transmit the byte in AL
 jmp sendbyte_qemu
 pusha
 .waitforbuf:                          ; make sure output buffer is clear
  cmp byte [cs:out_full],0
  jnz .waitforbuf
 mov byte [cs:out_buf],al
 mov byte [cs:out_full],1
 popa
 ret

byteready_qemu:                        ; test versions that just use
 push dx                               ; the 8250
 push ax
 mov dx,03fdh
 in al,dx 
 test al,1
 pop ax
 pop dx
 ret

readbyte_qemu:
 push dx
 .wait:
  call byteready
  jz .wait
 mov dx,03f8h
 in al,dx
 pop dx
 ret

sendbyte_qemu:
 push dx
 mov dx,03fdh
 push ax
 .wait:
  in al,dx   
  test al,20h
  jz .wait
 pop ax
 mov dx,03f8h
 out dx,al
 pop dx
 ret

; an example of a Tcl script that can be used with QEMU for testing
%if 0 ;-----------------------------------------------------------------------
proc connect {s h p} {
 set val(-1) 0
 fconfigure stdin -blocking 0 -buffering line
 fconfigure $s -translation binary -encoding binary -buffering none
 while 1 {
  if {[string length [set k [gets stdin]]]} {set val(-1) $k}
  fconfigure $s -blocking 0
  if {[string length [set b [read $s 1]]]} {
   binary scan $b c b;set b [expr $b & 0xff]
   if {($b==0x00)&&([info exists a])} {
    puts "ID"
    puts -nonewline $s "\xff\xfe\x00"
    puts -nonewline $s "\x71\x36\x00"
    read $s
    unset a
   }
   if {$b==0x78} {
    fconfigure $s -blocking 1
    binary scan [read $s 3] S a;set a [expr $a&0xffff]
    puts "READ [format %4.4x $a]"
   }
   if {$b==0xaa} {
    fconfigure $s -blocking 1
    binary scan [read $s 3] Sc a v;set a [expr $a&0xffff];set v [expr $v&0xff]
    puts "WRITE [format %4.4x $a] <= [format %2.2x $v]"
    unset a
   }
   if {$b==0x12} {
    puts "CLEAR"
    unset a
   }
  }
  if {[info exists a]} {
   set v $val(-1);catch {set v $val($a)}
   puts " [format %4.4x $a] = [format %2.2x $v]"
   puts -nonewline $s [binary format Sc $a $v]
   after 100
  }
 }
}
vwait [set s [socket -server connect 2212]]
%endif ;----------------------------------------------------------------------

putstring:                             ; print ASCIZ (+extras) string at [BX]
 pusha
 mov ax,0b800h
 mov es,ax
 mov di,word [cs:cursor_loc]
 mov ah,70h                            ; assume attribute is black-on-grey
 .charloop:
  mov al,byte [cs:bx]
  or al,al
  jz .done
  cmp al,10h
  jne .notskip                         ; value of 16 means to skip a run
   inc bx
   xor ch,ch
   mov cl,byte [cs:bx]
   shl cl,1
   add di,cx
   jmp .chardone
  .notskip:
  ja .notattr                          ; values under 16 are colors
   cmp byte [cs:bx+1],10h
   ja .nobg                            ; two sub-16 values mean fg,bg
    mov ah,byte [cs:bx+1]
    shl ah,4
    inc bx
   .nobg:
   and ah,11110000b
   or ah,al
   jmp .chardone
  .notattr:
   mov word [es:di],ax
   add di,2
  .chardone:
   inc bx
   mov al,byte [cs:bx]                 ; load the next char
  jmp .charloop
 .done:
 mov word [cs:cursor_loc],di
 popa
 ret

puthex:                                ; write AL's value in hex
 pusha
 mov cl,al                             ; save AL in CL
 xor bh,bh
 shr al,4
 mov bl,al
 mov al,[cs:nibbles+bx]
 call putchar
 mov al,cl
 and al,0fh
 mov bl,al
 mov al,[cs:nibbles+bx]
 call putchar
 popa
 ret

nibbles db '0123456789abcdef'

puthexword:                            ; write AX's value in hex
 ror ax,8
 call puthex
 ror ax,8
 call puthex
 ret

putdec:                                ; write DX:AX w/ BL decimal places
 pusha
 mov si,10h                            ; si will hold digit count}
 .zeroloop:                            ; but first, zero all digits}
  mov word [cs:.digits+si-2],0000h
  sub si,2
  jnz .zeroloop
 .divloop:
  mov di,ax                            ; save low word
  mov ax,dx                            ; ess high word first...
  xor dx,dx                            ; ...as a 32-bit quantity
  div word [cs:.ten]                   ; divide by 10
  mov cx,ax                            ; save quotient from high word
  mov ax,di
  div word [cs:.ten]                   ; now divide by low word
  mov byte [cs:.digits+si],dl          ; save the modulus as a digit
  inc si                               ; move to the next digit
  mov dx,cx                            ; retrieve high quotient
  or cx,ax                             ; check if CX:AX is zero
  jnz .divloop
 xor bh,bh                             ; make BX=BL+1
 cmp bl,-1                             ; if no decimal places,
 je .writeloop                         ; don't bother zero-padding
 inc bx
 cmp si,bx                             ; see if we need to zero-pad it
 jae .writeloop
  mov si,bx
 .writeloop:                           ; now write them all out
  mov al,byte [cs:.digits+si-1]
  add al,'0'
  call putchar
  cmp bx,si
  jnz .nodecimal
   mov al,'.'
   call putchar
  .nodecimal:
  dec si
  jnz .writeloop
 popa
 ret
 .ten dw 10
 .digits times 10h db 0                ; buffer for writing in decimal

xmitpoll:                              ; the send part of the timer handler
 cmp byte [cs:.skip],0
 jz .noskip
  dec byte [cs:.skip]
  ret
 .noskip:
 cmp byte [cs:out_full],0              ; check if there's a byte to send
 jne .notempty
  ret
 .notempty:
 mov byte [cs:.skip],2                 ; we're gonna skip 2 ticks after this
 cmp byte [cs:.bit],0                  ; should we send the start bit?
 je .startbit
 cmp byte [cs:.bit],9                  ; the parity bit?
 je .paritybit
 cmp byte [cs:.bit],10                 ; the stop bit?
 je .stopbit
  mov al,byte [cs:out_buf]             ; none of the above: a data bit
  and al,1
  add [cs:.parity],al                  ; track parity
  shr byte [cs:out_buf],1
  inc byte [cs:.bit]
  jmp .setline
 .startbit:
  xor al,al                            ; sending a '0'
  inc byte [cs:.bit]
  jmp .setline
 .paritybit:
  mov al,[cs:.parity]
  inc byte [cs:.bit]
  jmp .setline
 .stopbit:
  mov al,1
  mov byte [cs:.parity],0
  mov byte [cs:.bit],0
  mov byte [cs:out_full],0             ; clear the "output buffer full" flag
  mov byte [cs:.skip],6
  jmp .setline
 .skip db 0                            ; how many ticks to skip
 .bit db 0                             ; which bit we're on
 .parity db 0                          ; where we track parity
 .setline:                             ; set line to low bit of AL
  mov dx,[cs:xmitport]
  and al,1
  or al,2                              ; provide power for serial pull-up
  xor al,1                             
  xmitxor equ $-1                      ; pointer so we can neutralize the XOR
  out dx,al
  ret

recvpoll:                              ; the receive part
 mov dx,[cs:recvport]
 in al,dx
 mov cl,al
 shl cl,3
 recvshl equ $-1                       ; pointer so we can neutralize the SHL
 and cl,80h                            ; read line level into high bit of CL

 cmp byte [cs:.skip],0
 jz .noskip
  dec byte [cs:.skip]
  ret
 .noskip:
 cmp byte [cs:.bit],0                  ; are we waiting for the start bit?
 je .startbit
 cmp byte [cs:.bit],9                  ; are we expecting the parity bit?
 je .paritybit
  shr byte [cs:.buf],1                 ; otherwise, this is a data bit
  add byte [cs:.buf],cl                ; add it into the buffer
  inc byte [cs:.bit]
  add byte [cs:.parity],cl             ; track parity
  mov byte [cs:.skip],2                ; skip to the middle of the next bit
  ret
 .startbit:
  or cl,cl
  jz .bytestart                        ; check for start bit
   ret
  .bytestart:
  mov byte [cs:.skip],3                ; skip to middle of the data bit
  inc byte [cs:.bit]
  ret
 .paritybit:
  cmp byte [cs:.parity],cl             ; check the parity bit
  jne .parerr
   xor bh,bh
   mov bl,[cs:in_end]
   mov al,byte [cs:.buf]
   mov byte [cs:in_buf+bx],al          ; store the new byte in the fifo
   inc bl
   and bl,0fh
   mov [cs:in_end],bl
   cmp bl,byte [cs:in_start]           ; check for overrun
   jne .nowrap
    inc byte [cs:in_start]
    and byte [cs:in_start],0fh         ; destroy the oldest byte
   .nowrap:
  .parerr:
  mov byte [cs:.parity],0
  mov byte [cs:.buf],0
  mov byte [cs:.bit],0
  mov byte [cs:.skip],2                ; skip through the stop bit
  ret
 .skip db 0                            ; how many ticks to skip
 .bit db 0                             ; which bit we're on
 .buf db 0                             ; where we assemble bits
 .parity db 0                          ; where we track parity

handler:                               ; new irq 0 handler
 pusha
 call xmitpoll
 call recvpoll
 inc word [cs:smalltickcount]
 mov ax,word [cs:smalltickcount]       ; big ticks are 4 small ticks
 and ax,3
 pushf
 pop ax
 shr ax,6
 and ax,1
 add word [cs:bigtickcount],ax
 cmp word [cs:smalltickcount],321      ; see if we need to chain
 jb nochain
  inc word [cs:hugetickcount]
  inc word [cs:bigtickcount]
  mov word [cs:smalltickcount],0
  popa
  db 0eah                              ; this becomes a far jmp
  oldhandler dd 0                      ; when oldhandler is filled in
 nochain:
 mov al,20h
 out 20h,al                            ; send EOI to PIC
 popa
 iret

clrscr:                                ; clear the text-mode screen
 pusha
 mov ax,0b800h
 mov es,ax
 xor di,di
 mov ax,7000h
 mov cx,1000
 rep stosw
 mov word [cs:cursor_loc],0
 mov byte [es:1],77h
 popa
 ret

putchar:                               ; put character in AL onto screen
 pusha
 mov bx,0b800h
 mov es,bx
 mov di,word [cs:cursor_loc]
 mov byte [es:di],al
 add word [cs:cursor_loc],2
 popa
 ret

bitshow:                               ; show on/off. al=bit of cl, ah=invert
 pusha
 call putstring                        ; print string pointed to by bx
 test cl,al
 pushf
 pop bx
 shr bx,3+3
 and bx,1
 xor bl,ah
 shl bx,3
 add bx,.onstr
 call putstring
 popa
 ret
 .onstr db 'ON    ',0
 db 0                                  ; padding so they're 8 bytes apart
 .offstr db 'OFF   ',0

errbitshow:                            ; show ok/trouble. al=bit of cl
 pusha
 call putstring                        ; print string pointed to by bx
 test cl,al
 pushf
 pop bx
 shr bx,3+3
 and bx,1
 shl bx,3
 add bx,.errstr
 call putstring
 popa
 ret
 .errstr db 'CODE!  ',0
 .okstr db 'ok     ',0

; pointer to the list of parameters
params dw unknown_params

; null-terminated list of parameters
; unknown ECU
unknown_params dw romid_param, 0000h

; turbo ECU
turbo_params dw romid_param
             dw hitachi_vb, hitachi_vsp, hitachi_erev
             dw hitachi_tw, hitachi_advs, hitachi_qa
             dw hitachi_ldata, hitachi_thv, hitachi_tps, hitachi_tim_t
             dw hitachi_isc, hitachi_o2r, hitachi_alphar
             dw hitachi_rtrd, hitachi_wgc, hitachi_barop_t
             dw hitachi_manip, hitachi_fa0_t, hitachi_fa1_t
             dw hitachi_u1, hitachi_u2, hitachi_u3_t
             dw hitachi_m1_t, hitachi_m2_t, hitachi_m3_t
             dw hitachi_clear_t
             dw 0000h

; early turbo ECU -- uses alternate pulse width
early_turbo_params dw romid_param
             dw hitachi_vb, hitachi_vsp, hitachi_erev
             dw hitachi_tw, hitachi_advs, hitachi_qa
             dw hitachi_ldata, hitachi_thv, hitachi_tps, hitachi_tim_n
             dw hitachi_isc, hitachi_o2r, hitachi_alphar
             dw hitachi_rtrd, hitachi_wgc, hitachi_barop_t
             dw hitachi_manip, hitachi_fa0_t, hitachi_fa1_t
             dw hitachi_u1, hitachi_u2, hitachi_u3_t
             dw hitachi_m1_n, hitachi_m2_n, hitachi_m3_et
             dw hitachi_clear_et
             dw 0000h

; Hitachi non-turbo ECU
hitachi_params dw romid_param
               dw hitachi_vb, hitachi_vsp, hitachi_erev
               dw hitachi_tw, hitachi_advs, hitachi_qa
               dw hitachi_ldata, hitachi_thv, hitachi_tps, hitachi_tim_n
               dw hitachi_isc, hitachi_o2r, hitachi_alphar
               dw hitachi_rtrd, hitachi_barop_n, hitachi_fa0_n
               dw hitachi_fa1_n
               dw hitachi_u1, hitachi_u2, hitachi_u3_n
               dw hitachi_m1_n, hitachi_m2_n, hitachi_m3_n
               dw hitachi_clear_n
               dw 0000h

; 1990-1991 JECS non-turbo ECU
jecs1_params dw romid_param
             dw jecs1_vb, jecs1_vsp, jecs1_erev
             dw jecs1_tw, jecs1_advs, jecs1_qa
             dw jecs1_ldata, jecs1_thv
             dw jecs1_tim, jecs1_isc
             dw jecs1_o2r, jecs1_alphar, jecs1_rtrd
             dw jecs1_barop, jecs1_fa0, jecs1_fa1
             dw jecs1_u1, jecs1_u2, jecs1_u3
             dw jecs1_m1, jecs1_m2, jecs1_m3
             dw jecs1_clear
             dw 0000h

; 1992 JECS non-turbo ECU
jecs2_params dw romid_param
             dw jecs2_vb, jecs2_vsp, jecs2_erev
             dw jecs2_tw, jecs2_advs, jecs2_qa
             dw jecs2_ldata, jecs2_thv
             dw jecs2_tim, jecs2_isc
             dw jecs2_o2r, jecs2_alphar, jecs2_rtrd
             dw jecs2_barop, jecs2_fa0, jecs2_fa1
             dw jecs2_u1, jecs2_u2, jecs2_u3
             dw jecs2_m1, jecs2_m2, jecs2_m3
             dw jecs2_clear
             dw 0000h

; 1993-1994 JECS non-turbo ECU -- has no barometric pressure reading
jecs3_params dw romid_param
             dw jecs2_vb, jecs2_vsp, jecs2_erev
             dw jecs2_tw, jecs2_advs, jecs2_qa
             dw jecs2_ldata, jecs2_thv
             dw jecs2_tim, jecs2_isc
             dw jecs2_o2r, jecs2_alphar, jecs2_rtrd
             dw jecs2_fa0, jecs2_fa1
             dw jecs2_u1, jecs2_u2, jecs2_u3
             dw jecs2_m1, jecs2_m2, jecs2_m3
             dw jecs2_clear
             dw 0000h

current_param db -1                    ; index of the current parameter

; each parameter is defined by:
;  1-word address
;  1-word offset of output function (takes 8-bit input in DX:AX)
;  6-byte ASCIZ unit label
;  1-word address of ASCIZ parameter label
romid_param:
 dw 0fffeh
 dw write_romid
 db 0,0,0,0,0,0
 dw hitlbl_romid
 hitlbl_romid db 'ROM ID',0
 write_romid:
  mov al,byte [cs:romid]
  call puthex
  mov al,'.'
  call putchar
  mov al,byte [cs:romid+1]
  call puthex
  mov al,'.'
  call putchar
  mov al,byte [cs:romid+2]
  call puthex
  mov al,' '
  call putchar
  mov al,'('
  call putchar
  mov bx,[cs:romstr]
  call putstring
  mov al,')'
  call putchar
  ret
hitachi_vb:
 dw 1404h
 dw hitwrite_vb
 db 'volts',0
 dw hitlbl_vb
 hitlbl_vb db 'System Voltage',0
 hitwrite_vb:                          ; x*0.08
  shl ax,3
  mov bl,2
  call putdec
  ret
hitachi_vsp:
 dw 154bh
 dw hitwrite_vsp
 db 'mph',0,0,0
 dw hitlbl_vsp
 hitlbl_vsp db 'Vehicle Speed',0
 hitwrite_vsp:                         ; x/1.6
  mov dx,10
  mul dx
  shr ax,4
  mov bl,-1
  call putdec
  ret
hitachi_erev:
 dw 140bh
 dw hitwrite_erev
 db 'rpm',0,0,0
 dw hitlbl_erev
 hitlbl_erev db 'Engine Speed',0
 hitwrite_erev:                        ; x*25
  mov dx,25
  mul dx
  mov bl,-1
  call putdec
  ret
hitachi_tw:
 dw 1405h
 dw hitwrite_tw
 db 248,'F',0,0,0,0
 dw hitlbl_tw
 hitlbl_tw db 'Coolant Temperature',0
 hitwrite_tw:                          ; table lookup
  mov bx,ax
  mov al,byte [cs:.tw_lookup+bx]
  cmp bx,14                            ; the first 14 values need 255 added
  jae .noadd255
   add ax,255
  .noadd255:
  cmp bx,255-29                        ; the last 29 values are negative
  jbe .noinvert
   push ax
   mov al,'-'
   call putchar
   pop ax
  .noinvert:
  mov bl,-1
  call putdec
  ret
  .tw_lookup:
   ; the first 14 values need 255 added
   db 146,132,117,105, 90, 76, 63, 48, 36, 29, 22, 15,  9,  2
   db                                                         250,243
   db 237,234,230,226,223,219,216,212,208,205,203,199,198,194,192,189
   db 187,185,183,181,180,178,176,174,172,171,169,167,165,163,162,160
   db 158,156,154,153,153,151,149,147,147,145,144,144,142,140,140,138
   db 138,136,135,135,133,131,131,129,129,127,127,126,126,124,124,122
   db 122,120,120,118,118,117,117,115,115,113,113,111,111,109,109,108
   db 108,106,106,104,104,102,102,100,100, 99, 99, 97, 97, 97, 95, 95
   db  95, 93, 93, 91, 91, 90, 90, 88, 88, 86, 86, 84, 84, 84, 82, 82
   db  82, 81, 81, 79, 79, 79, 77, 77, 77, 75, 75, 73, 73, 73, 72, 72
   db  72, 70, 70, 68, 68, 68, 66, 66, 66, 64, 64, 63, 63, 61, 61, 59
   db  59, 57, 57, 55, 55, 55, 54, 54, 54, 52, 52, 50, 50, 50, 48, 48
   db  48, 46, 46, 45, 45, 43, 43, 41, 41, 39, 39, 37, 37, 37, 36, 36
   db  36, 34, 34, 32, 32, 30, 30, 28, 28, 27, 27, 25, 25, 23, 23, 21
   db  21, 19, 18, 18, 16, 14, 14, 12, 12, 10,  9,  9,  7,  5,  5,  3
   db   3,  1,  0
   ; the last 29 values need to be inverted
   db               2,  4,  6,  8,  9,  9, 11, 13, 15, 17, 18, 20, 22
   db  24, 27, 31, 33, 36, 40, 42, 45, 47, 47, 47, 47, 47, 47, 47, 47
hitachi_advs:
 dw 1489h
 dw hitwrite_advs
 db 248,'BTDC',0
 dw hitlbl_advs
 hitlbl_advs db 'Ignition Timing',0
 hitwrite_advs:                        ; x
  mov bl,-1
  call putdec
  ret
hitachi_qa:
 dw 1400h
 dw hitwrite_qa
 db 'volts',0
 dw hitlbl_qa
 hitlbl_qa db 'Airflow Signal',0
 hitwrite_qa:                          ; x/50
  shl ax,1
  mov bl,2
  call putdec
  ret
hitachi_ldata:
 dw 1414h
 dw hitwrite_ldata
 db 0,0,0,0,0,0
 dw hitlbl_ldata
 hitlbl_ldata db 'Load',0
 hitwrite_ldata:                       ; x
  mov bl,-1
  call putdec
  ret
hitachi_thv:
 dw 1487h
 dw hitwrite_qa
 db 'volts',0
 dw hitlbl_thv
 hitlbl_thv db 'Throttle Position Signal',0
hitachi_tps:
 dw 1453h
 dw hitwrite_isc
 db 248,0,0,0,0,0
 dw hitlbl_tps
 hitlbl_tps db 'Throttle Angle',0
hitachi_tim_n:
 dw 142ah
 dw hitwrite_tim_n
 db 'ms',0,0,0,0
 dw hitlbl_tim_n
 hitlbl_tim_n db 'Injector Pulse Width (approx)',0
 hitwrite_tim_n:                       ; x*2.048
  shl ax,11
  mov bl,3
  call putdec
  ret
hitachi_tim_t:
 dw 15f0h
 dw hitwrite_tim_t
 db 'ms',0,0,0,0
 dw hitlbl_tim_t
 hitlbl_tim_t db 'Injector Pulse Width',0
 hitwrite_tim_t:                       ; x*0.128
  shl ax,7
  mov bl,3
  call putdec
  ret
hitachi_isc:
 dw 158ah
 dw hitwrite_isc
 db '%',0,0,0,0,0
 dw hitlbl_isc
 hitlbl_isc db 'IAC valve duty cycle',0
 hitwrite_isc:                         ; x/2
  mov dx,10
  mul dx
  shr ax,1
  mov bl,1
  call putdec
  ret
hitachi_o2r:
 dw 1403h
 dw hitwrite_o2r
 db 'volts',0
 dw hitlbl_o2r
 hitlbl_o2r db 'Oxygen Sensor Signal',0
 hitwrite_o2r:                         ; x/100
  mov bl,2
  call putdec
  ret
hitachi_alphar:
 dw 1488h
 dw hitwrite_alphar
 db '%',0,0,0,0,0
 dw hitlbl_alphar
 hitlbl_alphar db 'Fuel Trim',0
 hitwrite_alphar:                      ; (x-128)/1.28
  sub al,80h
  jnc .positive_trim
   push ax
   mov al,'-'
   call putchar
   pop ax
   neg al
  .positive_trim:
  mov dx,10000
  mul dx
  mov bx,80h
  div bx
  xor dx,dx
  mov bl,2
  call putdec
  ret
hitachi_rtrd:
 dw 1530h
 dw hitwrite_rtrd
 db 248,0,0,0,0,0
 dw hitlbl_rtrd
 hitlbl_rtrd db 'Timing Correction',0
 hitwrite_rtrd:                        ; x
  or al,al
  jns .positive
   push ax
   mov al,'-'
   call putchar
   pop ax
   neg al
  .positive:
  mov bl,-1
  call putdec
  ret
hitachi_wgc:
 dw 144dh
 dw hitwrite_wgc
 db '%',0,0,0,0,0
 dw hitlbl_wgc
 hitlbl_wgc db 'Boost control duty cycle',0
 hitwrite_wgc:                         ; x/2.56
  mov dx,10000
  mul dx
  mov al,ah
  mov ah,dl
  mov dl,dh
  xor dh,dh
  mov bl,2
  call putdec
  ret
hitachi_barop_n:
 dw 140ah
 dw hitwrite_barop_n
 db 'torr',0,0
 dw hitlbl_barop_n
 hitlbl_barop_n db 'Barometric Pressure',0
 hitwrite_barop_n:                     ; 930-x*3.09
  mov dx,309
  mul dx
  push bx
  push cx
  mov cx,dx
  mov bx,ax
  mov dx,1
  mov ax,6b48h
  sub ax,bx
  sbb dx,cx
  pop cx
  pop bx
  mov bl,2
  call putdec
  ret
hitachi_barop_t:
 dw 1516h
 dw hitwrite_barop_t
 db 'torr',0,0
 dw hitlbl_barop_n
 hitwrite_barop_t:                     ; x*1.25+500
  mov dx,125
  mul dx
  add ax,50000
  adc dx,0
  mov bl,2
  call putdec
  ret
hitachi_manip:
 dw 00beh
 dw hitwrite_manip
 manip_units db 0,0,0,0,0,0
 dw hitlbl_manip
 hitlbl_manip db 'Boost/Vacuum',0
 hitwrite_manip:                       ; x/0.128-1060 (in torr)
  mov dx,100
  mul dx
  sub ax,13568
  mov dx,15107
  mov word [cs:manip_units],7370h     
  mov word [cs:manip_units+2],0069h    ; set the units to 'psi'
  jns .onboost
   push ax
   mov al,'-'
   call putchar
   pop ax
   neg ax
   mov dx,30757
   mov word [cs:manip_units],6e69h     ; set the units to 'inHg'
   mov word [cs:manip_units+2],6748h
  .onboost:
  mul dx
  mov bx,10000
  div bx
  xor dx,dx
  mov bl,3
  call putdec
  ret
hitachi_fa0_n:
 dw 15a8h
 dw hitwrite_fa0_n
 db 0,0,0,0,0,0
 dw hitlbl_fa0_n
 hitlbl_fa0_n db 'Input Switches',0
 hitwrite_fa0_n:                       ; [IG.AT.!UD.!RM.xx.!NT.PK.FC]
  push di
  mov cl,al
  mov di,word [cs:cursor_loc]
  mov bx,igstr
  mov ax,0080h
  call bitshow
  mov bx,atstr
  mov ax,0040h
  call bitshow
  add di,80
  mov word [cs:cursor_loc],di
  mov bx,udstr
  mov ax,0120h
  call bitshow
  mov bx,rmstr
  mov ax,0110h
  call bitshow
  add di,80
  mov word [cs:cursor_loc],di
  mov bx,ntstr
  mov ax,0104h
  call bitshow
  mov bx,pkstr
  mov ax,0002h
  call bitshow
  add di,80
  mov word [cs:cursor_loc],di
  mov bx,fcstr
  mov ax,0101h
  call bitshow
  pop di
  ret
hitachi_fa1_n:
 dw 15a9h
 dw hitwrite_fa1_n
 db 0,0,0,0,0,0
 dw hitlbl_fa1_n
 hitlbl_fa1_n db 'I/O Switches',0
 hitwrite_fa1_n:                       ; [ID.AC.AR.RF.FP.CN.KS.xx]
  push di
  mov cl,al
  mov di,word [cs:cursor_loc]
  mov bx,idstr
  mov ax,0080h
  call bitshow
  mov bx,acstr
  mov ax,0040h
  call bitshow
  add di,80
  mov word [cs:cursor_loc],di
  mov bx,arstr
  mov ax,0020h
  call bitshow
  mov bx,rfstr
  mov ax,0010h
  call bitshow
  add di,80
  mov word [cs:cursor_loc],di
  mov bx,fpstr
  mov ax,0008h
  call bitshow
  mov bx,cnstr
  mov ax,0004h
  call bitshow
  add di,80
  mov word [cs:cursor_loc],di
  mov bx,ksstr
  mov ax,0002h
  call bitshow
  pop di
  ret
hitachi_fa0_t:
 dw 15a8h
 dw hitwrite_fa0_t
 db 0,0,0,0,0,0
 dw hitlbl_fa0_n
 hitwrite_fa0_t:                       ; [IG.AT.!UD.!RM.xx.!NT.!PK.FC]
  push di
  mov cl,al
  mov di,word [cs:cursor_loc]
  mov bx,igstr
  mov ax,0080h
  call bitshow
  mov bx,atstr
  mov ax,0040h
  call bitshow
  add di,80
  mov word [cs:cursor_loc],di
  mov bx,udstr
  mov ax,0120h
  call bitshow
  mov bx,rmstr
  mov ax,0110h
  call bitshow
  add di,80
  mov word [cs:cursor_loc],di
  mov bx,ntstr
  mov ax,0104h
  call bitshow
  mov bx,pkstr
  mov ax,0102h
  call bitshow
  add di,80
  mov word [cs:cursor_loc],di
  mov bx,fcstr
  mov ax,0101h
  call bitshow
  pop di
  ret
hitachi_fa1_t:
 dw 15a9h
 dw hitwrite_fa1_t
 db 0,0,0,0,0,0
 dw hitlbl_fa1_n
 hitwrite_fa1_t:                       ; [ID.AC.AR.RF.FP.CN.KS.BR]
  push di
  mov cl,al
  mov di,word [cs:cursor_loc]
  mov bx,idstr
  mov ax,0080h
  call bitshow
  mov bx,acstr
  mov ax,0040h
  call bitshow
  add di,80
  mov word [cs:cursor_loc],di
  mov bx,arstr
  mov ax,0020h
  call bitshow
  mov bx,rfstr
  mov ax,0010h
  call bitshow
  add di,80
  mov word [cs:cursor_loc],di
  mov bx,fpstr
  mov ax,0008h
  call bitshow
  mov bx,cnstr
  mov ax,0004h
  call bitshow
  add di,80
  mov word [cs:cursor_loc],di
  mov bx,ksstr
  mov ax,0002h
  call bitshow
  mov bx,brstr
  mov ax,0001h
  call bitshow
  pop di
  ret
hitachi_u1:
 dw 0047h
 dw hitwrite_u1
 db 0,0,0,0,0,0
 dw hitlbl_u1
 hitlbl_u1 db 'Active Trouble Codes (1/3)',0
 hitwrite_u1:
  push di
  mov cl,al
  sub word [cs:cursor_loc],4
  mov di,word [cs:cursor_loc]
  mov bx,err1str0
  mov al,01h
  call errbitshow
  mov bx,err1str1
  mov al,02h
  call errbitshow
  add di,80
  mov word [cs:cursor_loc],di
  mov bx,err1str2
  mov al,04h
  call errbitshow
  mov bx,err1str3
  mov al,08h
  call errbitshow
  add di,80
  mov word [cs:cursor_loc],di
  mov bx,err1str4
  mov al,10h
  call errbitshow
  mov bx,err1str5
  mov al,20h
  call errbitshow
  add di,80
  mov word [cs:cursor_loc],di
  mov bx,err1str6
  mov al,40h
  call errbitshow
  pop di
  ret
hitachi_u2:
 dw 0048h
 dw hitwrite_u2
 db 0,0,0,0,0,0
 dw hitlbl_u2
 hitlbl_u2 db 'Active Trouble Codes (2/3)',0
 hitwrite_u2:
  push di
  mov cl,al
  sub word [cs:cursor_loc],4
  mov di,word [cs:cursor_loc]
  mov bx,err2str0
  mov al,01h
  call errbitshow
  mov bx,err2str1
  mov al,02h
  call errbitshow
  add di,80
  mov word [cs:cursor_loc],di
  mov bx,err2str2
  mov al,04h
  call errbitshow
  mov bx,err2str3
  mov al,08h
  call errbitshow
  add di,80
  mov word [cs:cursor_loc],di
  mov bx,err2str4
  mov al,10h
  call errbitshow
  mov bx,err2str5
  mov al,20h
  call errbitshow
  add di,80
  mov word [cs:cursor_loc],di
  mov bx,err2str6
  mov al,40h
  call errbitshow
  mov bx,err2str7
  mov al,80h
  call errbitshow
  pop di
  ret
hitachi_u3_n:
 dw 0049h
 dw hitwrite_u3_n
 db 0,0,0,0,0,0
 dw hitlbl_u3_n
 hitlbl_u3_n db 'Active Trouble Codes (3/3)',0
 hitwrite_u3_n:
  push di
  mov cl,al
  sub word [cs:cursor_loc],4
  mov di,word [cs:cursor_loc]
  mov bx,err3str0
  mov al,01h
  call errbitshow
  mov bx,err3str1
  mov al,02h
  call errbitshow
  add di,80
  mov word [cs:cursor_loc],di
  mov bx,err3str4
  mov al,10h
  call errbitshow
  mov bx,err3str5
  mov al,20h
  call errbitshow
  add di,80
  mov word [cs:cursor_loc],di
  mov bx,err3str6
  mov al,40h
  call errbitshow
  mov bx,err3str7
  mov al,80h
  call errbitshow
  pop di
  ret
hitachi_u3_t:
 dw 0049h
 dw hitwrite_u3_t
 db 0,0,0,0,0,0
 dw hitlbl_u3_n
 hitwrite_u3_t:
  push di
  mov cl,al
  sub word [cs:cursor_loc],4
  mov di,word [cs:cursor_loc]
  mov bx,err3str0
  mov al,01h
  call errbitshow
  mov bx,err3str1
  mov al,02h
  call errbitshow
  add di,80
  mov word [cs:cursor_loc],di
  mov bx,err3str3
  mov al,08h
  call errbitshow
  mov bx,err3str4
  mov al,10h
  call errbitshow
  add di,80
  mov word [cs:cursor_loc],di
  mov bx,err3str5
  mov al,20h
  call errbitshow
  mov bx,err3str6
  mov al,40h
  call errbitshow
  add di,80
  mov word [cs:cursor_loc],di
  mov bx,err3str7
  mov al,80h
  call errbitshow
  pop di
  ret
hitachi_m1_n:
 dw 1604h
 dw hitwrite_u1
 db 0,0,0,0,0,0
 dw hitlbl_m1_n
 hitlbl_m1_n db 'Stored Trouble Codes (1/3)',0
hitachi_m2_n:
 dw 1605h
 dw hitwrite_u2
 db 0,0,0,0,0,0
 dw hitlbl_m2_n
 hitlbl_m2_n db 'Stored Trouble Codes (2/3)',0
hitachi_m3_n:
 dw 1606h
 dw hitwrite_u3_n
 db 0,0,0,0,0,0
 dw hitlbl_m3_n
 hitlbl_m3_n db 'Stored Trouble Codes (3/3)',0
hitachi_m3_et:
 dw 1606h
 dw hitwrite_u3_t
 db 0,0,0,0,0,0
 dw hitlbl_m3_n
hitachi_m1_t:
 dw 1664h
 dw hitwrite_u1
 db 0,0,0,0,0,0
 dw hitlbl_m1_n
hitachi_m2_t:
 dw 1665h
 dw hitwrite_u2
 db 0,0,0,0,0,0
 dw hitlbl_m2_n
hitachi_m3_t:
 dw 1666h
 dw hitwrite_u3_n
 db 0,0,0,0,0,0
 dw hitlbl_m3_n

jecs1_vb:
 dw 4780h
 dw hitwrite_vb
 db 'volts',0
 dw hitlbl_vb
jecs1_vsp:
 dw 4781h
 dw j1write_vsp
 db 'mph',0,0,0
 dw hitlbl_vsp
 j1write_vsp:
  mov dx,125
  mul dx
  mov bl,2
  call putdec
  ret
  jecs1_erev:
 dw 43bch
 dw hitwrite_erev
 db 'rpm',0,0,0
 dw hitlbl_erev
jecs1_tw:
 dw 4782h
 dw j1write_tw
 db 248,'F',0,0,0,0
 dw hitlbl_tw
 j1write_tw:
  mov dx,18
  mul dx
  cmp ax,580
  jnb .positive
   push ax
   mov al,'-'
   call putchar
   pop ax
   sub ax,580
   neg ax
   xor dx,dx
   jmp .subdone
  .positive:
   sub ax,580
   sbb dx,0
  .subdone:
  mov bl,1
  call putdec
  ret
jecs1_advs:
 dw 43c8h
 dw hitwrite_advs
 db 248,'BTDC',0
 dw hitlbl_advs
jecs1_qa:
 dw 43adh
 dw hitwrite_qa
 db 'volts',0
 dw hitlbl_qa
jecs1_ldata:
 dw 43aah
 dw hitwrite_ldata
 db 0,0,0,0,0,0
 dw hitlbl_ldata
jecs1_thv:
 dw 4784h
 dw hitwrite_qa
 db 'volts',0
 dw hitlbl_thv
jecs1_tim:
 dw 43abh
 dw j1write_tim
 db 'ms',0,0,0,0
 dw hitlbl_tim_n
 j1write_tim:
  shl ax,8
  mov bl,3
  call putdec
  ret
jecs1_isc:
 dw 43e3h
 dw j1write_isc
 db '%',0,0,0,0,0
 dw hitlbl_isc
 j1write_isc:
  mov dx,1000
  mul dx
  mov bx,255
  div bx
  xor dx,dx
  mov bl,1
  call putdec
  ret
jecs1_o2r:
 dw 43cfh
 dw hitwrite_o2r
 db 'volts',0
 dw hitlbl_o2r
jecs1_alphar:
 dw 43ceh
 dw hitwrite_alphar
 db '%',0,0,0,0,0
 dw hitlbl_alphar
jecs1_rtrd:
 dw 440dh
 dw j1write_rtrd
 db 248,0,0,0,0,0
 dw hitlbl_rtrd
 j1write_rtrd:
  sub al,80h
  jnc .positive
   push ax
   mov al,'-'
   call putchar
   pop ax
   neg al
  .positive:
  mov ah,100
  mul ah
  shr ax,2
  mov bl,2
  call putdec
  ret
jecs1_barop:
 dw 4787h
 dw j1write_barop
 db 'torr',0,0
 dw hitlbl_barop_n
 j1write_barop:
  shl ax,3
  mov bl,-1
  call putdec
  ret
jecs1_fa0:
 dw 4789h
 dw j1write_fa0
 db 0,0,0,0,0,0
 dw hitlbl_fa0_n
 j1write_fa0:                          ; [IG.AT.UD.RM.xx.NT.PK.FC]
  push di
  mov cl,al
  mov di,word [cs:cursor_loc]
  mov bx,igstr
  mov ax,0080h
  call bitshow
  mov bx,atstr
  mov ax,0040h
  call bitshow
  add di,80
  mov word [cs:cursor_loc],di
  mov bx,udstr
  mov ax,0020h
  call bitshow
  mov bx,rmstr
  mov ax,0010h
  call bitshow
  add di,80
  mov word [cs:cursor_loc],di
  mov bx,ntstr
  mov ax,0004h
  call bitshow
  mov bx,pkstr
  mov ax,0002h
  call bitshow
  add di,80
  mov word [cs:cursor_loc],di
  mov bx,fcstr
  mov ax,0101h
  call bitshow
  pop di
  ret
jecs1_fa1:
 dw 478ah
 dw hitwrite_fa1_n
 db 0,0,0,0,0,0
 dw hitlbl_fa1_n
jecs1_u1:
 dw 4407h
 dw hitwrite_u1
 db 0,0,0,0,0,0
 dw hitlbl_u1
jecs1_u2:
 dw 4406h
 dw hitwrite_u2
 db 0,0,0,0,0,0
 dw hitlbl_u2
jecs1_u3:
 dw 4405h
 dw j1write_u3
 db 0,0,0,0,0,0
 dw hitlbl_u3_n
 j1write_u3:
  push di
  mov cl,al
  sub word [cs:cursor_loc],4
  mov di,word [cs:cursor_loc]
  mov bx,err3str0
  mov al,01h
  call errbitshow
  mov bx,err3str1
  mov al,02h
  call errbitshow
  add di,80
  mov word [cs:cursor_loc],di
  mov bx,err3str5
  mov al,20h
  call errbitshow
  mov bx,err3str6
  mov al,40h
  call errbitshow
  add di,80
  mov word [cs:cursor_loc],di
  mov bx,err3str7
  mov al,80h
  call errbitshow
  pop di
  ret
jecs1_m1:
 dw 440ah
 dw hitwrite_u1
 db 0,0,0,0,0,0
 dw hitlbl_m1_n
jecs1_m2:
 dw 4409h
 dw hitwrite_u2
 db 0,0,0,0,0,0
 dw hitlbl_m2_n
jecs1_m3:
 dw 4408h
 dw j1write_u3
 db 0,0,0,0,0,0
 dw hitlbl_m3_n

jecs2_vb:
 dw 1335h
 dw hitwrite_vb
 db 'volts',0     
 dw hitlbl_vb
jecs2_vsp:
 dw 1336h
 dw j1write_vsp
 db 'mph',0,0,0
 dw hitlbl_vsp
jecs2_erev:
 dw 1338h
 dw hitwrite_erev
 db 'rpm',0,0,0
 dw hitlbl_erev
jecs2_tw:
 dw 1337h
 dw j1write_tw
 db 248,'F',0,0,0,0
 dw hitlbl_tw
jecs2_advs:
 dw 1323h
 dw hitwrite_advs
 db 248,'BTDC',0
 dw hitlbl_advs
jecs2_qa:
 dw 1307h
 dw hitwrite_qa
 db 'volts',0
 dw hitlbl_qa
jecs2_ldata:
 dw 1305h
 dw hitwrite_ldata
 db 0,0,0,0,0,0
 dw hitlbl_ldata
jecs2_thv:
 dw 1329h
 dw hitwrite_qa
 db 'volts',0
 dw hitlbl_thv
jecs2_tim:
 dw 1306h
 dw j1write_tim
 db 'ms',0,0,0,0
 dw hitlbl_tim_t
jecs2_isc:
 dw 1314h
 dw j1write_isc
 db '%',0,0,0,0,0
 dw hitlbl_isc
jecs2_o2r:
 dw 1310h
 dw hitwrite_qa
 db 'volts',0
 dw hitlbl_o2r
jecs2_alphar:
 dw 133eh
 dw hitwrite_alphar
 db '%',0,0,0,0,0
 dw hitlbl_alphar
jecs2_rtrd:
 dw 1328h
 dw j1write_rtrd
 db 248,0,0,0,0,0
 dw hitlbl_rtrd
jecs2_barop:
  dw 1340h
  dw j1write_barop
  db 'torr',0,0
  dw hitlbl_barop_n
jecs2_fa0:
 dw 1343h
 dw j2write_fa0
 db 0,0,0,0,0,0
 dw hitlbl_fa0_n
 j2write_fa0:                          ; [IG.AT.UD.RM.xx.!NT.PK.FC]
  push di
  mov cl,al
  mov di,word [cs:cursor_loc]
  mov bx,igstr
  mov ax,0080h
  call bitshow
  mov bx,atstr
  mov ax,0040h
  call bitshow
  add di,80
  mov word [cs:cursor_loc],di
  mov bx,udstr
  mov ax,0020h
  call bitshow
  mov bx,rmstr
  mov ax,0010h
  call bitshow
  add di,80
  mov word [cs:cursor_loc],di
  mov bx,ntstr
  mov ax,0104h
  call bitshow
  mov bx,pkstr
  mov ax,0002h
  call bitshow
  add di,80
  mov word [cs:cursor_loc],di
  mov bx,fcstr
  mov ax,0101h
  call bitshow
  pop di
  ret
jecs2_fa1:
 dw 1344h
 dw hitwrite_fa1_n
 db 0,0,0,0,0,0
 dw hitlbl_fa1_n
jecs2_u1:
 dw 1348h
 dw hitwrite_u1
 db 0,0,0,0,0,0
 dw hitlbl_u1
jecs2_u2:
 dw 1347h
 dw hitwrite_u2
 db 0,0,0,0,0,0
 dw hitlbl_u2
jecs2_u3:
 dw 1346h
 dw j1write_u3
 db 0,0,0,0,0,0
 dw hitlbl_u3_n
jecs2_m1:
 dw 134bh
 dw hitwrite_u1
 db 0,0,0,0,0,0
 dw hitlbl_m1_n
jecs2_m2:
 dw 134ah
 dw hitwrite_u2
 db 0,0,0,0,0,0
 dw hitlbl_m2_n
jecs2_m3:
 dw 1349h
 dw j1write_u3
 db 0,0,0,0,0,0
 dw hitlbl_m3_n
clear_write_zero:                      ; see if user wants to clear
 xor al,al                             ; clear with a zero value
clear_write_invert:                    ; or invert current value
 mov dl,al                             ; save clear value
 not dl
 mov di,[cs:cursor_loc]
 mov bx,.clearstr
 call putstring
 cmp byte [cs:clearrequest],0
 jnz .doclear
  ret
 .doclear:
  mov byte [cs:clearrequest],0
  mov [cs:cursor_loc],di
  mov bx,.gostr
  call putstring
  mov cl,4
  .clearloop:                          ; send the clear command 4 times
   mov al,0aah
   call sendbyte
   mov al,byte [cs:si+1]
   call sendbyte
   mov al,byte [cs:si]
   call sendbyte
   mov al,dl
   call sendbyte
   mov ax,180h
   call sleep
   loop .clearloop
  mov [cs:cursor_loc],di
  mov bx,.donestr
  call putstring
  mov ax,8000h
  call sleep
  mov [cs:cursor_loc],di
  call cleartoend
  call initparam
  ret
 .clearstr db 'Press C to clear...',0
 .gostr db 'Clearing ECU memory...',0
 .donestr db 'OK. Turn off ignition.',0
hitachi_clear_n:
 dw 1600h
 dw clear_write_invert
 db 0,0,0,0,0,0
 dw hitlbl_clear_n
 hitlbl_clear_n db 'Memory Clear',0
hitachi_clear_et:
 dw 15f0h
 dw clear_write_invert
 db 0,0,0,0,0,0
 dw hitlbl_clear_n
hitachi_clear_t:
 dw 1650h
 dw clear_write_zero
 db 0,0,0,0,0,0
 dw hitlbl_clear_n
jecs1_clear:
 dw 443fh
 dw clear_write_invert
 db 0,0,0,0,0,0
 dw hitlbl_clear_n
jecs2_clear:
 dw 135eh
 dw clear_write_zero
 db 0,0,0,0,0,0
 dw hitlbl_clear_n

igstr db 'Ignition    ',0
atstr db 'Auto Trans  ',0
udstr db 'Test Mode   ',0
rmstr db 'Read Mode   ',0
ntstr db 'Neutral     ',0
pkstr db 'Park        ',0
fcstr db 'California  ',0
idstr db 'Idle Switch ',0
acstr db 'A/C Switch  ',0
arstr db 'A/C Relay   ',0
rfstr db 'Rad Fan     ',0
fpstr db 'Fuel Pump   ',0
cnstr db 'Purge Valve ',0
ksstr db 'Pinging     ',0
brstr db 'Press Exch  ',0

err1str0 db '11/Crank',250,250,250,250,0
err1str1 db '12/Start Sw',250,0
err1str2 db '13/Cam',250,250,250,250,250,250,0
err1str3 db '14/Inj #1',250,250,250,0
err1str4 db '15/Inj #2',250,250,250,0
err1str5 db '16/Inj #3',250,250,250,0
err1str6 db '17/Inj #4',250,250,250,0

err2str0 db '21/Temp',250,250,250,250,250,0
err2str1 db '22/Knock',250,250,250,250,0
err2str2 db '23/MAF',250,250,250,250,250,250,0
err2str3 db '24/IAC',250,250,250,250,250,250,0
err2str4 db '31/TPS',250,250,250,250,250,250,0
err2str5 db '32/Oxygen',250,250,250,0
err2str6 db '33/VSS',250,250,250,250,250,250,0
err2str7 db '35/Purge',250,250,250,250,0

err3str0 db '41/FuelTrim',250,0
err3str1 db '42/Idle Sw',250,250,0
err3str3 db '44/WGC',250,250,250,250,250,250,0
err3str4 db '45/Baro',250,250,250,250,250,0
err3str5 db '49/WrongMAF',250,0
err3str6 db '51/NeutSw',250,250,250,0
err3str7 db '52/ParkSw',250,250,250,0 

%define VERSION '0.07'
%define YEAR '2012'

info:

db 8,0,' Ú',12,'ABOUT',8,'ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄ¿ Ú',12,'PARALLEL',8
db  'Ä',12,'PORT',8,'Ä',12,'INTERFACE',8,'Ä',12,'(RECOMMENDED)',8,'ÄÄ¿ '
db ' ³',9,'B10 Scan Tool v',VERSION,'',16,15,8,'³ ³ ',9,'Select pin',16,10
db  'Parallel port pin ',8,'³ '
db ' ³',15,'For 1990-1994 North American',16,6,8,'³ ³',16,5,15
db  '2 ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄ 13',16,8,8,'³ '
db ' ³',16,4,15,'Subaru Legacy Models',16,10,8,'³ ³     ',15
db  '3 ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄ 1',16,9,8,'³ '
db ' ³',15,' Vikash Goel ',10,'vikashgoel',40h,'gmail',2eh,'com ',8,'³ ³     '
db  15,'9 ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄ 25',16,8,8,'³ '
db ' ³',10,'http://www.surrealmirage.com/vrg3/',8,'³ Ã',12,'SERIAL',8,'Ä',12
db  'PORT',8,'Ä',12,'INTERFACE',8,'Ä',12,'(EXPERIMENTAL!)',8,'ÄÄ´ '
db ' ÀÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÙ ³   ',9,'Select',16,16
db  'DB9 serial    ',8,'³ '
db ' Ú',12,'KEYS',8,'ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄ¿ ³    ',9,'pin     ',15
db  'c',16,15,9,'pin ',8,'ÚÄÄÄÄÄÄÙ '
db ' ³',9,'Space',16,3,15,'Next parameter',16,12,8,'³ ³',16,9,15
db  'bÛ/ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄ 7  ',8,'³  ',11,'ÚÄ¿   '
db 8,' ³',9,'BkSp',16,4,15,'Previous parameter',16,8,8,'³ ³',16,5,15
db  '2 ÄÄÄÛ 2N3904',16,14,8,'³  ',11,'³P³   '
db 8,' ³',9,'Esc',16,5,15,'Quit',16,22,8,'³ ³',16,10,15
db  'Û\ÄÄÄÄÄÂÄÄÄÄÄÄÄÄÄÄ 1  ',8,'³  ',11,'³R³   '
db 8,' ³',9,'Enter',16,3,15,'Re-initialize parameter   ',8,'³ ³',16,11,15
db  'e     ÀÄÄ/\/\/ÄÄÄ 3  ',8,'³  ',11,'³E³   '
db 8,' ÀÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÙ ³',16,21,15,'10K',16,8,8,'³  ',11
db  '³S³   '
db 8,' Ú',12,'DIAGNOSTIC',8,'Ä',12,'PORT',8,'ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄ¿ ³     ',15
db  '9 ÄÄÄÄÄÄÂÄÄÄÄÄÄÄÄÄÄÄÄÄÄ 5  ',8,'³  ',11,'³S³   '
db 8,' ³',16,21,14,'ÚÄÂÄ¿ ÚÄÂÄ¿  ',8,'³ ³',16,12,15,'_³_',16,17,8,'³  ',11
db  '³ ³   '
db 8,' ³ ',15,'Yellow 9-pin Subaru ',14,'³1³2³Ü³3³4³  ',8,'³ ³',16,10,15
db  '³_\_/_ 1N4733A',16,8,8,'³  ',11,'³A³   '
db 8,' ³ ',15,'Select Monitor port ',14,'ÃÄÅÄÅÐÅÄÅÄ´  ',8,'³ ³',16,13,15
db  '³  ³',16,15,8,'³  ',11,'³ ³   '
db 8,' ³',16,21,14,'³5³6³7³8³9³  ',8,'³ ³',16,5,15
db  '3 ÄÄÄÄÄÄÁÄÄÄÄÄ/\/\/ÄÄÄÄ 4  ',8,'³  ',11,'³K³   '
db 8,' ³',16,21,14,'ÀÄÁÄÁÄÁÄÁÄÙ  ',8,'³ ³',16,20,15,'10K',16,9,8,'³  ',11
db  '³E³   '
db 8,' ÀÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÙ '
db  'ÀÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÙ  ',11,'³Y³   '
db 16,74,11,'ÀÄÙ   '
db 7
db ' Copyright (c) 2004-',YEAR,' Vikash Ravi Goel.',16,119
db 'This program is free software and comes with absolutely no warranty.'
db 16,12
db 'For more information press the "L" key.',0

license:

db 7,0
db 'B10 Scan Tool for 1990-1994 North American Subaru Legacy Models',16,17
db 'Copyright (c) 2004-',YEAR,' Vikash Ravi Goel',16,120
db 'This program is free software; you can redistribute it and/or modify'
db 16,12
db 'it under the terms of version 2 of the GNU General Public License as'
db 16,12
db 'published by the Free Software Foundation.',16,118
db 'This program is distributed in the hope that it will be useful,',16,17
db 'but WITHOUT ANY WARRANTY; without even the implied warranty of',16,18
db 'MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the',16,19
db 'GNU General Public License for more details.',16,116
db 'You should have received a copy of the GNU General Public License',16,15
db 'along with this program. If not, write to:',16,125
db 'The Free Software Foundation, Inc.',16,46
db '51 Franklin St.',16,65
db 'Boston, MA 02110-1301',16,59
db 'USA',16,80
db 16,70
db 'Press a key...',0

byline db 7,0,'B10 Scan Tool v',VERSION
       db ' by Vikash Ravi Goel <vikashgoel@gmail.com>',0

romid db 00h,00h,00h                   ; ECU's rom id
romstr dw unrecstr                     ; ECU designation in words
unrecstr db 'Unrecognized',0

out_buf db 0                           ; output register
out_full db 0

in_buf times 10h db 0                  ; input fifo
in_start db 0
in_end db 0

oldintmask db 0                        ; saved original interrupt mask

apmpresent db 0                        ; whether APM is present

smalltickcount dw 0                    ; high-freq ticks to maintain time
bigtickcount dw 0                      ; low-freq ticks to sleep
hugetickcount dw 0                     ; very-low-freq ticks to age samples

clearrequest db 0                      ; mem-clear requested flag

cursor_loc dw 0                        ; linear cursor position

portstr dw 0                           ; string representing port type
baseport dw 0                          ; base address of port
xmitport dw 0                          ; address of output register
xmitbit db 0                           ; bit index in output register
xmitmask db 0                          ; XOR mask to apply to output
recvport dw 0                          ; address of input register
recvbit db 0                           ; bit index in input register
recvmask db 0                          ; XOR mask to apply to input

magic dw 0b105h,0ca11h                 ; magic number to check presence

%ifdef SIZE                            ; if this is intended to be used
 times SIZE-($-$$) db 0                ; as a full floppy disk image, pad
%endif                                 ; it out to the given size
