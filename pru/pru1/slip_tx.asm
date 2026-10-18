; SPDX-License-Identifier: MIT
;
; PRU1 signal generation. transmit_xy is called from the C loop.
; It SLIP-frames X and Y, then shifts each byte out as UART 8N1 on R30 bit 5.
; The bit wait is 2170 cycles: LDI + NOP + a 1084-iteration 2-cycle loop.
;

	.sect	".text"
	.global	transmit_xy

END	.set	0xC0
ESC	.set	0xDB
ESC_END	.set	0xDC
ESC_ESC	.set	0xDD

; r14 = x, r15 = y. Return with JMP r3.w2.
transmit_xy:
	SUB	r2, r2, 32
	SBBO	&r28, r2, 24, 8
	ADD	r17, r2, 0

	LDI	r18, END
	SBBO	&r18.b0, r17, 0, 1
	ADD	r17, r17, 1

	LDI	r16, 0
	LDI	r1, 0xFF
	AND	r21, r14, r14
	LDI	r19, 4
x_bytes:
	AND	r18, r21, r1
	XOR	r16, r16, r18
	JAL	r28.w0, slip_push
	LSR	r21, r21, 8
	SUB	r19, r19, 1
	QBNE	x_bytes, r19, 0

	AND	r21, r15, r15
	LDI	r19, 4
y_bytes:
	AND	r18, r21, r1
	XOR	r16, r16, r18
	JAL	r28.w0, slip_push
	LSR	r21, r21, 8
	SUB	r19, r19, 1
	QBNE	y_bytes, r19, 0

	AND	r18, r16, r16
	JAL	r28.w0, slip_push

	LDI	r18, END
	SBBO	&r18.b0, r17, 0, 1
	ADD	r17, r17, 1

	ADD	r19, r2, 0
send_loop:
	QBEQ	send_done, r19, r17
	LBBO	&r18.b0, r19, 0, 1
	ADD	r19, r19, 1
	JAL	r29.w0, uart_byte
	JMP	send_loop

send_done:
	LBBO	&r28, r2, 24, 8
	ADD	r2, r2, 32
	JMP	r3.w2

; Append r18, escaping END and ESC. Advances r17. Returns via r28.w0.
slip_push:
	QBEQ	push_end, r18, END
	QBEQ	push_esc, r18, ESC
	SBBO	&r18.b0, r17, 0, 1
	ADD	r17, r17, 1
	JMP	r28.w0
push_end:
	LDI	r18, ESC
	SBBO	&r18.b0, r17, 0, 1
	ADD	r17, r17, 1
	LDI	r18, ESC_END
	SBBO	&r18.b0, r17, 0, 1
	ADD	r17, r17, 1
	JMP	r28.w0
push_esc:
	LDI	r18, ESC
	SBBO	&r18.b0, r17, 0, 1
	ADD	r17, r17, 1
	LDI	r18, ESC_ESC
	SBBO	&r18.b0, r17, 0, 1
	ADD	r17, r17, 1
	JMP	r28.w0

; Shift r18 out, LSB first, 8N1, idle high on R30.t5. Returns via r29.w0.
uart_byte:
	CLR	r30, r30.t5
	LDI	r0, 1084
	NOP
start_wait:
	SUB	r0, r0, 1
	QBNE	start_wait, r0, 0

	LDI	r20, 8
bit_loop:
	AND	r1, r18, 1
	QBEQ	bit_low, r1, 0
	SET	r30, r30.t5
	JMP	bit_wait
bit_low:
	CLR	r30, r30.t5
	NOP
bit_wait:
	LSR	r18, r18, 1
	LDI	r0, 1084
	NOP
bit_delay:
	SUB	r0, r0, 1
	QBNE	bit_delay, r0, 0
	SUB	r20, r20, 1
	QBNE	bit_loop, r20, 0

	SET	r30, r30.t5
	LDI	r0, 1084
	NOP
stop_wait:
	SUB	r0, r0, 1
	QBNE	stop_wait, r0, 0
	JMP	r29.w0
