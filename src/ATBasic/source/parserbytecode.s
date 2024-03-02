; Altirra BASIC - Parser bytecode program module
; Copyright (C) 2014 Avery Lee, All Rights Reserved.
;
; Copying and distribution of this file, with or without modification,
; are permitted in any medium without royalty provided the copyright
; notice and this notice are preserved.  This file is offered as-is,
; without any warranty.

?parser_program_start = *

parse_state_table:
		dta		a(pa_state0-1)
		dta		a(pa_state1-1)
		dta		a(pa_expr-1)
		dta		a(pa_aexpr.entry-1)
		dta		a(pa_sexpr-1)
		dta		a(pa_assign-1)
		dta		a(pa_avar-1)
		dta		a(pa_iocb-1)
		dta		a(pa_array-1)
		dta		a(pa_array2-1)
		dta		a(pa_comma-1)
		dta		a(pa_aexpr_comma-1)

		;statements
parse_state_table_statements:
		dta		<pa_state_rem-1
		dta		<pa_state_data-1
		dta		<pa_state_input-1
		dta		<pa_state_color-1
		dta		<pa_state_list-1
		dta		<pa_state_enter-1
		dta		<pa_state_let-1
		dta		<pa_state_if-1
		dta		<pa_state_for-1
		dta		<pa_state_next-1
		dta		<pa_state_goto-1
		dta		<pa_state_goto2-1
		dta		<pa_state_gosub-1
		dta		<pa_state_trap-1
		dta		<pa_state_bye-1
		dta		<pa_state_cont-1
		dta		<pa_state_com-1
		dta		<pa_state_close-1
		dta		<pa_state_clr-1
		dta		<pa_state_deg-1
		dta		<pa_state_dim-1
		dta		<pa_state_end-1
		dta		<pa_state_new-1
		dta		<pa_state_open-1
		dta		<pa_state_load-1
		dta		<pa_state_save-1
		dta		<pa_state_status-1
		dta		<pa_state_note-1
		dta		<pa_state_point-1
		dta		<pa_state_xio-1
		dta		<pa_state_on-1
		dta		<pa_state_poke-1
		dta		<pa_state_print-1
		dta		<pa_state_rad-1
		dta		<pa_state_read-1
		dta		<pa_state_restore-1
		dta		<pa_state_return-1
		dta		<pa_state_run-1
		dta		<pa_state_stop-1
		dta		<pa_state_pop-1
		dta		<pa_state_print-1
		dta		<pa_state_get-1
		dta		<pa_state_put-1
		dta		<pa_state_graphics-1
		dta		<pa_state_plot-1
		dta		<pa_state_position-1
		dta		<pa_state_dos-1
		dta		<pa_state_drawto-1
		dta		<pa_state_setcolor-1
		dta		<pa_state_locate-1
		dta		<pa_state_sound-1
		dta		<pa_state_lprint-1
		dta		<pa_state_csave-1
		dta		<pa_state_cload-1
		dta		<0
		dta		<0
		dta		<0
		dta		<0
		dta		<0
		dta		<0
		dta		<0
		dta		<0
		dta		<pa_state_dpoke-1
		dta		<0

		;$40
		dta		<0
		dta		<0
		dta		<0
		dta		<pa_state_bput-1
		dta		<pa_state_bget-1
		dta		<0
		dta		<pa_state_cp-1
		dta		<pa_state_erase-1
		dta		<pa_state_protect-1
		dta		<pa_state_unprotect-1
		dta		<pa_state_dir-1
		dta		<pa_state_rename-1
		dta		<pa_state_move-1
		
;============================================================================
; Parser instructions
;
;	$00-1F	Parser command
;	$20-7F	Literal character match
;	$80-FF	Jump/Jsr to state (even for jump, odd for jsr)
;
.macro PA_BRANCH_TARGET
		dta		:1-(*+1)
		.if :1<*-$80||:1>*+$7f
		.error "Branch from ",*," to ",:1," out of range."
		.endif
.endm

.macro PAI_EXPECT			;Expect a character; fail if not there.
		dta		c:1
