	.psc02
	.segment "HEADER"
header:
	.byte 0
	.byte 0
	.byte 0
	jmp service
	; 6502 code.
	.byte 128+2
	.byte copyright - header
	; version
	.byte 1
	; title string
	.asciiz "Sugar Smash"
	; version string (not itself null-terminated)
	.byte "0.10",13
copyright:
	.byte 0
	.asciiz "(C)CRTC"
	.byte 0

service:
	cmp #4
	beq command
	cmp #3
	beq autoboot
	rts
command:
	phy
	phx

	ldx #0
loop:
	lda ($f2),y
	cmp cmd,x
	bne unknown
	iny
	inx
	cpx #5
	bne loop

	jmp main_code
unknown:
	plx
	ply
	lda #4
	rts

autoboot:
	jmp main_code

cmd:
	.byte "SUGAR"

main_code:
