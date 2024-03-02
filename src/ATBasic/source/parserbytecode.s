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
		dta		a(pa_state_rem-1)
		dta		a(pa_state_data-1)
		dta		a(pa_state_input-1)
		dta		a(pa_state_color-1)
		dta		a(pa_state_list-1)
		dta		a(pa_state_enter-1)
		dta		a(pa_state_let-1)
		dta		a(pa_state_if-1)
		dta		a(pa_state_for-1)
		dta		a(pa_state_next-1)
		dta		a(pa_state_goto-1)
		dta		a(pa_state_goto2-1)
		dta		a(pa_state_gosub-1)
		dta		a(pa_state_trap-1)
		dta		a(pa_state_bye-1)
		dta		a(pa_state_cont-1)
		dta		a(pa_state_com-1)
		dta		a(pa_state_close-1)
		dta		a(pa_state_clr-1)
		dta		a(pa_state_deg-1)
		dta		a(pa_state_dim-1)
		dta		a(pa_state_end-1)
		dta		a(pa_state_new-1)
		dta		a(pa_state_open-1)
		dta		a(pa_state_load-1)
		dta		a(pa_state_save-1)
		dta		a(pa_state_status-1)
		dta		a(pa_state_note-1)
		dta		a(pa_state_point-1)
		dta		a(pa_state_xio-1)
		dta		a(pa_state_on-1)
		dta		a(pa_state_poke-1)
		dta		a(pa_state_print-1)
		dta		a(pa_state_rad-1)
		dta		a(pa_state_read-1)
		dta		a(pa_state_restore-1)
		dta		a(pa_state_return-1)
		dta		a(pa_state_run-1)
		dta		a(pa_state_stop-1)
		dta		a(pa_state_pop-1)
		dta		a(pa_state_print-1)
		dta		a(pa_state_get-1)
		dta		a(pa_state_put-1)
		dta		a(pa_state_graphics-1)
		dta		a(pa_state_plot-1)
		dta		a(pa_state_position-1)
		dta		a(pa_state_dos-1)
		dta		a(pa_state_drawto-1)
		dta		a(pa_state_setcolor-1)
		dta		a(pa_state_locate-1)
		dta		a(pa_state_sound-1)
		dta		a(pa_state_lprint-1)
		dta		a(pa_state_csave-1)
		dta		a(pa_state_cload-1)

		;functions
parse_state_table_functions:
		dta		a(pa_state_str-1)
		dta		a(pa_state_chr-1)
		dta		a(pa_state_usr-1)
		dta		a(pa_state_asc-1)
		dta		a(pa_state_val-1)
		dta		a(pa_state_len-1)
		dta		a(pa_state_adr-1)
		dta		a(pa_state_atn-1)
		dta		a(pa_state_cos-1)
		dta		a(pa_state_peek-1)
		dta		a(pa_state_sin-1)
		dta		a(pa_state_rnd-1)
		dta		a(pa_state_fre-1)
		dta		a(pa_state_exp-1)
		dta		a(pa_state_log-1)
		dta		a(pa_state_clog-1)
		dta		a(pa_state_sqr-1)
		dta		a(pa_state_sgn-1)
		dta		a(pa_state_abs-1)
		dta		a(pa_state_int-1)
		dta		a(pa_state_paddle-1)
		dta		a(pa_state_stick-1)
		dta		a(pa_state_ptrig-1)
		dta		a(pa_state_strig-1)		
		dta		a(pa_state_and-1)
		dta		a(pa_state_not-1)
		dta		a(pa_state_or-1)

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

.macro PAI_STBEGIN			;Begin a statement.
		dta		$0d
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
PST_FUNCTION_BASE	= (parse_state_table_functions-parse_state_table)/2

;============================================================================
.proc pa_state0		;initial statement
		PAI_STBEGIN
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

arrayvar:
		PAI_JSR		PST_ARRAY
variable:
		PAI_BSTR	need_soperator
		PAI_B		need_operator

entry:
need_value:
		PAI_SPACES
		PAI_BEQEMIT	'+', TOK_EXP_UNPLUS, need_value
		PAI_BEQEMIT	'-', TOK_EXP_UNMINUS, need_value
		PAI_BEQEMIT	'(', TOK_EXP_OPENPAREN, open_paren
		PAI_BEQ		'"',const_string
		PAI_TRYNUMBER	need_operator
		PAI_TRYFUNCTION	need_operator
		PAI_OR		not_not
		PAI_EXPECT	'N'
		PAI_EXPECT	'O'
		PAI_EXPECT	'T'
		PAI_ACCEPT
		PAI_EMIT	TOK_EXP_NOT
		PAI_B		need_value
not_not:
		PAI_TRYARRAYVAR	arrayvar
		PAI_TRYVARIABLE	variable
		PAI_FAIL
		
open_paren:
		PAM_AEXPR
		PAI_EXPECT	')'
		PAI_EMIT_B	TOK_EXP_CLOSEPAREN, need_operator

op_less:
		PAI_BEQEMIT	'=', TOK_EXP_LE, need_value
		PAI_BEQEMIT	'>', TOK_EXP_NE, need_value
		PAI_EMIT_B	TOK_EXP_LT, need_value

const_string:
		PAI_STRING
need_soperator:
		PAI_SPACES
		PAI_BEQ		'<',op_str_l
		PAI_BEQ		'>',op_str_g
		PAI_BEQEMIT	'=', TOK_EXP_STR_EQ, need_svalue
		PAI_FAIL

const_string2:
		PAI_STRING
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

op_str_l:
		PAI_BEQEMIT	'>', TOK_EXP_STR_NE, need_svalue
		PAI_BEQEMIT	'=', TOK_EXP_STR_LE, need_svalue
		PAI_EMIT_B	TOK_EXP_STR_LT, need_svalue

op_str_g:
		PAI_BEQEMIT	'=', TOK_EXP_STR_GE, need_svalue
		PAI_EMIT_B	TOK_EXP_STR_GT, need_svalue
		
need_svalue:
		PAI_SPACES
		PAI_BEQ		'"',const_string2
		PAI_TRYVARIABLE svariable
		PAI_FAIL
		
svariable:
		PAI_BSTR	need_operator
		PAI_FAIL
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
func:
		PAI_RTS
		
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
pa_state_sound:			;STATEMENT aexpr,aexpr,aexpr,aexpr
		PAM_AEXPR_COMMA
pa_state_setcolor:		;STATEMENT aexpr,aexpr,aexpr
		PAM_AEXPR_COMMA
pa_state_drawto:		;STATEMENT aexpr,aexpr
pa_state_plot:
pa_state_poke:
pa_state_position:
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
		PAM_NEXT
		
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
pa_state_put:
		PAM_IOCB
		PAM_COMMA
		PAM_AVAR
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
		PAI_STBEGIN
		PAI_TRYSTATEMENT
		PAM_AEXPR
		PAM_NEXT
		
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
		PAI_B		linenos
		
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
		PAM_SEXPR
		PAM_NEXT

pa_state_lprint:
		PAI_BEOS	pa_state_print_simple
		PAI_B		pa_state_print_item

pa_state_print:
		PAI_SPACES
		PAI_BEOS	pa_state_print_simple
		PAI_OR		pa_state_print_item
		PAM_IOCB
		PAI_ACCEPT
pa_state_print_item:
		PAI_BEOS	pa_state_print_simple
		PAI_SPACES
		PAI_BEQEMIT	',', TOK_EXP_COMMA, pa_state_print_item
		PAI_BEQEMIT	';', TOK_EXP_SEMI, pa_state_print_item
		PAM_EXPR
		PAI_B		pa_state_print_item
pa_state_print_simple:
		PAM_NEXT

pa_state_run:
		PAI_OR		pa_state_run_2
		PAM_SEXPR
		PAI_ACCEPT
pa_state_run_2:
		PAM_NEXT

;==========================================================================
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
		PAI_EXPECT	'('
		PAI_EMIT	TOK_EXP_OPEN_FUN
		PAM_AEXPR
		PAI_EXPECT	')'
		PAI_EMIT	TOK_EXP_CLOSEPAREN
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
		PAI_EXPECT	')'
		PAI_EMIT	TOK_EXP_CLOSEPAREN
		PAI_NUM
		PAI_RTS

;sexpr fun(aexpr)
pa_state_chr:
pa_state_str:
		PAI_EXPECT	'('
		PAI_EMIT	TOK_EXP_OPEN_FUN
		PAM_AEXPR
		PAI_EXPECT	')'
		PAI_EMIT	TOK_EXP_CLOSEPAREN
		PAI_STR
		PAI_RTS

;aexpr usr(aexpr[, aexpr])
.proc pa_state_usr
		PAI_EXPECT	'('
		PAI_EMIT	TOK_EXP_OPEN_FUN
loop:
		PAM_AEXPR
		PAI_BEQEMIT	')', TOK_EXP_CLOSEPAREN, done
		PAI_BEQEMIT	',', TOK_EXP_ARRAY_COMMA, loop
		PAI_FAIL

done:
		PAI_NUM
		PAI_RTS
.endp

pa_state_and:
pa_state_not:
pa_state_or:
		PAI_FAIL

;============================================================================		
statement_table:
stname_rem:		dta		c'RE',c'M'+$80		;R.
stname_data:	dta		c'DAT',c'A'+$80		;D.
stname_input:	dta		c'INPU',c'T'+$80	;I.
stname_color:	dta		c'COLO',c'R'+$80	;C.		exp
stname_list:	dta		c'LIS',c'T'+$80		;L.
stname_enter:	dta		c'ENTE',c'R'+$80	;E.
stname_let:		dta		c'LE',c'T'+$80		;LE.
stname_if:		dta		c'I',c'F'+$80		;IF
stname_for:		dta		c'FO',c'R'+$80		;F.
stname_next:	dta		c'NEX',c'T'+$80		;N.
stname_goto:	dta		c'GOT',c'O'+$80		;G.
stname_go_to:	dta		c'GO T',c'O'+$80
stname_gosub:	dta		c'GOSU',c'B'+$80	;GOS.
stname_trap:	dta		c'TRA',c'P'+$80		;T.
stname_bye:		dta		c'BY',c'E'+$80		;B.
stname_cont:	dta		c'CON',c'T'+$80		;CON.
stname_com:		dta		c'CO',c'M'+$80
stname_close:	dta		c'CLOS',c'E'+$80	;CL.	#exp
stname_clr:		dta		c'CL',c'R'+$80
stname_deg:		dta		c'DE',c'G'+$80		;DE.
stname_dim:		dta		c'DI',c'M'+$80		;DI.
stname_end:		dta		c'EN',c'D'+$80		;
stname_new:		dta		c'NE',c'W'+$80		;
stname_open:	dta		c'OPE',c'N'+$80		;O.
stname_load:	dta		c'LOA',c'D'+$80		;LO.
stname_save:	dta		c'SAV',c'E'+$80		;S.
stname_status:	dta		c'STATU',c'S'+$80	;ST.
stname_note:	dta		c'NOT',c'E'+$80		;NO.
stname_point:	dta		c'POIN',c'T'+$80	;P.
stname_xio:		dta		c'XI',c'O'+$80		;X.
stname_on:		dta		c'O',c'N'+$80		;
stname_poke:	dta		c'POK',c'E'+$80		;POK.
stname_print:	dta		c'PRIN',c'T'+$80	;PR.
stname_rad:		dta		c'RA',c'D'+$80		;
stname_read:	dta		c'REA',c'D'+$80		;REA.
stname_restore:	dta		c'RESTOR',c'E'+$80	;RES.
stname_return:	dta		c'RETUR',c'N'+$80	;RET.
stname_run:		dta		c'RU',c'N'+$80		;RU.
stname_stop:	dta		c'STO',c'P'+$80		;STO.
stname_pop:		dta		c'PO',c'P'+$80		;
stname_qprint:	dta		c'?'+$80
stname_get:		dta		c'GE',c'T'+$80		;GE.
stname_put:		dta		c'PU',c'T'+$80		;PU.
stname_graphics dta		c'GRAPHIC',c'S'+$80	;GR.
stname_plot:	dta		c'PLO',c'T'+$80		;PL.
stname_position dta		c'POSITIO',c'N'+$80	;POS.
stname_dos:		dta		c'DO',c'S'+$80		;DO.
stname_drawto:	dta		c'DRAWT',c'O'+$80	;DR.
stname_setcolor dta		c'SETCOLO',c'R'+$80	;SE.
stname_locate:	dta		c'LOCAT',c'E'+$80	;LOC.
stname_sound:	dta		c'SOUN',c'D'+$80	;SO.
stname_lprint:	dta		c'LPRIN',c'T'+$80	;LP.
stname_csave:	dta		c'CSAV',c'E'+$80
stname_cload:	dta		c'CLOA',c'D'+$80	;CLOA.
				dta		0					;end for searching
		
		_STATIC_ASSERT *-statement_table<255 "Statement table exceeds 256 bytes."
		
.proc stname_table
		dta		stname_rem - statement_table
		dta		stname_data - statement_table
		dta		stname_input - statement_table
		dta		stname_color - statement_table
		dta		stname_list - statement_table
		dta		stname_enter - statement_table
		dta		stname_let - statement_table
		dta		stname_if - statement_table
		dta		stname_for - statement_table
		dta		stname_next - statement_table
		dta		stname_goto - statement_table
		dta		stname_go_to - statement_table
		dta		stname_gosub - statement_table
		dta		stname_trap - statement_table
		dta		stname_bye - statement_table
		dta		stname_cont - statement_table
		dta		stname_com - statement_table
		dta		stname_close - statement_table
		dta		stname_clr - statement_table
		dta		stname_deg - statement_table
		dta		stname_dim - statement_table
		dta		stname_end - statement_table
		dta		stname_new - statement_table
		dta		stname_open - statement_table
		dta		stname_load - statement_table
		dta		stname_save - statement_table
		dta		stname_status - statement_table
		dta		stname_note - statement_table
		dta		stname_point - statement_table
		dta		stname_xio - statement_table
		dta		stname_on - statement_table
		dta		stname_poke - statement_table
		dta		stname_print - statement_table
		dta		stname_rad - statement_table
		dta		stname_read - statement_table
		dta		stname_restore - statement_table
		dta		stname_return - statement_table
		dta		stname_run - statement_table
		dta		stname_stop - statement_table
		dta		stname_pop - statement_table
		dta		stname_qprint - statement_table
		dta		stname_get - statement_table
		dta		stname_put - statement_table
		dta		stname_graphics - statement_table
		dta		stname_plot - statement_table
		dta		stname_position - statement_table
		dta		stname_dos - statement_table
		dta		stname_drawto - statement_table
		dta		stname_setcolor - statement_table
		dta		stname_locate - statement_table
		dta		stname_sound - statement_table
		dta		stname_lprint - statement_table
		dta		stname_csave - statement_table
		dta		stname_cload - statement_table
.endp

.echo "- Parser program length: ",*-?parser_program_start