.endm

.macro PAI_SPACES			;Eat zero or more spaces.
		dta		c' '
.endm

.macro PAI_FAIL				;Fail the current line; backtrack if possible.
		dta		$00
.endm

.macro PAI_ACCEPT			;Accept the current OR clause (pop backtracking state).
		dta		$01
.endm

.macro PAI_TRYSTATEMENT		;Try to parse a statement; jump to statement state if so.
		dta		$02
.endm

.macro PAI_OR				;Push a backtracking state.
		dta		$03
		.if :1<*
		.error "PAI_OR only allows forward branches"
		.endif
		dta		:1-(*+1)
.endm

.macro PAI_EOL				;Check for end of line; fail if missing.
		dta		$04
.endm

.macro PAI_B				;Unconditional branch.
		dta		$05
		PA_BRANCH_TARGET :1
.endm

.macro PAI_BEQ				;Branch and eat character if match.
		dta		$06,c:1,$00
		PA_BRANCH_TARGET :2
.endm

.macro PAI_BEQEMIT			;Branch, emit, and eat character if match.
		dta		$06,c:1,:2
		PA_BRANCH_TARGET :3
.endm

.macro PAI_EMIT				;Emit a token.
		dta		$07,:1
.endm

.macro PAI_COPYLINE			;Copy remainder of line
		dta		$08
.endm

.macro PAI_RTS				;Return from subroutine.
		dta		$09
.endm

.macro PAI_TRYNUMBER		;Try to parse and emit a number; jump to target if so.
		dta		$0a
		PA_BRANCH_TARGET :1
.endm

.macro PAI_TRYVARIABLE		;Try to parse and emit a variable; jump to target if so.
		dta		$0b
		PA_BRANCH_TARGET :1
.endm

.macro PAI_TRYFUNCTION		;Try to parse and emit a variable; jump to target if so.
		dta		$0c
		PA_BRANCH_TARGET :1
.endm

.macro PAI_HEX_B			;Parse hex and then branch
		dta		$0d
		PA_BRANCH_TARGET :1
.endm

.macro PAI_STEND			;End a statement.
		dta		$0e
.endm

.macro PAI_STRING			;Parse a string literal.
		dta		$0f
.endm

.macro PAI_BSTR				;Branch if last variable was string.
		dta		$10
		PA_BRANCH_TARGET :1
.endm

.macro PAI_NUM				;Set expression type to number.
		dta		$11
.endm

.macro PAI_STR				;Set expression type to string.
		dta		$12
.endm

.macro PAI_EMIT_B			;PAI_EMIT + PAI_B
		dta		$13
		dta		:1
		PA_BRANCH_TARGET :2
.endm

.macro PAI_TRYARRAYVAR		;Try to parse and emit a array or string array variable; jump to target if so.
		dta		$14
		PA_BRANCH_TARGET :1
.endm

.macro PAI_BEOS				;Branch if end of statement
		dta		$15
		PA_BRANCH_TARGET :1
.endm

.macro PAI_JUMP				;Jump to the given state.
		dta		$80+[:1]*2
.endm

.macro PAI_JSR				;Jump to subroutine.
		dta		$81+[:1]*2
.endm

.macro PAM_EXPR
		PAI_JSR PST_EXPR
.endm

.macro PAM_AEXPR
		PAI_JSR PST_AEXPR
.endm

.macro PAM_SEXPR
		PAI_JSR PST_SEXPR
.endm

.macro PAM_COMMA
		PAI_JSR	PST_COMMA
.endm

.macro PAM_AEXPR_COMMA
		PAI_JSR	PST_AEXPR_COMMA
.endm

.macro PAM_NEXT
		PAI_JUMP PST_NEXT
.endm

.macro PAM_AVAR
		PAI_JSR PST_AVAR
.endm

.macro PAM_IOCB
		PAI_JSR PST_IOCB
.endm

;============================================================================
PST_NEXT			= $01
PST_EXPR			= $02
PST_AEXPR			= $03
PST_SEXPR			= $04
PST_ASSIGN			= $05
PST_AVAR			= $06
PST_IOCB			= $07
PST_ARRAY			= $08
PST_ARRAY2			= $09
PST_COMMA			= $0A
PST_AEXPR_COMMA		= $0B
PST_STATEMENT_BASE	= (parse_state_table_statements-parse_state_table)/2

;============================================================================
.proc pa_state0		;initial statement
		PAI_TRYSTATEMENT
		
		;assume it's an implicit let
		PAI_EMIT	TOK_ILET
		PAI_JUMP	PST_ASSIGN
.endp
		
.proc pa_state1
		;skip spaces
		PAI_SPACES
		
		;check for continuation
		PAI_BEQEMIT	':', TOK_EOS, next_statement
		PAI_EOL
		PAI_EMIT	TOK_EOL
next_statement:
		PAI_STEND
.endp

;----------------------------
.proc pa_aexpr
		;This is pretty complicated.

str_var:
		PAI_FAIL
arrayvar:
		PAI_JSR		PST_ARRAY
variable:
need_either_operator:
		PAI_BSTR	str_var
		PAI_ACCEPT
		PAI_B		need_operator

const_hex:
		PAI_HEX_B	need_operator

open_paren:
		PAM_AEXPR
		PAI_EXPECT	')'
		PAI_EMIT_B	TOK_EXP_CLOSEPAREN, need_operator

entry:
need_value:
		PAI_SPACES
		PAI_BEQEMIT	'+', TOK_EXP_UNPLUS, need_value
		PAI_BEQEMIT	'-', TOK_EXP_UNMINUS, need_value
		PAI_BEQEMIT	'(', TOK_EXP_OPENPAREN, open_paren
		PAI_BEQ		'$',const_hex
		PAI_TRYNUMBER	need_operator
		PAI_OR		not_not
		PAI_EXPECT	'N'
		PAI_EXPECT	'O'
		PAI_EXPECT	'T'
		PAI_ACCEPT
		PAI_EMIT	TOK_EXP_NOT
		PAI_B		need_value
not_not:
		PAI_OR		svalue
		PAI_TRYFUNCTION	variable
		PAI_TRYARRAYVAR	arrayvar
		PAI_TRYVARIABLE	variable
		PAI_FAIL

op_less:
		PAI_BEQEMIT	'=', TOK_EXP_LE, need_value
		PAI_BEQEMIT	'>', TOK_EXP_NE, need_value
		PAI_EMIT_B	TOK_EXP_LT, need_value

need_operator:
		PAI_SPACES
		PAI_BEQEMIT	'+', TOK_EXP_ADD, need_value
		PAI_BEQEMIT	'-', TOK_EXP_SUBTRACT, need_value
		PAI_BEQEMIT	'*', TOK_EXP_MULTIPLY, need_value
		PAI_BEQEMIT	'/', TOK_EXP_DIVIDE, need_value
		PAI_BEQEMIT	'^', TOK_EXP_POWER, need_value
		PAI_BEQ		'<',op_less
		PAI_BEQ		'>',op_greater
		PAI_BEQEMIT	'=', TOK_EXP_EQUAL, need_value
		PAI_BEQEMIT	'%', TOK_EXP_BITWISE_XOR, need_value
		PAI_BEQEMIT	'!', TOK_EXP_BITWISE_OR, need_value
		PAI_BEQEMIT	'&', TOK_EXP_BITWISE_AND, need_value
		PAI_OR		not_and
		PAI_EXPECT	'A'
		PAI_EXPECT	'N'
		PAI_EXPECT	'D'
		PAI_EMIT	TOK_EXP_AND
		PAI_ACCEPT
		PAI_B		need_value

not_and:
		PAI_OR		not_or
		PAI_EXPECT	'O'
		PAI_EXPECT	'R'
		PAI_EMIT	TOK_EXP_OR
		PAI_ACCEPT
		PAI_B		need_value
not_or:
		PAI_RTS

op_greater:
		PAI_BEQEMIT	'=', TOK_EXP_GE, need_value
		PAI_EMIT_B	TOK_EXP_GT, need_value

svalue:
		PAM_SEXPR
need_soperator:
		PAI_SPACES
		PAI_BEQ		'<',op_str_l
		PAI_BEQ		'>',op_str_g
		PAI_BEQEMIT	'=', TOK_EXP_STR_EQ, need_svalue
		PAI_FAIL

op_str_l:
		PAI_BEQEMIT	'>', TOK_EXP_STR_NE, need_svalue
		PAI_BEQEMIT	'=', TOK_EXP_STR_LE, need_svalue
		PAI_EMIT_B	TOK_EXP_STR_LT, need_svalue

op_str_g:
		PAI_BEQEMIT	'=', TOK_EXP_STR_GE, need_svalue
		PAI_EMIT_B	TOK_EXP_STR_GT, need_svalue
		
need_svalue:
		PAM_SEXPR
		PAI_B		need_operator
.endp

;----------------------------
.proc pa_expr
		PAI_OR		sexpr
		PAM_AEXPR
		PAI_ACCEPT
		PAI_RTS
sexpr:
		PAM_SEXPR
		PAI_RTS
.endp

;----------------------------
.proc pa_sexpr
		PAI_SPACES
		PAI_BEQ		'"',const_string
		PAI_TRYFUNCTION	func
		PAI_TRYVARIABLE var
		PAI_FAIL
var:
		PAI_BSTR	is_str
		PAI_FAIL
is_str:
		PAI_BEQEMIT	'(', TOK_EXP_OPEN_STR, is_arraystr
		PAI_RTS
const_string:
		PAI_STRING
done:
		PAI_RTS
func:
		PAI_BSTR	done
		PAI_FAIL
		
is_arraystr:
		PAM_AEXPR
		PAI_SPACES
		PAI_BEQEMIT	',', TOK_EXP_ARRAY_COMMA, substr
end_arraystr:
		PAI_EXPECT	')'
		PAI_EMIT	TOK_EXP_CLOSEPAREN
		PAI_STR
		PAI_RTS

substr:
		PAM_AEXPR
		PAI_SPACES
		PAI_B		end_arraystr
.endp

;----------------------------
.proc pa_assign
		PAI_TRYARRAYVAR is_array
		PAI_TRYVARIABLE ilet_ok
		PAI_FAIL
is_array:
		PAI_JSR		PST_ARRAY
ilet_ok:
		PAI_SPACES
		PAI_EXPECT	'='
		PAI_BSTR	string_assign
		PAI_EMIT	TOK_EXP_ASSIGNNUM
		PAM_AEXPR
		PAM_NEXT
		
string_assign:
		PAI_EMIT	TOK_EXP_ASSIGNSTR
		PAM_SEXPR
		PAM_NEXT
.endp
		
;----------------------------
.proc pa_avar
		PAI_SPACES
		PAI_TRYVARIABLE var_ok
fail:
		PAI_FAIL
var_ok:
		PAI_BSTR	fail
		PAI_RTS
.endp

;----------------------------
.proc pa_iocb
		PAI_SPACES
		PAI_EXPECT	'#'
		PAI_EMIT	TOK_EXP_HASH
		PAM_AEXPR
		PAI_SPACES
		PAI_RTS
.endp

;----------------------------
.proc pa_array
		PAI_BSTR	sarrayvar		
		PAI_EMIT	TOK_EXP_OPEN_ARY
		PAI_JSR		PST_ARRAY2
		PAI_NUM
		PAI_RTS

sarrayvar:
		PAI_BEQEMIT	'(', TOK_EXP_OPEN_STR, substring
sarrayvar_exit:
		PAI_STR
		PAI_RTS
		
substring:
		PAI_JSR		PST_ARRAY2
		PAI_B		sarrayvar_exit
.endp

;----------------------------
.proc pa_array2
		PAM_AEXPR
		PAI_SPACES
		PAI_BEQEMIT	',', TOK_EXP_ARRAY_COMMA, multi
term:
		PAI_EMIT	TOK_EXP_CLOSEPAREN
		PAI_SPACES
		PAI_EXPECT	')'
		PAI_RTS
		
multi:
		PAM_AEXPR
		PAI_B		term
.endp

;----------------------------
pa_aexpr_comma:
		PAM_AEXPR
.proc pa_comma
		PAI_SPACES
		PAI_EXPECT	','
		PAI_EMIT	TOK_EXP_COMMA
		PAI_RTS
.endp

;==========================================================================
.pages 1

pa_functions_begin:

;aexpr fun(aexpr)
pa_state_abs:
pa_state_atn:
pa_state_clog:
pa_state_cos:
pa_state_exp:
pa_state_fre:
pa_state_int:
pa_state_log:
pa_state_paddle:
pa_state_peek:
pa_state_ptrig:
pa_state_rnd:
pa_state_sgn:
pa_state_sin:
pa_state_sqr:
pa_state_stick:
pa_state_strig:
pa_state_dpeek:
pa_state_vstick:
pa_state_hstick:
		PAI_EXPECT	'('
		PAI_EMIT	TOK_EXP_OPEN_FUN
		PAM_AEXPR
pa_close_numeric_function:
		PAI_EXPECT	')'
		PAI_EMIT	TOK_EXP_CLOSEPAREN
pa_end_numeric_function:
		PAI_NUM
		PAI_RTS

;aexpr fun(sexpr)
pa_state_adr:
pa_state_asc:
pa_state_len:
pa_state_val:
		PAI_EXPECT	'('
		PAI_EMIT	TOK_EXP_OPEN_FUN
		PAM_SEXPR
		PAI_B		pa_close_numeric_function

;aexpr usr(aexpr[, aexpr])
pa_state_usr:
		PAI_EXPECT	'('
		PAI_EMIT	TOK_EXP_OPEN_FUN
pa_state_usr_loop:
		PAM_AEXPR
		PAI_BEQEMIT	')', TOK_EXP_CLOSEPAREN, pa_end_numeric_function
		PAI_BEQEMIT	',', TOK_EXP_ARRAY_COMMA, pa_state_usr_loop
		PAI_FAIL

;sexpr fun(aexpr)
pa_state_chr:
pa_state_str:
pa_state_hex:
		PAI_EXPECT	'('
.endpg
		PAI_EMIT	TOK_EXP_OPEN_FUN
		PAM_AEXPR
		PAI_EXPECT	')'
		PAI_EMIT	TOK_EXP_CLOSEPAREN
		PAI_STR
		PAI_RTS

pa_functions_end:

;==========================================================================

.pages 1
pa_statements_begin:

pa_state_sound:			;STATEMENT aexpr,aexpr,aexpr,aexpr
		PAM_AEXPR_COMMA
pa_state_setcolor:		;STATEMENT aexpr,aexpr,aexpr
pa_state_move:
		PAM_AEXPR_COMMA
pa_state_drawto:		;STATEMENT aexpr,aexpr
pa_state_plot:
pa_state_poke:
pa_state_position:
pa_state_dpoke:
		PAM_AEXPR_COMMA
pa_state_color:			;STATEMENT aexpr
pa_state_goto:
pa_state_goto2:
pa_state_gosub:
pa_state_graphics:
pa_state_trap:
		PAM_AEXPR
pa_state_bye:			;STATEMENT
pa_state_cload:
pa_state_clr:
pa_state_cont:
pa_state_csave:
pa_state_deg:
pa_state_dos:
pa_state_end:
pa_state_new:
pa_state_pop:
pa_state_rad:
pa_state_return:
pa_state_stop:
pa_state_cp:
		PAM_NEXT

pa_state_bput:			;BPUT #iocb,aexpr,aexpr
pa_state_bget:			;BGET #iocb,aexpr,aexpr
		PAM_IOCB
		PAM_COMMA
		PAI_B			pa_state_dpoke

pa_state_close:
		PAM_IOCB
		PAM_NEXT

pa_state_data:
pa_state_rem:
		PAI_SPACES
		PAI_COPYLINE

;---------------------------------------------------------------------------		
.proc pa_state_restore
		PAI_BEOS	no_lineno
		PAM_AEXPR
no_lineno:
		PAM_NEXT
.endp

;---------------------------------------------------------------------------		
pa_state_com = pa_state_dim
.proc pa_state_dim
next_var_2:
		PAI_TRYARRAYVAR	is_var
		PAI_FAIL

is_var:
		PAI_BSTR	is_string
		PAI_EMIT	TOK_EXP_OPEN_DIMARY
		PAM_AEXPR
		PAI_SPACES
		PAI_BEQEMIT	',', TOK_EXP_ARRAY_COMMA, is_multi
is_done:
		PAI_SPACES
		PAI_EXPECT	')'
		PAI_EMIT	TOK_EXP_CLOSEPAREN
		PAI_SPACES
		PAI_BEQEMIT	',', TOK_EXP_COMMA, next_var_2
		PAM_NEXT
		
is_multi:
last_dim:
		PAM_AEXPR
		PAI_B		is_done
		
is_string:
		PAI_EXPECT	'('
		PAI_EMIT	TOK_EXP_OPEN_DIMSTR
		PAI_B		last_dim
.endp

.proc pa_state_for
		PAI_TRYVARIABLE have_loop_var
		PAI_FAIL
have_loop_var:
		PAI_SPACES
		PAI_EXPECT	'='
		PAI_EMIT	TOK_EXP_ASSIGNNUM
		PAM_AEXPR
		PAI_SPACES
		PAI_EMIT	TOK_EXP_TO
		PAI_EXPECT	'T'
		PAI_EXPECT	'O'
		PAM_AEXPR
		PAI_SPACES
		PAI_BEQEMIT	'S', TOK_EXP_STEP, have_step
		PAM_NEXT
have_step:
		PAI_EXPECT	'T'
		PAI_EXPECT	'E'
		PAI_EXPECT	'P'
		PAM_AEXPR
		PAM_NEXT
.endp

;--------------------------------------------------------------------------
pa_state_get:
pa_state_status:
		PAM_IOCB
		PAM_COMMA
		PAM_AVAR
		PAM_NEXT

;--------------------------------------------------------------------------
pa_state_put:
		PAM_IOCB
		PAM_COMMA
		PAM_AEXPR
		PAM_NEXT

;--------------------------------------------------------------------------
.proc pa_state_input
		PAI_SPACES
		
		PAI_OR		var_loop
		PAM_IOCB
		PAI_ACCEPT
		PAI_BEQEMIT	';', TOK_EXP_SEMI, var_loop
		PAM_COMMA
.def :pa_state_read = *
var_loop:
		PAI_TRYVARIABLE var_ok
		PAI_FAIL
var_ok:
		PAI_BEOS	end
		PAM_COMMA
		PAI_B		var_loop
end:
		PAM_NEXT
.endp

;--------------------------------------------------------------------------
.proc pa_state_if
		PAM_AEXPR
		PAI_EMIT	TOK_EXP_THEN
		PAI_SPACES
		PAI_EXPECT	'T'
		PAI_EXPECT	'H'
		PAI_EXPECT	'E'
		PAI_EXPECT	'N'
		PAI_TRYNUMBER	is_lineno
		PAI_STEND			;must end statement without EOS
		PAI_TRYSTATEMENT
		PAM_AEXPR
is_lineno:
		PAM_NEXT
.endp

;--------------------------------------------------------------------------
; LIST filename[,lineno[,lineno]]
; LIST [lineno[,lineno]]
.proc pa_state_list
		PAI_BEOS	end
		PAI_OR		no_filespec
		PAM_SEXPR
		PAI_ACCEPT
		PAI_BEOS	end
		PAM_COMMA
no_filespec:
		PAM_AEXPR
		PAI_BEOS	end
		PAM_COMMA
		PAM_AEXPR
end:
		PAM_NEXT
.endp

;--------------------------------------------------------------------------
.proc pa_state_let
		PAI_JUMP	PST_ASSIGN
.endp

;--------------------------------------------------------------------------
pa_state_locate:
		PAM_AEXPR_COMMA
		PAM_AEXPR_COMMA
pa_state_next:
		PAM_AVAR
		PAM_NEXT

pa_state_note:
pa_state_point:
		PAM_IOCB
		PAM_COMMA
		PAM_AVAR
		PAM_COMMA
		PAM_AVAR
		PAM_NEXT

.proc pa_state_on
		;parse conditional expression
		PAM_AEXPR
		PAI_SPACES
		PAI_EXPECT	'G'
		PAI_EXPECT	'O'
		PAI_BEQEMIT	'T', TOK_EXP_GOTO, is_goto
		PAI_EXPECT	'S'
		PAI_EXPECT	'U'
		PAI_EXPECT	'B'
		PAI_EMIT	TOK_EXP_GOSUB
		PAI_B		linenos
		
is_goto:
		PAI_EXPECT	'O'
lineno_comma:
linenos:
		PAM_AEXPR
		PAI_BEQEMIT	',', TOK_EXP_COMMA, lineno_comma
		PAM_NEXT
.endp

pa_state_xio:
		PAM_AEXPR_COMMA
pa_state_open:
		PAM_IOCB
		PAM_COMMA
		PAM_AEXPR_COMMA
		PAM_AEXPR_COMMA
pa_state_load:
pa_state_save:
pa_state_enter:
pa_state_erase:
pa_state_protect:
pa_state_unprotect:
pa_state_rename:
		PAM_SEXPR
pa_state_print_simple:
		PAM_NEXT

pa_state_run:
pa_state_dir:
		PAI_SPACES
		PAI_BEOS	pa_state_run_2
		PAM_SEXPR
pa_state_run_2:
		PAM_NEXT

pa_state_lprint:
		PAI_BEOS	pa_state_print_simple
		PAI_B		pa_state_print_item

pa_state_print:
		PAI_SPACES
.endpg
		PAI_BEOS	pa_state_print_simple
		PAI_OR		pa_state_print_item
		PAM_IOCB
		PAI_ACCEPT
		PAI_B		pa_state_print_sep
pa_state_print_item:
		PAI_SPACES
		PAI_BEOS	pa_state_print_simple
		PAI_BEQEMIT	',', TOK_EXP_COMMA, pa_state_print_item
		PAI_BEQEMIT	';', TOK_EXP_SEMI, pa_state_print_item
		PAM_EXPR
pa_state_print_sep:
		PAI_BEOS	pa_state_print_simple
		PAI_BEQEMIT	',', TOK_EXP_COMMA, pa_state_print_item
		PAI_BEQEMIT	';', TOK_EXP_SEMI, pa_state_print_item
		PAI_FAIL

pa_statements_end:

;============================================================================		
statement_table:
		dta		c'RE',c'M'+$80		;R.
		dta		c'DAT',c'A'+$80		;D.
		dta		c'INPU',c'T'+$80	;I.
		dta		c'COLO',c'R'+$80	;C.		exp
		dta		c'LIS',c'T'+$80		;L.
		dta		c'ENTE',c'R'+$80	;E.
		dta		c'LE',c'T'+$80		;LE.
		dta		c'I',c'F'+$80		;IF
		dta		c'FO',c'R'+$80		;F.
		dta		c'NEX',c'T'+$80		;N.
		dta		c'GOT',c'O'+$80		;G.
		dta		c'GO T',c'O'+$80
		dta		c'GOSU',c'B'+$80	;GOS.
		dta		c'TRA',c'P'+$80		;T.
		dta		c'BY',c'E'+$80		;B.
		dta		c'CON',c'T'+$80		;CON.
		dta		c'CO',c'M'+$80
		dta		c'CLOS',c'E'+$80	;CL.	#exp
		dta		c'CL',c'R'+$80
		dta		c'DE',c'G'+$80		;DE.
		dta		c'DI',c'M'+$80		;DI.
		dta		c'EN',c'D'+$80		;
		dta		c'NE',c'W'+$80		;
		dta		c'OPE',c'N'+$80		;O.
		dta		c'LOA',c'D'+$80		;LO.
		dta		c'SAV',c'E'+$80		;S.
		dta		c'STATU',c'S'+$80	;ST.
		dta		c'NOT',c'E'+$80		;NO.
		dta		c'POIN',c'T'+$80	;P.
		dta		c'XI',c'O'+$80		;X.
		dta		c'O',c'N'+$80		;
		dta		c'POK',c'E'+$80		;POK.
		dta		c'PRIN',c'T'+$80	;PR.
		dta		c'RA',c'D'+$80		;
		dta		c'REA',c'D'+$80		;REA.
		dta		c'RESTOR',c'E'+$80	;RES.
		dta		c'RETUR',c'N'+$80	;RET.
		dta		c'RU',c'N'+$80		;RU.
		dta		c'STO',c'P'+$80		;STO.
		dta		c'PO',c'P'+$80		;
		dta		c'?'+$80
		dta		c'GE',c'T'+$80		;GE.
		dta		c'PU',c'T'+$80		;PU.
		dta		c'GRAPHIC',c'S'+$80	;GR.
		dta		c'PLO',c'T'+$80		;PL.
		dta		c'POSITIO',c'N'+$80	;POS.
		dta		c'DO',c'S'+$80		;DO.
		dta		c'DRAWT',c'O'+$80	;DR.
		dta		c'SETCOLO',c'R'+$80	;SE.
		dta		c'LOCAT',c'E'+$80	;LOC.
		dta		c'SOUN',c'D'+$80	;SO.
		dta		c'LPRIN',c'T'+$80	;LP.
		dta		c'CSAV',c'E'+$80
		dta		c'CLOA',c'D'+$80	;CLOA.
		dta		c'?'+$80
		dta		c'?'+$80
		dta		c'?'+$80
		dta		c'?'+$80
		dta		c'?'+$80
		dta		c'?'+$80
		dta		c'?'+$80
		dta		c'?'+$80
		dta		c'DPOK',c'E'+$80
		dta		c'?'+$80

		;$40
		dta		c'?'+$80
		dta		c'?'+$80
		dta		c'?'+$80
		dta		c'BPU',c'T'+$80
		dta		c'BGE',c'T'+$80
		dta		c'?'+$80
		dta		c'C',c'P'+$80
		dta		c'ERAS',c'E'+$80
		dta		c'PROTEC',c'T'+$80
		dta		c'UNPROTEC',c'T'+$80
		dta		c'DI',c'R'+$80		;
		dta		c'RENAM',c'E'+$80
		dta		c'MOV',c'E'+$80
		dta		0					;end for searching
	
.echo "-- Statement token table length: ", *-statement_table

		;functions
parse_state_table_functions:
		dta		<pa_state_str-1
		dta		<pa_state_chr-1
		dta		<pa_state_usr-1
		dta		<pa_state_asc-1
		dta		<pa_state_val-1
		dta		<pa_state_len-1
		dta		<pa_state_adr-1
		dta		<pa_state_atn-1
		dta		<pa_state_cos-1
		dta		<pa_state_peek-1
		dta		<pa_state_sin-1
		dta		<pa_state_rnd-1
		dta		<pa_state_fre-1
		dta		<pa_state_exp-1
		dta		<pa_state_log-1
		dta		<pa_state_clog-1
		dta		<pa_state_sqr-1
		dta		<pa_state_sgn-1
		dta		<pa_state_abs-1
		dta		<pa_state_int-1
		dta		<pa_state_paddle-1
		dta		<pa_state_stick-1
		dta		<pa_state_ptrig-1
		dta		<pa_state_strig-1		
		dta		0
		dta		0
		dta		0
		dta		0
		dta		0
		dta		0
		dta		0
		dta		<pa_state_hex-1
		dta		0
		dta		<pa_state_dpeek-1
		dta		0
		dta		<pa_state_vstick-1
		dta		<pa_state_hstick-1

.echo "- Parser program length: ",*-?parser_program_start
