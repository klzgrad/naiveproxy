package main

import (
	"fmt"
	"math"
	"sort"
	"strconv"
)

const endSymbol rune = 1114112

/* The rule types inferred from the grammar are below. */
type pegRule uint8

const (
	ruleUnknown pegRule = iota
	ruleAsmFile
	ruleStatement
	ruleGlobalDirective
	ruleDirective
	ruleDirectiveName
	ruleLocationDirective
	ruleFileDirective
	ruleLocDirective
	ruleArgs
	ruleArg
	ruleQuotedArg
	ruleQuotedText
	ruleLabelContainingDirective
	ruleLabelContainingDirectiveName
	ruleSymbolArgs
	ruleSymbolArg
	ruleSymbolType
	ruleDot
	ruleTCMarker
	ruleEscapedChar
	ruleWS
	ruleComment
	ruleLabel
	ruleSymbolName
	ruleLocalSymbol
	ruleLocalLabel
	ruleLocalLabelRef
	ruleInstruction
	ruleInstructionName
	ruleInstructionArg
	ruleGOTLocation
	ruleGOTSymbolOffset
	ruleAVX512Token
	ruleTOCRefHigh
	ruleTOCRefLow
	ruleIndirectionIndicator
	ruleRegisterOrConstant
	ruleARMConstantTweak
	ruleARMRegister
	ruleARMVectorRegister
	ruleMemoryRef
	ruleSymbolRef
	ruleLow12BitsSymbolRef
	ruleARMBaseIndexScale
	ruleARMGOTLow12
	ruleARMPostincrement
	ruleBaseIndexScale
	ruleOperator
	ruleOffset
	ruleSection
	ruleSegmentRegister
)

var rul3s = [...]string{
	"Unknown",
	"AsmFile",
	"Statement",
	"GlobalDirective",
	"Directive",
	"DirectiveName",
	"LocationDirective",
	"FileDirective",
	"LocDirective",
	"Args",
	"Arg",
	"QuotedArg",
	"QuotedText",
	"LabelContainingDirective",
	"LabelContainingDirectiveName",
	"SymbolArgs",
	"SymbolArg",
	"SymbolType",
	"Dot",
	"TCMarker",
	"EscapedChar",
	"WS",
	"Comment",
	"Label",
	"SymbolName",
	"LocalSymbol",
	"LocalLabel",
	"LocalLabelRef",
	"Instruction",
	"InstructionName",
	"InstructionArg",
	"GOTLocation",
	"GOTSymbolOffset",
	"AVX512Token",
	"TOCRefHigh",
	"TOCRefLow",
	"IndirectionIndicator",
	"RegisterOrConstant",
	"ARMConstantTweak",
	"ARMRegister",
	"ARMVectorRegister",
	"MemoryRef",
	"SymbolRef",
	"Low12BitsSymbolRef",
	"ARMBaseIndexScale",
	"ARMGOTLow12",
	"ARMPostincrement",
	"BaseIndexScale",
	"Operator",
	"Offset",
	"Section",
	"SegmentRegister",
}

type token32 struct {
	pegRule
	begin, end uint32
}

func (t *token32) String() string {
	return fmt.Sprintf("\x1B[34m%v\x1B[m %v %v", rul3s[t.pegRule], t.begin, t.end)
}

type node32 struct {
	token32
	up, next *node32
}

func (node *node32) print(pretty bool, buffer string) {
	var print func(node *node32, depth int)
	print = func(node *node32, depth int) {
		for node != nil {
			for c := 0; c < depth; c++ {
				fmt.Printf(" ")
			}
			rule := rul3s[node.pegRule]
			quote := strconv.Quote(string(([]rune(buffer)[node.begin:node.end])))
			if !pretty {
				fmt.Printf("%v %v\n", rule, quote)
			} else {
				fmt.Printf("\x1B[34m%v\x1B[m %v\n", rule, quote)
			}
			if node.up != nil {
				print(node.up, depth+1)
			}
			node = node.next
		}
	}
	print(node, 0)
}

func (node *node32) Print(buffer string) {
	node.print(false, buffer)
}

func (node *node32) PrettyPrint(buffer string) {
	node.print(true, buffer)
}

type tokens32 struct {
	tree []token32
}

func (t *tokens32) Trim(length uint32) {
	t.tree = t.tree[:length]
}

func (t *tokens32) Print() {
	for _, token := range t.tree {
		fmt.Println(token.String())
	}
}

func (t *tokens32) AST() *node32 {
	type element struct {
		node *node32
		down *element
	}
	tokens := t.Tokens()
	var stack *element
	for _, token := range tokens {
		if token.begin == token.end {
			continue
		}
		node := &node32{token32: token}
		for stack != nil && stack.node.begin >= token.begin && stack.node.end <= token.end {
			stack.node.next = node.up
			node.up = stack.node
			stack = stack.down
		}
		stack = &element{node: node, down: stack}
	}
	if stack != nil {
		return stack.node
	}
	return nil
}

func (t *tokens32) PrintSyntaxTree(buffer string) {
	t.AST().Print(buffer)
}

func (t *tokens32) PrettyPrintSyntaxTree(buffer string) {
	t.AST().PrettyPrint(buffer)
}

func (t *tokens32) Add(rule pegRule, begin, end, index uint32) {
	if tree := t.tree; int(index) >= len(tree) {
		expanded := make([]token32, 2*len(tree))
		copy(expanded, tree)
		t.tree = expanded
	}
	t.tree[index] = token32{
		pegRule: rule,
		begin:   begin,
		end:     end,
	}
}

func (t *tokens32) Tokens() []token32 {
	return t.tree
}

type Asm struct {
	Buffer string
	buffer []rune
	rules  [52]func() bool
	parse  func(rule ...int) error
	reset  func()
	Pretty bool
	tokens32
}

func (p *Asm) Parse(rule ...int) error {
	return p.parse(rule...)
}

func (p *Asm) Reset() {
	p.reset()
}

type textPosition struct {
	line, symbol int
}

type textPositionMap map[int]textPosition

func translatePositions(buffer []rune, positions []int) textPositionMap {
	length, translations, j, line, symbol := len(positions), make(textPositionMap, len(positions)), 0, 1, 0
	sort.Ints(positions)

search:
	for i, c := range buffer {
		if c == '\n' {
			line, symbol = line+1, 0
		} else {
			symbol++
		}
		if i == positions[j] {
			translations[positions[j]] = textPosition{line, symbol}
			for j++; j < length; j++ {
				if i != positions[j] {
					continue search
				}
			}
			break search
		}
	}

	return translations
}

type parseError struct {
	p   *Asm
	max token32
}

func (e *parseError) Error() string {
	tokens, error := []token32{e.max}, "\n"
	positions, p := make([]int, 2*len(tokens)), 0
	for _, token := range tokens {
		positions[p], p = int(token.begin), p+1
		positions[p], p = int(token.end), p+1
	}
	translations := translatePositions(e.p.buffer, positions)
	format := "parse error near %v (line %v symbol %v - line %v symbol %v):\n%v\n"
	if e.p.Pretty {
		format = "parse error near \x1B[34m%v\x1B[m (line %v symbol %v - line %v symbol %v):\n%v\n"
	}
	for _, token := range tokens {
		begin, end := int(token.begin), int(token.end)
		error += fmt.Sprintf(format,
			rul3s[token.pegRule],
			translations[begin].line, translations[begin].symbol,
			translations[end].line, translations[end].symbol,
			strconv.Quote(string(e.p.buffer[begin:end])))
	}

	return error
}

func (p *Asm) PrintSyntaxTree() {
	if p.Pretty {
		p.tokens32.PrettyPrintSyntaxTree(p.Buffer)
	} else {
		p.tokens32.PrintSyntaxTree(p.Buffer)
	}
}

func (p *Asm) Init() {
	var (
		max                  token32
		position, tokenIndex uint32
		buffer               []rune
	)
	p.reset = func() {
		max = token32{}
		position, tokenIndex = 0, 0

		p.buffer = []rune(p.Buffer)
		if len(p.buffer) == 0 || p.buffer[len(p.buffer)-1] != endSymbol {
			p.buffer = append(p.buffer, endSymbol)
		}
		buffer = p.buffer
	}
	p.reset()

	_rules := p.rules
	tree := tokens32{tree: make([]token32, math.MaxInt16)}
	p.parse = func(rule ...int) error {
		r := 1
		if len(rule) > 0 {
			r = rule[0]
		}
		matches := p.rules[r]()
		p.tokens32 = tree
		if matches {
			p.Trim(tokenIndex)
			return nil
		}
		return &parseError{p, max}
	}

	add := func(rule pegRule, begin uint32) {
		tree.Add(rule, begin, position, tokenIndex)
		tokenIndex++
		if begin != position && position > max.end {
			max = token32{rule, begin, position}
		}
	}

	matchDot := func() bool {
		if buffer[position] != endSymbol {
			position++
			return true
		}
		return false
	}

	/*matchChar := func(c byte) bool {
		if buffer[position] == c {
			position++
			return true
		}
		return false
	}*/

	/*matchRange := func(lower byte, upper byte) bool {
		if c := buffer[position]; c >= lower && c <= upper {
			position++
			return true
		}
		return false
	}*/

	_rules = [...]func() bool{
		nil,
		/* 0 AsmFile <- <(Statement* !.)> */
		func() bool {
			position0, tokenIndex0 := position, tokenIndex
			{
				position1 := position
			l2:
				{
					position3, tokenIndex3 := position, tokenIndex
					if !_rules[ruleStatement]() {
						goto l3
					}
					goto l2
				l3:
					position, tokenIndex = position3, tokenIndex3
				}
				{
					position4, tokenIndex4 := position, tokenIndex
					if !matchDot() {
						goto l4
					}
					goto l0
				l4:
					position, tokenIndex = position4, tokenIndex4
				}
				add(ruleAsmFile, position1)
			}
			return true
		l0:
			position, tokenIndex = position0, tokenIndex0
			return false
		},
		/* 1 Statement <- <(WS? (Label / ((GlobalDirective / LocationDirective / LabelContainingDirective / Instruction / Directive / Comment / ) WS? ((Comment? '\n') / ';'))))> */
		func() bool {
			position5, tokenIndex5 := position, tokenIndex
			{
				position6 := position
				{
					position7, tokenIndex7 := position, tokenIndex
					if !_rules[ruleWS]() {
						goto l7
					}
					goto l8
				l7:
					position, tokenIndex = position7, tokenIndex7
				}
			l8:
				{
					position9, tokenIndex9 := position, tokenIndex
					if !_rules[ruleLabel]() {
						goto l10
					}
					goto l9
				l10:
					position, tokenIndex = position9, tokenIndex9
					{
						position11, tokenIndex11 := position, tokenIndex
						if !_rules[ruleGlobalDirective]() {
							goto l12
						}
						goto l11
					l12:
						position, tokenIndex = position11, tokenIndex11
						if !_rules[ruleLocationDirective]() {
							goto l13
						}
						goto l11
					l13:
						position, tokenIndex = position11, tokenIndex11
						if !_rules[ruleLabelContainingDirective]() {
							goto l14
						}
						goto l11
					l14:
						position, tokenIndex = position11, tokenIndex11
						if !_rules[ruleInstruction]() {
							goto l15
						}
						goto l11
					l15:
						position, tokenIndex = position11, tokenIndex11
						if !_rules[ruleDirective]() {
							goto l16
						}
						goto l11
					l16:
						position, tokenIndex = position11, tokenIndex11
						if !_rules[ruleComment]() {
							goto l17
						}
						goto l11
					l17:
						position, tokenIndex = position11, tokenIndex11
					}
				l11:
					{
						position18, tokenIndex18 := position, tokenIndex
						if !_rules[ruleWS]() {
							goto l18
						}
						goto l19
					l18:
						position, tokenIndex = position18, tokenIndex18
					}
				l19:
					{
						position20, tokenIndex20 := position, tokenIndex
						{
							position22, tokenIndex22 := position, tokenIndex
							if !_rules[ruleComment]() {
								goto l22
							}
							goto l23
						l22:
							position, tokenIndex = position22, tokenIndex22
						}
					l23:
						if buffer[position] != rune('\n') {
							goto l21
						}
						position++
						goto l20
					l21:
						position, tokenIndex = position20, tokenIndex20
						if buffer[position] != rune(';') {
							goto l5
						}
						position++
					}
				l20:
				}
			l9:
				add(ruleStatement, position6)
			}
			return true
		l5:
			position, tokenIndex = position5, tokenIndex5
			return false
		},
		/* 2 GlobalDirective <- <((('.' ('g' / 'G') ('l' / 'L') ('o' / 'O') ('b' / 'B') ('a' / 'A') ('l' / 'L')) / ('.' ('g' / 'G') ('l' / 'L') ('o' / 'O') ('b' / 'B') ('l' / 'L'))) WS SymbolName)> */
		func() bool {
			position24, tokenIndex24 := position, tokenIndex
			{
				position25 := position
				{
					position26, tokenIndex26 := position, tokenIndex
					if buffer[position] != rune('.') {
						goto l27
					}
					position++
					{
						position28, tokenIndex28 := position, tokenIndex
						if buffer[position] != rune('g') {
							goto l29
						}
						position++
						goto l28
					l29:
						position, tokenIndex = position28, tokenIndex28
						if buffer[position] != rune('G') {
							goto l27
						}
						position++
					}
				l28:
					{
						position30, tokenIndex30 := position, tokenIndex
						if buffer[position] != rune('l') {
							goto l31
						}
						position++
						goto l30
					l31:
						position, tokenIndex = position30, tokenIndex30
						if buffer[position] != rune('L') {
							goto l27
						}
						position++
					}
				l30:
					{
						position32, tokenIndex32 := position, tokenIndex
						if buffer[position] != rune('o') {
							goto l33
						}
						position++
						goto l32
					l33:
						position, tokenIndex = position32, tokenIndex32
						if buffer[position] != rune('O') {
							goto l27
						}
						position++
					}
				l32:
					{
						position34, tokenIndex34 := position, tokenIndex
						if buffer[position] != rune('b') {
							goto l35
						}
						position++
						goto l34
					l35:
						position, tokenIndex = position34, tokenIndex34
						if buffer[position] != rune('B') {
							goto l27
						}
						position++
					}
				l34:
					{
						position36, tokenIndex36 := position, tokenIndex
						if buffer[position] != rune('a') {
							goto l37
						}
						position++
						goto l36
					l37:
						position, tokenIndex = position36, tokenIndex36
						if buffer[position] != rune('A') {
							goto l27
						}
						position++
					}
				l36:
					{
						position38, tokenIndex38 := position, tokenIndex
						if buffer[position] != rune('l') {
							goto l39
						}
						position++
						goto l38
					l39:
						position, tokenIndex = position38, tokenIndex38
						if buffer[position] != rune('L') {
							goto l27
						}
						position++
					}
				l38:
					goto l26
				l27:
					position, tokenIndex = position26, tokenIndex26
					if buffer[position] != rune('.') {
						goto l24
					}
					position++
					{
						position40, tokenIndex40 := position, tokenIndex
						if buffer[position] != rune('g') {
							goto l41
						}
						position++
						goto l40
					l41:
						position, tokenIndex = position40, tokenIndex40
						if buffer[position] != rune('G') {
							goto l24
						}
						position++
					}
				l40:
					{
						position42, tokenIndex42 := position, tokenIndex
						if buffer[position] != rune('l') {
							goto l43
						}
						position++
						goto l42
					l43:
						position, tokenIndex = position42, tokenIndex42
						if buffer[position] != rune('L') {
							goto l24
						}
						position++
					}
				l42:
					{
						position44, tokenIndex44 := position, tokenIndex
						if buffer[position] != rune('o') {
							goto l45
						}
						position++
						goto l44
					l45:
						position, tokenIndex = position44, tokenIndex44
						if buffer[position] != rune('O') {
							goto l24
						}
						position++
					}
				l44:
					{
						position46, tokenIndex46 := position, tokenIndex
						if buffer[position] != rune('b') {
							goto l47
						}
						position++
						goto l46
					l47:
						position, tokenIndex = position46, tokenIndex46
						if buffer[position] != rune('B') {
							goto l24
						}
						position++
					}
				l46:
					{
						position48, tokenIndex48 := position, tokenIndex
						if buffer[position] != rune('l') {
							goto l49
						}
						position++
						goto l48
					l49:
						position, tokenIndex = position48, tokenIndex48
						if buffer[position] != rune('L') {
							goto l24
						}
						position++
					}
				l48:
				}
			l26:
				if !_rules[ruleWS]() {
					goto l24
				}
				if !_rules[ruleSymbolName]() {
					goto l24
				}
				add(ruleGlobalDirective, position25)
			}
			return true
		l24:
			position, tokenIndex = position24, tokenIndex24
			return false
		},
		/* 3 Directive <- <('.' DirectiveName (WS Args)?)> */
		func() bool {
			position50, tokenIndex50 := position, tokenIndex
			{
				position51 := position
				if buffer[position] != rune('.') {
					goto l50
				}
				position++
				if !_rules[ruleDirectiveName]() {
					goto l50
				}
				{
					position52, tokenIndex52 := position, tokenIndex
					if !_rules[ruleWS]() {
						goto l52
					}
					if !_rules[ruleArgs]() {
						goto l52
					}
					goto l53
				l52:
					position, tokenIndex = position52, tokenIndex52
				}
			l53:
				add(ruleDirective, position51)
			}
			return true
		l50:
			position, tokenIndex = position50, tokenIndex50
			return false
		},
		/* 4 DirectiveName <- <([a-z] / [A-Z] / ([0-9] / [0-9]) / '_')+> */
		func() bool {
			position54, tokenIndex54 := position, tokenIndex
			{
				position55 := position
				{
					position58, tokenIndex58 := position, tokenIndex
					if c := buffer[position]; c < rune('a') || c > rune('z') {
						goto l59
					}
					position++
					goto l58
				l59:
					position, tokenIndex = position58, tokenIndex58
					if c := buffer[position]; c < rune('A') || c > rune('Z') {
						goto l60
					}
					position++
					goto l58
				l60:
					position, tokenIndex = position58, tokenIndex58
					{
						position62, tokenIndex62 := position, tokenIndex
						if c := buffer[position]; c < rune('0') || c > rune('9') {
							goto l63
						}
						position++
						goto l62
					l63:
						position, tokenIndex = position62, tokenIndex62
						if c := buffer[position]; c < rune('0') || c > rune('9') {
							goto l61
						}
						position++
					}
				l62:
					goto l58
				l61:
					position, tokenIndex = position58, tokenIndex58
					if buffer[position] != rune('_') {
						goto l54
					}
					position++
				}
			l58:
			l56:
				{
					position57, tokenIndex57 := position, tokenIndex
					{
						position64, tokenIndex64 := position, tokenIndex
						if c := buffer[position]; c < rune('a') || c > rune('z') {
							goto l65
						}
						position++
						goto l64
					l65:
						position, tokenIndex = position64, tokenIndex64
						if c := buffer[position]; c < rune('A') || c > rune('Z') {
							goto l66
						}
						position++
						goto l64
					l66:
						position, tokenIndex = position64, tokenIndex64
						{
							position68, tokenIndex68 := position, tokenIndex
							if c := buffer[position]; c < rune('0') || c > rune('9') {
								goto l69
							}
							position++
							goto l68
						l69:
							position, tokenIndex = position68, tokenIndex68
							if c := buffer[position]; c < rune('0') || c > rune('9') {
								goto l67
							}
							position++
						}
					l68:
						goto l64
					l67:
						position, tokenIndex = position64, tokenIndex64
						if buffer[position] != rune('_') {
							goto l57
						}
						position++
					}
				l64:
					goto l56
				l57:
					position, tokenIndex = position57, tokenIndex57
				}
				add(ruleDirectiveName, position55)
			}
			return true
		l54:
			position, tokenIndex = position54, tokenIndex54
			return false
		},
		/* 5 LocationDirective <- <(FileDirective / LocDirective)> */
		func() bool {
			position70, tokenIndex70 := position, tokenIndex
			{
				position71 := position
				{
					position72, tokenIndex72 := position, tokenIndex
					if !_rules[ruleFileDirective]() {
						goto l73
					}
					goto l72
				l73:
					position, tokenIndex = position72, tokenIndex72
					if !_rules[ruleLocDirective]() {
						goto l70
					}
				}
			l72:
				add(ruleLocationDirective, position71)
			}
			return true
		l70:
			position, tokenIndex = position70, tokenIndex70
			return false
		},
		/* 6 FileDirective <- <('.' ('f' / 'F') ('i' / 'I') ('l' / 'L') ('e' / 'E') WS (!('#' / '\n') .)+)> */
		func() bool {
			position74, tokenIndex74 := position, tokenIndex
			{
				position75 := position
				if buffer[position] != rune('.') {
					goto l74
				}
				position++
				{
					position76, tokenIndex76 := position, tokenIndex
					if buffer[position] != rune('f') {
						goto l77
					}
					position++
					goto l76
				l77:
					position, tokenIndex = position76, tokenIndex76
					if buffer[position] != rune('F') {
						goto l74
					}
					position++
				}
			l76:
				{
					position78, tokenIndex78 := position, tokenIndex
					if buffer[position] != rune('i') {
						goto l79
					}
					position++
					goto l78
				l79:
					position, tokenIndex = position78, tokenIndex78
					if buffer[position] != rune('I') {
						goto l74
					}
					position++
				}
			l78:
				{
					position80, tokenIndex80 := position, tokenIndex
					if buffer[position] != rune('l') {
						goto l81
					}
					position++
					goto l80
				l81:
					position, tokenIndex = position80, tokenIndex80
					if buffer[position] != rune('L') {
						goto l74
					}
					position++
				}
			l80:
				{
					position82, tokenIndex82 := position, tokenIndex
					if buffer[position] != rune('e') {
						goto l83
					}
					position++
					goto l82
				l83:
					position, tokenIndex = position82, tokenIndex82
					if buffer[position] != rune('E') {
						goto l74
					}
					position++
				}
			l82:
				if !_rules[ruleWS]() {
					goto l74
				}
				{
					position86, tokenIndex86 := position, tokenIndex
					{
						position87, tokenIndex87 := position, tokenIndex
						if buffer[position] != rune('#') {
							goto l88
						}
						position++
						goto l87
					l88:
						position, tokenIndex = position87, tokenIndex87
						if buffer[position] != rune('\n') {
							goto l86
						}
						position++
					}
				l87:
					goto l74
				l86:
					position, tokenIndex = position86, tokenIndex86
				}
				if !matchDot() {
					goto l74
				}
			l84:
				{
					position85, tokenIndex85 := position, tokenIndex
					{
						position89, tokenIndex89 := position, tokenIndex
						{
							position90, tokenIndex90 := position, tokenIndex
							if buffer[position] != rune('#') {
								goto l91
							}
							position++
							goto l90
						l91:
							position, tokenIndex = position90, tokenIndex90
							if buffer[position] != rune('\n') {
								goto l89
							}
							position++
						}
					l90:
						goto l85
					l89:
						position, tokenIndex = position89, tokenIndex89
					}
					if !matchDot() {
						goto l85
					}
					goto l84
				l85:
					position, tokenIndex = position85, tokenIndex85
				}
				add(ruleFileDirective, position75)
			}
			return true
		l74:
			position, tokenIndex = position74, tokenIndex74
			return false
		},
		/* 7 LocDirective <- <('.' ('l' / 'L') ('o' / 'O') ('c' / 'C') WS (!('#' / '/' / '\n') .)+)> */
		func() bool {
			position92, tokenIndex92 := position, tokenIndex
			{
				position93 := position
				if buffer[position] != rune('.') {
					goto l92
				}
				position++
				{
					position94, tokenIndex94 := position, tokenIndex
					if buffer[position] != rune('l') {
						goto l95
					}
					position++
					goto l94
				l95:
					position, tokenIndex = position94, tokenIndex94
					if buffer[position] != rune('L') {
						goto l92
					}
					position++
				}
			l94:
				{
					position96, tokenIndex96 := position, tokenIndex
					if buffer[position] != rune('o') {
						goto l97
					}
					position++
					goto l96
				l97:
					position, tokenIndex = position96, tokenIndex96
					if buffer[position] != rune('O') {
						goto l92
					}
					position++
				}
			l96:
				{
					position98, tokenIndex98 := position, tokenIndex
					if buffer[position] != rune('c') {
						goto l99
					}
					position++
					goto l98
				l99:
					position, tokenIndex = position98, tokenIndex98
					if buffer[position] != rune('C') {
						goto l92
					}
					position++
				}
			l98:
				if !_rules[ruleWS]() {
					goto l92
				}
				{
					position102, tokenIndex102 := position, tokenIndex
					{
						position103, tokenIndex103 := position, tokenIndex
						if buffer[position] != rune('#') {
							goto l104
						}
						position++
						goto l103
					l104:
						position, tokenIndex = position103, tokenIndex103
						if buffer[position] != rune('/') {
							goto l105
						}
						position++
						goto l103
					l105:
						position, tokenIndex = position103, tokenIndex103
						if buffer[position] != rune('\n') {
							goto l102
						}
						position++
					}
				l103:
					goto l92
				l102:
					position, tokenIndex = position102, tokenIndex102
				}
				if !matchDot() {
					goto l92
				}
			l100:
				{
					position101, tokenIndex101 := position, tokenIndex
					{
						position106, tokenIndex106 := position, tokenIndex
						{
							position107, tokenIndex107 := position, tokenIndex
							if buffer[position] != rune('#') {
								goto l108
							}
							position++
							goto l107
						l108:
							position, tokenIndex = position107, tokenIndex107
							if buffer[position] != rune('/') {
								goto l109
							}
							position++
							goto l107
						l109:
							position, tokenIndex = position107, tokenIndex107
							if buffer[position] != rune('\n') {
								goto l106
							}
							position++
						}
					l107:
						goto l101
					l106:
						position, tokenIndex = position106, tokenIndex106
					}
					if !matchDot() {
						goto l101
					}
					goto l100
				l101:
					position, tokenIndex = position101, tokenIndex101
				}
				add(ruleLocDirective, position93)
			}
			return true
		l92:
			position, tokenIndex = position92, tokenIndex92
			return false
		},
		/* 8 Args <- <(Arg (WS? ',' WS? Arg)*)> */
		func() bool {
			position110, tokenIndex110 := position, tokenIndex
			{
				position111 := position
				if !_rules[ruleArg]() {
					goto l110
				}
			l112:
				{
					position113, tokenIndex113 := position, tokenIndex
					{
						position114, tokenIndex114 := position, tokenIndex
						if !_rules[ruleWS]() {
							goto l114
						}
						goto l115
					l114:
						position, tokenIndex = position114, tokenIndex114
					}
				l115:
					if buffer[position] != rune(',') {
						goto l113
					}
					position++
					{
						position116, tokenIndex116 := position, tokenIndex
						if !_rules[ruleWS]() {
							goto l116
						}
						goto l117
					l116:
						position, tokenIndex = position116, tokenIndex116
					}
				l117:
					if !_rules[ruleArg]() {
						goto l113
					}
					goto l112
				l113:
					position, tokenIndex = position113, tokenIndex113
				}
				add(ruleArgs, position111)
			}
			return true
		l110:
			position, tokenIndex = position110, tokenIndex110
			return false
		},
		/* 9 Arg <- <(QuotedArg / ([0-9] / [0-9] / ([a-z] / [A-Z]) / '%' / '+' / '-' / '*' / '_' / '@' / '.')*)> */
		func() bool {
			{
				position119 := position
				{
					position120, tokenIndex120 := position, tokenIndex
					if !_rules[ruleQuotedArg]() {
						goto l121
					}
					goto l120
				l121:
					position, tokenIndex = position120, tokenIndex120
				l122:
					{
						position123, tokenIndex123 := position, tokenIndex
						{
							position124, tokenIndex124 := position, tokenIndex
							if c := buffer[position]; c < rune('0') || c > rune('9') {
								goto l125
							}
							position++
							goto l124
						l125:
							position, tokenIndex = position124, tokenIndex124
							if c := buffer[position]; c < rune('0') || c > rune('9') {
								goto l126
							}
							position++
							goto l124
						l126:
							position, tokenIndex = position124, tokenIndex124
							{
								position128, tokenIndex128 := position, tokenIndex
								if c := buffer[position]; c < rune('a') || c > rune('z') {
									goto l129
								}
								position++
								goto l128
							l129:
								position, tokenIndex = position128, tokenIndex128
								if c := buffer[position]; c < rune('A') || c > rune('Z') {
									goto l127
								}
								position++
							}
						l128:
							goto l124
						l127:
							position, tokenIndex = position124, tokenIndex124
							if buffer[position] != rune('%') {
								goto l130
							}
							position++
							goto l124
						l130:
							position, tokenIndex = position124, tokenIndex124
							if buffer[position] != rune('+') {
								goto l131
							}
							position++
							goto l124
						l131:
							position, tokenIndex = position124, tokenIndex124
							if buffer[position] != rune('-') {
								goto l132
							}
							position++
							goto l124
						l132:
							position, tokenIndex = position124, tokenIndex124
							if buffer[position] != rune('*') {
								goto l133
							}
							position++
							goto l124
						l133:
							position, tokenIndex = position124, tokenIndex124
							if buffer[position] != rune('_') {
								goto l134
							}
							position++
							goto l124
						l134:
							position, tokenIndex = position124, tokenIndex124
							if buffer[position] != rune('@') {
								goto l135
							}
							position++
							goto l124
						l135:
							position, tokenIndex = position124, tokenIndex124
							if buffer[position] != rune('.') {
								goto l123
							}
							position++
						}
					l124:
						goto l122
					l123:
						position, tokenIndex = position123, tokenIndex123
					}
				}
			l120:
				add(ruleArg, position119)
			}
			return true
		},
		/* 10 QuotedArg <- <('"' QuotedText '"')> */
		func() bool {
			position136, tokenIndex136 := position, tokenIndex
			{
				position137 := position
				if buffer[position] != rune('"') {
					goto l136
				}
				position++
				if !_rules[ruleQuotedText]() {
					goto l136
				}
				if buffer[position] != rune('"') {
					goto l136
				}
				position++
				add(ruleQuotedArg, position137)
			}
			return true
		l136:
			position, tokenIndex = position136, tokenIndex136
			return false
		},
		/* 11 QuotedText <- <(EscapedChar / (!'"' .))*> */
		func() bool {
			{
				position139 := position
			l140:
				{
					position141, tokenIndex141 := position, tokenIndex
					{
						position142, tokenIndex142 := position, tokenIndex
						if !_rules[ruleEscapedChar]() {
							goto l143
						}
						goto l142
					l143:
						position, tokenIndex = position142, tokenIndex142
						{
							position144, tokenIndex144 := position, tokenIndex
							if buffer[position] != rune('"') {
								goto l144
							}
							position++
							goto l141
						l144:
							position, tokenIndex = position144, tokenIndex144
						}
						if !matchDot() {
							goto l141
						}
					}
				l142:
					goto l140
				l141:
					position, tokenIndex = position141, tokenIndex141
				}
				add(ruleQuotedText, position139)
			}
			return true
		},
		/* 12 LabelContainingDirective <- <(LabelContainingDirectiveName WS SymbolArgs)> */
		func() bool {
			position145, tokenIndex145 := position, tokenIndex
			{
				position146 := position
				if !_rules[ruleLabelContainingDirectiveName]() {
					goto l145
				}
				if !_rules[ruleWS]() {
					goto l145
				}
				if !_rules[ruleSymbolArgs]() {
					goto l145
				}
				add(ruleLabelContainingDirective, position146)
			}
			return true
		l145:
			position, tokenIndex = position145, tokenIndex145
			return false
		},
		/* 13 LabelContainingDirectiveName <- <(('.' ('x' / 'X') ('w' / 'W') ('o' / 'O') ('r' / 'R') ('d' / 'D')) / ('.' ('w' / 'W') ('o' / 'O') ('r' / 'R') ('d' / 'D')) / ('.' ('l' / 'L') ('o' / 'O') ('n' / 'N') ('g' / 'G')) / ('.' ('s' / 'S') ('e' / 'E') ('t' / 'T')) / ('.' '8' ('b' / 'B') ('y' / 'Y') ('t' / 'T') ('e' / 'E')) / ('.' '4' ('b' / 'B') ('y' / 'Y') ('t' / 'T') ('e' / 'E')) / ('.' ('q' / 'Q') ('u' / 'U') ('a' / 'A') ('d' / 'D')) / ('.' ('t' / 'T') ('c' / 'C')) / ('.' ('l' / 'L') ('o' / 'O') ('c' / 'C') ('a' / 'A') ('l' / 'L') ('e' / 'E') ('n' / 'N') ('t' / 'T') ('r' / 'R') ('y' / 'Y')) / ('.' ('s' / 'S') ('i' / 'I') ('z' / 'Z') ('e' / 'E')) / ('.' ('t' / 'T') ('y' / 'Y') ('p' / 'P') ('e' / 'E')) / ('.' ('u' / 'U') ('l' / 'L') ('e' / 'E') ('b' / 'B') '1' '2' '8') / ('.' ('s' / 'S') ('l' / 'L') ('e' / 'E') ('b' / 'B') '1' '2' '8'))> */
		func() bool {
			position147, tokenIndex147 := position, tokenIndex
			{
				position148 := position
				{
					position149, tokenIndex149 := position, tokenIndex
					if buffer[position] != rune('.') {
						goto l150
					}
					position++
					{
						position151, tokenIndex151 := position, tokenIndex
						if buffer[position] != rune('x') {
							goto l152
						}
						position++
						goto l151
					l152:
						position, tokenIndex = position151, tokenIndex151
						if buffer[position] != rune('X') {
							goto l150
						}
						position++
					}
				l151:
					{
						position153, tokenIndex153 := position, tokenIndex
						if buffer[position] != rune('w') {
							goto l154
						}
						position++
						goto l153
					l154:
						position, tokenIndex = position153, tokenIndex153
						if buffer[position] != rune('W') {
							goto l150
						}
						position++
					}
				l153:
					{
						position155, tokenIndex155 := position, tokenIndex
						if buffer[position] != rune('o') {
							goto l156
						}
						position++
						goto l155
					l156:
						position, tokenIndex = position155, tokenIndex155
						if buffer[position] != rune('O') {
							goto l150
						}
						position++
					}
				l155:
					{
						position157, tokenIndex157 := position, tokenIndex
						if buffer[position] != rune('r') {
							goto l158
						}
						position++
						goto l157
					l158:
						position, tokenIndex = position157, tokenIndex157
						if buffer[position] != rune('R') {
							goto l150
						}
						position++
					}
				l157:
					{
						position159, tokenIndex159 := position, tokenIndex
						if buffer[position] != rune('d') {
							goto l160
						}
						position++
						goto l159
					l160:
						position, tokenIndex = position159, tokenIndex159
						if buffer[position] != rune('D') {
							goto l150
						}
						position++
					}
				l159:
					goto l149
				l150:
					position, tokenIndex = position149, tokenIndex149
					if buffer[position] != rune('.') {
						goto l161
					}
					position++
					{
						position162, tokenIndex162 := position, tokenIndex
						if buffer[position] != rune('w') {
							goto l163
						}
						position++
						goto l162
					l163:
						position, tokenIndex = position162, tokenIndex162
						if buffer[position] != rune('W') {
							goto l161
						}
						position++
					}
				l162:
					{
						position164, tokenIndex164 := position, tokenIndex
						if buffer[position] != rune('o') {
							goto l165
						}
						position++
						goto l164
					l165:
						position, tokenIndex = position164, tokenIndex164
						if buffer[position] != rune('O') {
							goto l161
						}
						position++
					}
				l164:
					{
						position166, tokenIndex166 := position, tokenIndex
						if buffer[position] != rune('r') {
							goto l167
						}
						position++
						goto l166
					l167:
						position, tokenIndex = position166, tokenIndex166
						if buffer[position] != rune('R') {
							goto l161
						}
						position++
					}
				l166:
					{
						position168, tokenIndex168 := position, tokenIndex
						if buffer[position] != rune('d') {
							goto l169
						}
						position++
						goto l168
					l169:
						position, tokenIndex = position168, tokenIndex168
						if buffer[position] != rune('D') {
							goto l161
						}
						position++
					}
				l168:
					goto l149
				l161:
					position, tokenIndex = position149, tokenIndex149
					if buffer[position] != rune('.') {
						goto l170
					}
					position++
					{
						position171, tokenIndex171 := position, tokenIndex
						if buffer[position] != rune('l') {
							goto l172
						}
						position++
						goto l171
					l172:
						position, tokenIndex = position171, tokenIndex171
						if buffer[position] != rune('L') {
							goto l170
						}
						position++
					}
				l171:
					{
						position173, tokenIndex173 := position, tokenIndex
						if buffer[position] != rune('o') {
							goto l174
						}
						position++
						goto l173
					l174:
						position, tokenIndex = position173, tokenIndex173
						if buffer[position] != rune('O') {
							goto l170
						}
						position++
					}
				l173:
					{
						position175, tokenIndex175 := position, tokenIndex
						if buffer[position] != rune('n') {
							goto l176
						}
						position++
						goto l175
					l176:
						position, tokenIndex = position175, tokenIndex175
						if buffer[position] != rune('N') {
							goto l170
						}
						position++
					}
				l175:
					{
						position177, tokenIndex177 := position, tokenIndex
						if buffer[position] != rune('g') {
							goto l178
						}
						position++
						goto l177
					l178:
						position, tokenIndex = position177, tokenIndex177
						if buffer[position] != rune('G') {
							goto l170
						}
						position++
					}
				l177:
					goto l149
				l170:
					position, tokenIndex = position149, tokenIndex149
					if buffer[position] != rune('.') {
						goto l179
					}
					position++
					{
						position180, tokenIndex180 := position, tokenIndex
						if buffer[position] != rune('s') {
							goto l181
						}
						position++
						goto l180
					l181:
						position, tokenIndex = position180, tokenIndex180
						if buffer[position] != rune('S') {
							goto l179
						}
						position++
					}
				l180:
					{
						position182, tokenIndex182 := position, tokenIndex
						if buffer[position] != rune('e') {
							goto l183
						}
						position++
						goto l182
					l183:
						position, tokenIndex = position182, tokenIndex182
						if buffer[position] != rune('E') {
							goto l179
						}
						position++
					}
				l182:
					{
						position184, tokenIndex184 := position, tokenIndex
						if buffer[position] != rune('t') {
							goto l185
						}
						position++
						goto l184
					l185:
						position, tokenIndex = position184, tokenIndex184
						if buffer[position] != rune('T') {
							goto l179
						}
						position++
					}
				l184:
					goto l149
				l179:
					position, tokenIndex = position149, tokenIndex149
					if buffer[position] != rune('.') {
						goto l186
					}
					position++
					if buffer[position] != rune('8') {
						goto l186
					}
					position++
					{
						position187, tokenIndex187 := position, tokenIndex
						if buffer[position] != rune('b') {
							goto l188
						}
						position++
						goto l187
					l188:
						position, tokenIndex = position187, tokenIndex187
						if buffer[position] != rune('B') {
							goto l186
						}
						position++
					}
				l187:
					{
						position189, tokenIndex189 := position, tokenIndex
						if buffer[position] != rune('y') {
							goto l190
						}
						position++
						goto l189
					l190:
						position, tokenIndex = position189, tokenIndex189
						if buffer[position] != rune('Y') {
							goto l186
						}
						position++
					}
				l189:
					{
						position191, tokenIndex191 := position, tokenIndex
						if buffer[position] != rune('t') {
							goto l192
						}
						position++
						goto l191
					l192:
						position, tokenIndex = position191, tokenIndex191
						if buffer[position] != rune('T') {
							goto l186
						}
						position++
					}
				l191:
					{
						position193, tokenIndex193 := position, tokenIndex
						if buffer[position] != rune('e') {
							goto l194
						}
						position++
						goto l193
					l194:
						position, tokenIndex = position193, tokenIndex193
						if buffer[position] != rune('E') {
							goto l186
						}
						position++
					}
				l193:
					goto l149
				l186:
					position, tokenIndex = position149, tokenIndex149
					if buffer[position] != rune('.') {
						goto l195
					}
					position++
					if buffer[position] != rune('4') {
						goto l195
					}
					position++
					{
						position196, tokenIndex196 := position, tokenIndex
						if buffer[position] != rune('b') {
							goto l197
						}
						position++
						goto l196
					l197:
						position, tokenIndex = position196, tokenIndex196
						if buffer[position] != rune('B') {
							goto l195
						}
						position++
					}
				l196:
					{
						position198, tokenIndex198 := position, tokenIndex
						if buffer[position] != rune('y') {
							goto l199
						}
						position++
						goto l198
					l199:
						position, tokenIndex = position198, tokenIndex198
						if buffer[position] != rune('Y') {
							goto l195
						}
						position++
					}
				l198:
					{
						position200, tokenIndex200 := position, tokenIndex
						if buffer[position] != rune('t') {
							goto l201
						}
						position++
						goto l200
					l201:
						position, tokenIndex = position200, tokenIndex200
						if buffer[position] != rune('T') {
							goto l195
						}
						position++
					}
				l200:
					{
						position202, tokenIndex202 := position, tokenIndex
						if buffer[position] != rune('e') {
							goto l203
						}
						position++
						goto l202
					l203:
						position, tokenIndex = position202, tokenIndex202
						if buffer[position] != rune('E') {
							goto l195
						}
						position++
					}
				l202:
					goto l149
				l195:
					position, tokenIndex = position149, tokenIndex149
					if buffer[position] != rune('.') {
						goto l204
					}
					position++
					{
						position205, tokenIndex205 := position, tokenIndex
						if buffer[position] != rune('q') {
							goto l206
						}
						position++
						goto l205
					l206:
						position, tokenIndex = position205, tokenIndex205
						if buffer[position] != rune('Q') {
							goto l204
						}
						position++
					}
				l205:
					{
						position207, tokenIndex207 := position, tokenIndex
						if buffer[position] != rune('u') {
							goto l208
						}
						position++
						goto l207
					l208:
						position, tokenIndex = position207, tokenIndex207
						if buffer[position] != rune('U') {
							goto l204
						}
						position++
					}
				l207:
					{
						position209, tokenIndex209 := position, tokenIndex
						if buffer[position] != rune('a') {
							goto l210
						}
						position++
						goto l209
					l210:
						position, tokenIndex = position209, tokenIndex209
						if buffer[position] != rune('A') {
							goto l204
						}
						position++
					}
				l209:
					{
						position211, tokenIndex211 := position, tokenIndex
						if buffer[position] != rune('d') {
							goto l212
						}
						position++
						goto l211
					l212:
						position, tokenIndex = position211, tokenIndex211
						if buffer[position] != rune('D') {
							goto l204
						}
						position++
					}
				l211:
					goto l149
				l204:
					position, tokenIndex = position149, tokenIndex149
					if buffer[position] != rune('.') {
						goto l213
					}
					position++
					{
						position214, tokenIndex214 := position, tokenIndex
						if buffer[position] != rune('t') {
							goto l215
						}
						position++
						goto l214
					l215:
						position, tokenIndex = position214, tokenIndex214
						if buffer[position] != rune('T') {
							goto l213
						}
						position++
					}
				l214:
					{
						position216, tokenIndex216 := position, tokenIndex
						if buffer[position] != rune('c') {
							goto l217
						}
						position++
						goto l216
					l217:
						position, tokenIndex = position216, tokenIndex216
						if buffer[position] != rune('C') {
							goto l213
						}
						position++
					}
				l216:
					goto l149
				l213:
					position, tokenIndex = position149, tokenIndex149
					if buffer[position] != rune('.') {
						goto l218
					}
					position++
					{
						position219, tokenIndex219 := position, tokenIndex
						if buffer[position] != rune('l') {
							goto l220
						}
						position++
						goto l219
					l220:
						position, tokenIndex = position219, tokenIndex219
						if buffer[position] != rune('L') {
							goto l218
						}
						position++
					}
				l219:
					{
						position221, tokenIndex221 := position, tokenIndex
						if buffer[position] != rune('o') {
							goto l222
						}
						position++
						goto l221
					l222:
						position, tokenIndex = position221, tokenIndex221
						if buffer[position] != rune('O') {
							goto l218
						}
						position++
					}
				l221:
					{
						position223, tokenIndex223 := position, tokenIndex
						if buffer[position] != rune('c') {
							goto l224
						}
						position++
						goto l223
					l224:
						position, tokenIndex = position223, tokenIndex223
						if buffer[position] != rune('C') {
							goto l218
						}
						position++
					}
				l223:
					{
						position225, tokenIndex225 := position, tokenIndex
						if buffer[position] != rune('a') {
							goto l226
						}
						position++
						goto l225
					l226:
						position, tokenIndex = position225, tokenIndex225
						if buffer[position] != rune('A') {
							goto l218
						}
						position++
					}
				l225:
					{
						position227, tokenIndex227 := position, tokenIndex
						if buffer[position] != rune('l') {
							goto l228
						}
						position++
						goto l227
					l228:
						position, tokenIndex = position227, tokenIndex227
						if buffer[position] != rune('L') {
							goto l218
						}
						position++
					}
				l227:
					{
						position229, tokenIndex229 := position, tokenIndex
						if buffer[position] != rune('e') {
							goto l230
						}
						position++
						goto l229
					l230:
						position, tokenIndex = position229, tokenIndex229
						if buffer[position] != rune('E') {
							goto l218
						}
						position++
					}
				l229:
					{
						position231, tokenIndex231 := position, tokenIndex
						if buffer[position] != rune('n') {
							goto l232
						}
						position++
						goto l231
					l232:
						position, tokenIndex = position231, tokenIndex231
						if buffer[position] != rune('N') {
							goto l218
						}
						position++
					}
				l231:
					{
						position233, tokenIndex233 := position, tokenIndex
						if buffer[position] != rune('t') {
							goto l234
						}
						position++
						goto l233
					l234:
						position, tokenIndex = position233, tokenIndex233
						if buffer[position] != rune('T') {
							goto l218
						}
						position++
					}
				l233:
					{
						position235, tokenIndex235 := position, tokenIndex
						if buffer[position] != rune('r') {
							goto l236
						}
						position++
						goto l235
					l236:
						position, tokenIndex = position235, tokenIndex235
						if buffer[position] != rune('R') {
							goto l218
						}
						position++
					}
				l235:
					{
						position237, tokenIndex237 := position, tokenIndex
						if buffer[position] != rune('y') {
							goto l238
						}
						position++
						goto l237
					l238:
						position, tokenIndex = position237, tokenIndex237
						if buffer[position] != rune('Y') {
							goto l218
						}
						position++
					}
				l237:
					goto l149
				l218:
					position, tokenIndex = position149, tokenIndex149
					if buffer[position] != rune('.') {
						goto l239
					}
					position++
					{
						position240, tokenIndex240 := position, tokenIndex
						if buffer[position] != rune('s') {
							goto l241
						}
						position++
						goto l240
					l241:
						position, tokenIndex = position240, tokenIndex240
						if buffer[position] != rune('S') {
							goto l239
						}
						position++
					}
				l240:
					{
						position242, tokenIndex242 := position, tokenIndex
						if buffer[position] != rune('i') {
							goto l243
						}
						position++
						goto l242
					l243:
						position, tokenIndex = position242, tokenIndex242
						if buffer[position] != rune('I') {
							goto l239
						}
						position++
					}
				l242:
					{
						position244, tokenIndex244 := position, tokenIndex
						if buffer[position] != rune('z') {
							goto l245
						}
						position++
						goto l244
					l245:
						position, tokenIndex = position244, tokenIndex244
						if buffer[position] != rune('Z') {
							goto l239
						}
						position++
					}
				l244:
					{
						position246, tokenIndex246 := position, tokenIndex
						if buffer[position] != rune('e') {
							goto l247
						}
						position++
						goto l246
					l247:
						position, tokenIndex = position246, tokenIndex246
						if buffer[position] != rune('E') {
							goto l239
						}
						position++
					}
				l246:
					goto l149
				l239:
					position, tokenIndex = position149, tokenIndex149
					if buffer[position] != rune('.') {
						goto l248
					}
					position++
					{
						position249, tokenIndex249 := position, tokenIndex
						if buffer[position] != rune('t') {
							goto l250
						}
						position++
						goto l249
					l250:
						position, tokenIndex = position249, tokenIndex249
						if buffer[position] != rune('T') {
							goto l248
						}
						position++
					}
				l249:
					{
						position251, tokenIndex251 := position, tokenIndex
						if buffer[position] != rune('y') {
							goto l252
						}
						position++
						goto l251
					l252:
						position, tokenIndex = position251, tokenIndex251
						if buffer[position] != rune('Y') {
							goto l248
						}
						position++
					}
				l251:
					{
						position253, tokenIndex253 := position, tokenIndex
						if buffer[position] != rune('p') {
							goto l254
						}
						position++
						goto l253
					l254:
						position, tokenIndex = position253, tokenIndex253
						if buffer[position] != rune('P') {
							goto l248
						}
						position++
					}
				l253:
					{
						position255, tokenIndex255 := position, tokenIndex
						if buffer[position] != rune('e') {
							goto l256
						}
						position++
						goto l255
					l256:
						position, tokenIndex = position255, tokenIndex255
						if buffer[position] != rune('E') {
							goto l248
						}
						position++
					}
				l255:
					goto l149
				l248:
					position, tokenIndex = position149, tokenIndex149
					if buffer[position] != rune('.') {
						goto l257
					}
					position++
					{
						position258, tokenIndex258 := position, tokenIndex
						if buffer[position] != rune('u') {
							goto l259
						}
						position++
						goto l258
					l259:
						position, tokenIndex = position258, tokenIndex258
						if buffer[position] != rune('U') {
							goto l257
						}
						position++
					}
				l258:
					{
						position260, tokenIndex260 := position, tokenIndex
						if buffer[position] != rune('l') {
							goto l261
						}
						position++
						goto l260
					l261:
						position, tokenIndex = position260, tokenIndex260
						if buffer[position] != rune('L') {
							goto l257
						}
						position++
					}
				l260:
					{
						position262, tokenIndex262 := position, tokenIndex
						if buffer[position] != rune('e') {
							goto l263
						}
						position++
						goto l262
					l263:
						position, tokenIndex = position262, tokenIndex262
						if buffer[position] != rune('E') {
							goto l257
						}
						position++
					}
				l262:
					{
						position264, tokenIndex264 := position, tokenIndex
						if buffer[position] != rune('b') {
							goto l265
						}
						position++
						goto l264
					l265:
						position, tokenIndex = position264, tokenIndex264
						if buffer[position] != rune('B') {
							goto l257
						}
						position++
					}
				l264:
					if buffer[position] != rune('1') {
						goto l257
					}
					position++
					if buffer[position] != rune('2') {
						goto l257
					}
					position++
					if buffer[position] != rune('8') {
						goto l257
					}
					position++
					goto l149
				l257:
					position, tokenIndex = position149, tokenIndex149
					if buffer[position] != rune('.') {
						goto l147
					}
					position++
					{
						position266, tokenIndex266 := position, tokenIndex
						if buffer[position] != rune('s') {
							goto l267
						}
						position++
						goto l266
					l267:
						position, tokenIndex = position266, tokenIndex266
						if buffer[position] != rune('S') {
							goto l147
						}
						position++
					}
				l266:
					{
						position268, tokenIndex268 := position, tokenIndex
						if buffer[position] != rune('l') {
							goto l269
						}
						position++
						goto l268
					l269:
						position, tokenIndex = position268, tokenIndex268
						if buffer[position] != rune('L') {
							goto l147
						}
						position++
					}
				l268:
					{
						position270, tokenIndex270 := position, tokenIndex
						if buffer[position] != rune('e') {
							goto l271
						}
						position++
						goto l270
					l271:
						position, tokenIndex = position270, tokenIndex270
						if buffer[position] != rune('E') {
							goto l147
						}
						position++
					}
				l270:
					{
						position272, tokenIndex272 := position, tokenIndex
						if buffer[position] != rune('b') {
							goto l273
						}
						position++
						goto l272
					l273:
						position, tokenIndex = position272, tokenIndex272
						if buffer[position] != rune('B') {
							goto l147
						}
						position++
					}
				l272:
					if buffer[position] != rune('1') {
						goto l147
					}
					position++
					if buffer[position] != rune('2') {
						goto l147
					}
					position++
					if buffer[position] != rune('8') {
						goto l147
					}
					position++
				}
			l149:
				add(ruleLabelContainingDirectiveName, position148)
			}
			return true
		l147:
			position, tokenIndex = position147, tokenIndex147
			return false
		},
		/* 14 SymbolArgs <- <(SymbolArg (WS? ',' WS? SymbolArg)*)> */
		func() bool {
			position274, tokenIndex274 := position, tokenIndex
			{
				position275 := position
				if !_rules[ruleSymbolArg]() {
					goto l274
				}
			l276:
				{
					position277, tokenIndex277 := position, tokenIndex
					{
						position278, tokenIndex278 := position, tokenIndex
						if !_rules[ruleWS]() {
							goto l278
						}
						goto l279
					l278:
						position, tokenIndex = position278, tokenIndex278
					}
				l279:
					if buffer[position] != rune(',') {
						goto l277
					}
					position++
					{
						position280, tokenIndex280 := position, tokenIndex
						if !_rules[ruleWS]() {
							goto l280
						}
						goto l281
					l280:
						position, tokenIndex = position280, tokenIndex280
					}
				l281:
					if !_rules[ruleSymbolArg]() {
						goto l277
					}
					goto l276
				l277:
					position, tokenIndex = position277, tokenIndex277
				}
				add(ruleSymbolArgs, position275)
			}
			return true
		l274:
			position, tokenIndex = position274, tokenIndex274
			return false
		},
		/* 15 SymbolArg <- <(Offset / SymbolType / ((Offset / LocalSymbol / SymbolName / Dot) WS? Operator WS? (Offset / LocalSymbol / SymbolName)) / (LocalSymbol TCMarker?) / (SymbolName Offset) / (SymbolName TCMarker?))> */
		func() bool {
			position282, tokenIndex282 := position, tokenIndex
			{
				position283 := position
				{
					position284, tokenIndex284 := position, tokenIndex
					if !_rules[ruleOffset]() {
						goto l285
					}
					goto l284
				l285:
					position, tokenIndex = position284, tokenIndex284
					if !_rules[ruleSymbolType]() {
						goto l286
					}
					goto l284
				l286:
					position, tokenIndex = position284, tokenIndex284
					{
						position288, tokenIndex288 := position, tokenIndex
						if !_rules[ruleOffset]() {
							goto l289
						}
						goto l288
					l289:
						position, tokenIndex = position288, tokenIndex288
						if !_rules[ruleLocalSymbol]() {
							goto l290
						}
						goto l288
					l290:
						position, tokenIndex = position288, tokenIndex288
						if !_rules[ruleSymbolName]() {
							goto l291
						}
						goto l288
					l291:
						position, tokenIndex = position288, tokenIndex288
						if !_rules[ruleDot]() {
							goto l287
						}
					}
				l288:
					{
						position292, tokenIndex292 := position, tokenIndex
						if !_rules[ruleWS]() {
							goto l292
						}
						goto l293
					l292:
						position, tokenIndex = position292, tokenIndex292
					}
				l293:
					if !_rules[ruleOperator]() {
						goto l287
					}
					{
						position294, tokenIndex294 := position, tokenIndex
						if !_rules[ruleWS]() {
							goto l294
						}
						goto l295
					l294:
						position, tokenIndex = position294, tokenIndex294
					}
				l295:
					{
						position296, tokenIndex296 := position, tokenIndex
						if !_rules[ruleOffset]() {
							goto l297
						}
						goto l296
					l297:
						position, tokenIndex = position296, tokenIndex296
						if !_rules[ruleLocalSymbol]() {
							goto l298
						}
						goto l296
					l298:
						position, tokenIndex = position296, tokenIndex296
						if !_rules[ruleSymbolName]() {
							goto l287
						}
					}
				l296:
					goto l284
				l287:
					position, tokenIndex = position284, tokenIndex284
					if !_rules[ruleLocalSymbol]() {
						goto l299
					}
					{
						position300, tokenIndex300 := position, tokenIndex
						if !_rules[ruleTCMarker]() {
							goto l300
						}
						goto l301
					l300:
						position, tokenIndex = position300, tokenIndex300
					}
				l301:
					goto l284
				l299:
					position, tokenIndex = position284, tokenIndex284
					if !_rules[ruleSymbolName]() {
						goto l302
					}
					if !_rules[ruleOffset]() {
						goto l302
					}
					goto l284
				l302:
					position, tokenIndex = position284, tokenIndex284
					if !_rules[ruleSymbolName]() {
						goto l282
					}
					{
						position303, tokenIndex303 := position, tokenIndex
						if !_rules[ruleTCMarker]() {
							goto l303
						}
						goto l304
					l303:
						position, tokenIndex = position303, tokenIndex303
					}
				l304:
				}
			l284:
				add(ruleSymbolArg, position283)
			}
			return true
		l282:
			position, tokenIndex = position282, tokenIndex282
			return false
		},
		/* 16 SymbolType <- <(('@' / '%') (('f' 'u' 'n' 'c' 't' 'i' 'o' 'n') / ('o' 'b' 'j' 'e' 'c' 't')))> */
		func() bool {
			position305, tokenIndex305 := position, tokenIndex
			{
				position306 := position
				{
					position307, tokenIndex307 := position, tokenIndex
					if buffer[position] != rune('@') {
						goto l308
					}
					position++
					goto l307
				l308:
					position, tokenIndex = position307, tokenIndex307
					if buffer[position] != rune('%') {
						goto l305
					}
					position++
				}
			l307:
				{
					position309, tokenIndex309 := position, tokenIndex
					if buffer[position] != rune('f') {
						goto l310
					}
					position++
					if buffer[position] != rune('u') {
						goto l310
					}
					position++
					if buffer[position] != rune('n') {
						goto l310
					}
					position++
					if buffer[position] != rune('c') {
						goto l310
					}
					position++
					if buffer[position] != rune('t') {
						goto l310
					}
					position++
					if buffer[position] != rune('i') {
						goto l310
					}
					position++
					if buffer[position] != rune('o') {
						goto l310
					}
					position++
					if buffer[position] != rune('n') {
						goto l310
					}
					position++
					goto l309
				l310:
					position, tokenIndex = position309, tokenIndex309
					if buffer[position] != rune('o') {
						goto l305
					}
					position++
					if buffer[position] != rune('b') {
						goto l305
					}
					position++
					if buffer[position] != rune('j') {
						goto l305
					}
					position++
					if buffer[position] != rune('e') {
						goto l305
					}
					position++
					if buffer[position] != rune('c') {
						goto l305
					}
					position++
					if buffer[position] != rune('t') {
						goto l305
					}
					position++
				}
			l309:
				add(ruleSymbolType, position306)
			}
			return true
		l305:
			position, tokenIndex = position305, tokenIndex305
			return false
		},
		/* 17 Dot <- <'.'> */
		func() bool {
			position311, tokenIndex311 := position, tokenIndex
			{
				position312 := position
				if buffer[position] != rune('.') {
					goto l311
				}
				position++
				add(ruleDot, position312)
			}
			return true
		l311:
			position, tokenIndex = position311, tokenIndex311
			return false
		},
		/* 18 TCMarker <- <('[' 'T' 'C' ']')> */
		func() bool {
			position313, tokenIndex313 := position, tokenIndex
			{
				position314 := position
				if buffer[position] != rune('[') {
					goto l313
				}
				position++
				if buffer[position] != rune('T') {
					goto l313
				}
				position++
				if buffer[position] != rune('C') {
					goto l313
				}
				position++
				if buffer[position] != rune(']') {
					goto l313
				}
				position++
				add(ruleTCMarker, position314)
			}
			return true
		l313:
			position, tokenIndex = position313, tokenIndex313
			return false
		},
		/* 19 EscapedChar <- <('\\' .)> */
		func() bool {
			position315, tokenIndex315 := position, tokenIndex
			{
				position316 := position
				if buffer[position] != rune('\\') {
					goto l315
				}
				position++
				if !matchDot() {
					goto l315
				}
				add(ruleEscapedChar, position316)
			}
			return true
		l315:
			position, tokenIndex = position315, tokenIndex315
			return false
		},
		/* 20 WS <- <(' ' / '\t')+> */
		func() bool {
			position317, tokenIndex317 := position, tokenIndex
			{
				position318 := position
				{
					position321, tokenIndex321 := position, tokenIndex
					if buffer[position] != rune(' ') {
						goto l322
					}
					position++
					goto l321
				l322:
					position, tokenIndex = position321, tokenIndex321
					if buffer[position] != rune('\t') {
						goto l317
					}
					position++
				}
			l321:
			l319:
				{
					position320, tokenIndex320 := position, tokenIndex
					{
						position323, tokenIndex323 := position, tokenIndex
						if buffer[position] != rune(' ') {
							goto l324
						}
						position++
						goto l323
					l324:
						position, tokenIndex = position323, tokenIndex323
						if buffer[position] != rune('\t') {
							goto l320
						}
						position++
					}
				l323:
					goto l319
				l320:
					position, tokenIndex = position320, tokenIndex320
				}
				add(ruleWS, position318)
			}
			return true
		l317:
			position, tokenIndex = position317, tokenIndex317
			return false
		},
		/* 21 Comment <- <((('/' '/') / '#') (!'\n' .)*)> */
		func() bool {
			position325, tokenIndex325 := position, tokenIndex
			{
				position326 := position
				{
					position327, tokenIndex327 := position, tokenIndex
					if buffer[position] != rune('/') {
						goto l328
					}
					position++
					if buffer[position] != rune('/') {
						goto l328
					}
					position++
					goto l327
				l328:
					position, tokenIndex = position327, tokenIndex327
					if buffer[position] != rune('#') {
						goto l325
					}
					position++
				}
			l327:
			l329:
				{
					position330, tokenIndex330 := position, tokenIndex
					{
						position331, tokenIndex331 := position, tokenIndex
						if buffer[position] != rune('\n') {
							goto l331
						}
						position++
						goto l330
					l331:
						position, tokenIndex = position331, tokenIndex331
					}
					if !matchDot() {
						goto l330
					}
					goto l329
				l330:
					position, tokenIndex = position330, tokenIndex330
				}
				add(ruleComment, position326)
			}
			return true
		l325:
			position, tokenIndex = position325, tokenIndex325
			return false
		},
		/* 22 Label <- <((LocalSymbol / LocalLabel / SymbolName) ':')> */
		func() bool {
			position332, tokenIndex332 := position, tokenIndex
			{
				position333 := position
				{
					position334, tokenIndex334 := position, tokenIndex
					if !_rules[ruleLocalSymbol]() {
						goto l335
					}
					goto l334
				l335:
					position, tokenIndex = position334, tokenIndex334
					if !_rules[ruleLocalLabel]() {
						goto l336
					}
					goto l334
				l336:
					position, tokenIndex = position334, tokenIndex334
					if !_rules[ruleSymbolName]() {
						goto l332
					}
				}
			l334:
				if buffer[position] != rune(':') {
					goto l332
				}
				position++
				add(ruleLabel, position333)
			}
			return true
		l332:
			position, tokenIndex = position332, tokenIndex332
			return false
		},
		/* 23 SymbolName <- <(([a-z] / [A-Z] / '.' / '_') ([a-z] / [A-Z] / '.' / ([0-9] / [0-9]) / '$' / '_')*)> */
		func() bool {
			position337, tokenIndex337 := position, tokenIndex
			{
				position338 := position
				{
					position339, tokenIndex339 := position, tokenIndex
					if c := buffer[position]; c < rune('a') || c > rune('z') {
						goto l340
					}
					position++
					goto l339
				l340:
					position, tokenIndex = position339, tokenIndex339
					if c := buffer[position]; c < rune('A') || c > rune('Z') {
						goto l341
					}
					position++
					goto l339
				l341:
					position, tokenIndex = position339, tokenIndex339
					if buffer[position] != rune('.') {
						goto l342
					}
					position++
					goto l339
				l342:
					position, tokenIndex = position339, tokenIndex339
					if buffer[position] != rune('_') {
						goto l337
					}
					position++
				}
			l339:
			l343:
				{
					position344, tokenIndex344 := position, tokenIndex
					{
						position345, tokenIndex345 := position, tokenIndex
						if c := buffer[position]; c < rune('a') || c > rune('z') {
							goto l346
						}
						position++
						goto l345
					l346:
						position, tokenIndex = position345, tokenIndex345
						if c := buffer[position]; c < rune('A') || c > rune('Z') {
							goto l347
						}
						position++
						goto l345
					l347:
						position, tokenIndex = position345, tokenIndex345
						if buffer[position] != rune('.') {
							goto l348
						}
						position++
						goto l345
					l348:
						position, tokenIndex = position345, tokenIndex345
						{
							position350, tokenIndex350 := position, tokenIndex
							if c := buffer[position]; c < rune('0') || c > rune('9') {
								goto l351
							}
							position++
							goto l350
						l351:
							position, tokenIndex = position350, tokenIndex350
							if c := buffer[position]; c < rune('0') || c > rune('9') {
								goto l349
							}
							position++
						}
					l350:
						goto l345
					l349:
						position, tokenIndex = position345, tokenIndex345
						if buffer[position] != rune('$') {
							goto l352
						}
						position++
						goto l345
					l352:
						position, tokenIndex = position345, tokenIndex345
						if buffer[position] != rune('_') {
							goto l344
						}
						position++
					}
				l345:
					goto l343
				l344:
					position, tokenIndex = position344, tokenIndex344
				}
				add(ruleSymbolName, position338)
			}
			return true
		l337:
			position, tokenIndex = position337, tokenIndex337
			return false
		},
		/* 24 LocalSymbol <- <('.' 'L' ([a-z] / [A-Z] / ([a-z] / [A-Z]) / '.' / ([0-9] / [0-9]) / '$' / '_')+)> */
		func() bool {
			position353, tokenIndex353 := position, tokenIndex
			{
				position354 := position
				if buffer[position] != rune('.') {
					goto l353
				}
				position++
				if buffer[position] != rune('L') {
					goto l353
				}
				position++
				{
					position357, tokenIndex357 := position, tokenIndex
					if c := buffer[position]; c < rune('a') || c > rune('z') {
						goto l358
					}
					position++
					goto l357
				l358:
					position, tokenIndex = position357, tokenIndex357
					if c := buffer[position]; c < rune('A') || c > rune('Z') {
						goto l359
					}
					position++
					goto l357
				l359:
					position, tokenIndex = position357, tokenIndex357
					{
						position361, tokenIndex361 := position, tokenIndex
						if c := buffer[position]; c < rune('a') || c > rune('z') {
							goto l362
						}
						position++
						goto l361
					l362:
						position, tokenIndex = position361, tokenIndex361
						if c := buffer[position]; c < rune('A') || c > rune('Z') {
							goto l360
						}
						position++
					}
				l361:
					goto l357
				l360:
					position, tokenIndex = position357, tokenIndex357
					if buffer[position] != rune('.') {
						goto l363
					}
					position++
					goto l357
				l363:
					position, tokenIndex = position357, tokenIndex357
					{
						position365, tokenIndex365 := position, tokenIndex
						if c := buffer[position]; c < rune('0') || c > rune('9') {
							goto l366
						}
						position++
						goto l365
					l366:
						position, tokenIndex = position365, tokenIndex365
						if c := buffer[position]; c < rune('0') || c > rune('9') {
							goto l364
						}
						position++
					}
				l365:
					goto l357
				l364:
					position, tokenIndex = position357, tokenIndex357
					if buffer[position] != rune('$') {
						goto l367
					}
					position++
					goto l357
				l367:
					position, tokenIndex = position357, tokenIndex357
					if buffer[position] != rune('_') {
						goto l353
					}
					position++
				}
			l357:
			l355:
				{
					position356, tokenIndex356 := position, tokenIndex
					{
						position368, tokenIndex368 := position, tokenIndex
						if c := buffer[position]; c < rune('a') || c > rune('z') {
							goto l369
						}
						position++
						goto l368
					l369:
						position, tokenIndex = position368, tokenIndex368
						if c := buffer[position]; c < rune('A') || c > rune('Z') {
							goto l370
						}
						position++
						goto l368
					l370:
						position, tokenIndex = position368, tokenIndex368
						{
							position372, tokenIndex372 := position, tokenIndex
							if c := buffer[position]; c < rune('a') || c > rune('z') {
								goto l373
							}
							position++
							goto l372
						l373:
							position, tokenIndex = position372, tokenIndex372
							if c := buffer[position]; c < rune('A') || c > rune('Z') {
								goto l371
							}
							position++
						}
					l372:
						goto l368
					l371:
						position, tokenIndex = position368, tokenIndex368
						if buffer[position] != rune('.') {
							goto l374
						}
						position++
						goto l368
					l374:
						position, tokenIndex = position368, tokenIndex368
						{
							position376, tokenIndex376 := position, tokenIndex
							if c := buffer[position]; c < rune('0') || c > rune('9') {
								goto l377
							}
							position++
							goto l376
						l377:
							position, tokenIndex = position376, tokenIndex376
							if c := buffer[position]; c < rune('0') || c > rune('9') {
								goto l375
							}
							position++
						}
					l376:
						goto l368
					l375:
						position, tokenIndex = position368, tokenIndex368
						if buffer[position] != rune('$') {
							goto l378
						}
						position++
						goto l368
					l378:
						position, tokenIndex = position368, tokenIndex368
						if buffer[position] != rune('_') {
							goto l356
						}
						position++
					}
				l368:
					goto l355
				l356:
					position, tokenIndex = position356, tokenIndex356
				}
				add(ruleLocalSymbol, position354)
			}
			return true
		l353:
			position, tokenIndex = position353, tokenIndex353
			return false
		},
		/* 25 LocalLabel <- <([0-9] ([0-9] / '$')*)> */
		func() bool {
			position379, tokenIndex379 := position, tokenIndex
			{
				position380 := position
				if c := buffer[position]; c < rune('0') || c > rune('9') {
					goto l379
				}
				position++
			l381:
				{
					position382, tokenIndex382 := position, tokenIndex
					{
						position383, tokenIndex383 := position, tokenIndex
						if c := buffer[position]; c < rune('0') || c > rune('9') {
							goto l384
						}
						position++
						goto l383
					l384:
						position, tokenIndex = position383, tokenIndex383
						if buffer[position] != rune('$') {
							goto l382
						}
						position++
					}
				l383:
					goto l381
				l382:
					position, tokenIndex = position382, tokenIndex382
				}
				add(ruleLocalLabel, position380)
			}
			return true
		l379:
			position, tokenIndex = position379, tokenIndex379
			return false
		},
		/* 26 LocalLabelRef <- <([0-9] ([0-9] / '$')* ('b' / 'f'))> */
		func() bool {
			position385, tokenIndex385 := position, tokenIndex
			{
				position386 := position
				if c := buffer[position]; c < rune('0') || c > rune('9') {
					goto l385
				}
				position++
			l387:
				{
					position388, tokenIndex388 := position, tokenIndex
					{
						position389, tokenIndex389 := position, tokenIndex
						if c := buffer[position]; c < rune('0') || c > rune('9') {
							goto l390
						}
						position++
						goto l389
					l390:
						position, tokenIndex = position389, tokenIndex389
						if buffer[position] != rune('$') {
							goto l388
						}
						position++
					}
				l389:
					goto l387
				l388:
					position, tokenIndex = position388, tokenIndex388
				}
				{
					position391, tokenIndex391 := position, tokenIndex
					if buffer[position] != rune('b') {
						goto l392
					}
					position++
					goto l391
				l392:
					position, tokenIndex = position391, tokenIndex391
					if buffer[position] != rune('f') {
						goto l385
					}
					position++
				}
			l391:
				add(ruleLocalLabelRef, position386)
			}
			return true
		l385:
			position, tokenIndex = position385, tokenIndex385
			return false
		},
		/* 27 Instruction <- <(InstructionName (WS InstructionArg (WS? ',' WS? InstructionArg)*)?)> */
		func() bool {
			position393, tokenIndex393 := position, tokenIndex
			{
				position394 := position
				if !_rules[ruleInstructionName]() {
					goto l393
				}
				{
					position395, tokenIndex395 := position, tokenIndex
					if !_rules[ruleWS]() {
						goto l395
					}
					if !_rules[ruleInstructionArg]() {
						goto l395
					}
				l397:
					{
						position398, tokenIndex398 := position, tokenIndex
						{
							position399, tokenIndex399 := position, tokenIndex
							if !_rules[ruleWS]() {
								goto l399
							}
							goto l400
						l399:
							position, tokenIndex = position399, tokenIndex399
						}
					l400:
						if buffer[position] != rune(',') {
							goto l398
						}
						position++
						{
							position401, tokenIndex401 := position, tokenIndex
							if !_rules[ruleWS]() {
								goto l401
							}
							goto l402
						l401:
							position, tokenIndex = position401, tokenIndex401
						}
					l402:
						if !_rules[ruleInstructionArg]() {
							goto l398
						}
						goto l397
					l398:
						position, tokenIndex = position398, tokenIndex398
					}
					goto l396
				l395:
					position, tokenIndex = position395, tokenIndex395
				}
			l396:
				add(ruleInstruction, position394)
			}
			return true
		l393:
			position, tokenIndex = position393, tokenIndex393
			return false
		},
		/* 28 InstructionName <- <(([a-z] / [A-Z]) ([a-z] / [A-Z] / '.' / ([0-9] / [0-9]))* ('.' / '+' / '-')?)> */
		func() bool {
			position403, tokenIndex403 := position, tokenIndex
			{
				position404 := position
				{
					position405, tokenIndex405 := position, tokenIndex
					if c := buffer[position]; c < rune('a') || c > rune('z') {
						goto l406
					}
					position++
					goto l405
				l406:
					position, tokenIndex = position405, tokenIndex405
					if c := buffer[position]; c < rune('A') || c > rune('Z') {
						goto l403
					}
					position++
				}
			l405:
			l407:
				{
					position408, tokenIndex408 := position, tokenIndex
					{
						position409, tokenIndex409 := position, tokenIndex
						if c := buffer[position]; c < rune('a') || c > rune('z') {
							goto l410
						}
						position++
						goto l409
					l410:
						position, tokenIndex = position409, tokenIndex409
						if c := buffer[position]; c < rune('A') || c > rune('Z') {
							goto l411
						}
						position++
						goto l409
					l411:
						position, tokenIndex = position409, tokenIndex409
						if buffer[position] != rune('.') {
							goto l412
						}
						position++
						goto l409
					l412:
						position, tokenIndex = position409, tokenIndex409
						{
							position413, tokenIndex413 := position, tokenIndex
							if c := buffer[position]; c < rune('0') || c > rune('9') {
								goto l414
							}
							position++
							goto l413
						l414:
							position, tokenIndex = position413, tokenIndex413
							if c := buffer[position]; c < rune('0') || c > rune('9') {
								goto l408
							}
							position++
						}
					l413:
					}
				l409:
					goto l407
				l408:
					position, tokenIndex = position408, tokenIndex408
				}
				{
					position415, tokenIndex415 := position, tokenIndex
					{
						position417, tokenIndex417 := position, tokenIndex
						if buffer[position] != rune('.') {
							goto l418
						}
						position++
						goto l417
					l418:
						position, tokenIndex = position417, tokenIndex417
						if buffer[position] != rune('+') {
							goto l419
						}
						position++
						goto l417
					l419:
						position, tokenIndex = position417, tokenIndex417
						if buffer[position] != rune('-') {
							goto l415
						}
						position++
					}
				l417:
					goto l416
				l415:
					position, tokenIndex = position415, tokenIndex415
				}
			l416:
				add(ruleInstructionName, position404)
			}
			return true
		l403:
			position, tokenIndex = position403, tokenIndex403
			return false
		},
		/* 29 InstructionArg <- <(IndirectionIndicator? (ARMConstantTweak / RegisterOrConstant / LocalLabelRef / TOCRefHigh / TOCRefLow / GOTLocation / GOTSymbolOffset / MemoryRef) AVX512Token*)> */
		func() bool {
			position420, tokenIndex420 := position, tokenIndex
			{
				position421 := position
				{
					position422, tokenIndex422 := position, tokenIndex
					if !_rules[ruleIndirectionIndicator]() {
						goto l422
					}
					goto l423
				l422:
					position, tokenIndex = position422, tokenIndex422
				}
			l423:
				{
					position424, tokenIndex424 := position, tokenIndex
					if !_rules[ruleARMConstantTweak]() {
						goto l425
					}
					goto l424
				l425:
					position, tokenIndex = position424, tokenIndex424
					if !_rules[ruleRegisterOrConstant]() {
						goto l426
					}
					goto l424
				l426:
					position, tokenIndex = position424, tokenIndex424
					if !_rules[ruleLocalLabelRef]() {
						goto l427
					}
					goto l424
				l427:
					position, tokenIndex = position424, tokenIndex424
					if !_rules[ruleTOCRefHigh]() {
						goto l428
					}
					goto l424
				l428:
					position, tokenIndex = position424, tokenIndex424
					if !_rules[ruleTOCRefLow]() {
						goto l429
					}
					goto l424
				l429:
					position, tokenIndex = position424, tokenIndex424
					if !_rules[ruleGOTLocation]() {
						goto l430
					}
					goto l424
				l430:
					position, tokenIndex = position424, tokenIndex424
					if !_rules[ruleGOTSymbolOffset]() {
						goto l431
					}
					goto l424
				l431:
					position, tokenIndex = position424, tokenIndex424
					if !_rules[ruleMemoryRef]() {
						goto l420
					}
				}
			l424:
			l432:
				{
					position433, tokenIndex433 := position, tokenIndex
					if !_rules[ruleAVX512Token]() {
						goto l433
					}
					goto l432
				l433:
					position, tokenIndex = position433, tokenIndex433
				}
				add(ruleInstructionArg, position421)
			}
			return true
		l420:
			position, tokenIndex = position420, tokenIndex420
			return false
		},
		/* 30 GOTLocation <- <('$' '_' 'G' 'L' 'O' 'B' 'A' 'L' '_' 'O' 'F' 'F' 'S' 'E' 'T' '_' 'T' 'A' 'B' 'L' 'E' '_' '-' LocalSymbol)> */
		func() bool {
			position434, tokenIndex434 := position, tokenIndex
			{
				position435 := position
				if buffer[position] != rune('$') {
					goto l434
				}
				position++
				if buffer[position] != rune('_') {
					goto l434
				}
				position++
				if buffer[position] != rune('G') {
					goto l434
				}
				position++
				if buffer[position] != rune('L') {
					goto l434
				}
				position++
				if buffer[position] != rune('O') {
					goto l434
				}
				position++
				if buffer[position] != rune('B') {
					goto l434
				}
				position++
				if buffer[position] != rune('A') {
					goto l434
				}
				position++
				if buffer[position] != rune('L') {
					goto l434
				}
				position++
				if buffer[position] != rune('_') {
					goto l434
				}
				position++
				if buffer[position] != rune('O') {
					goto l434
				}
				position++
				if buffer[position] != rune('F') {
					goto l434
				}
				position++
				if buffer[position] != rune('F') {
					goto l434
				}
				position++
				if buffer[position] != rune('S') {
					goto l434
				}
				position++
				if buffer[position] != rune('E') {
					goto l434
				}
				position++
				if buffer[position] != rune('T') {
					goto l434
				}
				position++
				if buffer[position] != rune('_') {
					goto l434
				}
				position++
				if buffer[position] != rune('T') {
					goto l434
				}
				position++
				if buffer[position] != rune('A') {
					goto l434
				}
				position++
				if buffer[position] != rune('B') {
					goto l434
				}
				position++
				if buffer[position] != rune('L') {
					goto l434
				}
				position++
				if buffer[position] != rune('E') {
					goto l434
				}
				position++
				if buffer[position] != rune('_') {
					goto l434
				}
				position++
				if buffer[position] != rune('-') {
					goto l434
				}
				position++
				if !_rules[ruleLocalSymbol]() {
					goto l434
				}
				add(ruleGOTLocation, position435)
			}
			return true
		l434:
			position, tokenIndex = position434, tokenIndex434
			return false
		},
		/* 31 GOTSymbolOffset <- <(('$' SymbolName ('@' 'G' 'O' 'T') ('O' 'F' 'F')?) / (':' ('g' / 'G') ('o' / 'O') ('t' / 'T') ':' SymbolName))> */
		func() bool {
			position436, tokenIndex436 := position, tokenIndex
			{
				position437 := position
				{
					position438, tokenIndex438 := position, tokenIndex
					if buffer[position] != rune('$') {
						goto l439
					}
					position++
					if !_rules[ruleSymbolName]() {
						goto l439
					}
					if buffer[position] != rune('@') {
						goto l439
					}
					position++
					if buffer[position] != rune('G') {
						goto l439
					}
					position++
					if buffer[position] != rune('O') {
						goto l439
					}
					position++
					if buffer[position] != rune('T') {
						goto l439
					}
					position++
					{
						position440, tokenIndex440 := position, tokenIndex
						if buffer[position] != rune('O') {
							goto l440
						}
						position++
						if buffer[position] != rune('F') {
							goto l440
						}
						position++
						if buffer[position] != rune('F') {
							goto l440
						}
						position++
						goto l441
					l440:
						position, tokenIndex = position440, tokenIndex440
					}
				l441:
					goto l438
				l439:
					position, tokenIndex = position438, tokenIndex438
					if buffer[position] != rune(':') {
						goto l436
					}
					position++
					{
						position442, tokenIndex442 := position, tokenIndex
						if buffer[position] != rune('g') {
							goto l443
						}
						position++
						goto l442
					l443:
						position, tokenIndex = position442, tokenIndex442
						if buffer[position] != rune('G') {
							goto l436
						}
						position++
					}
				l442:
					{
						position444, tokenIndex444 := position, tokenIndex
						if buffer[position] != rune('o') {
							goto l445
						}
						position++
						goto l444
					l445:
						position, tokenIndex = position444, tokenIndex444
						if buffer[position] != rune('O') {
							goto l436
						}
						position++
					}
				l444:
					{
						position446, tokenIndex446 := position, tokenIndex
						if buffer[position] != rune('t') {
							goto l447
						}
						position++
						goto l446
					l447:
						position, tokenIndex = position446, tokenIndex446
						if buffer[position] != rune('T') {
							goto l436
						}
						position++
					}
				l446:
					if buffer[position] != rune(':') {
						goto l436
					}
					position++
					if !_rules[ruleSymbolName]() {
						goto l436
					}
				}
			l438:
				add(ruleGOTSymbolOffset, position437)
			}
			return true
		l436:
			position, tokenIndex = position436, tokenIndex436
			return false
		},
		/* 32 AVX512Token <- <(WS? '{' '%'? ([0-9] / [a-z])* '}')> */
		func() bool {
			position448, tokenIndex448 := position, tokenIndex
			{
				position449 := position
				{
					position450, tokenIndex450 := position, tokenIndex
					if !_rules[ruleWS]() {
						goto l450
					}
					goto l451
				l450:
					position, tokenIndex = position450, tokenIndex450
				}
			l451:
				if buffer[position] != rune('{') {
					goto l448
				}
				position++
				{
					position452, tokenIndex452 := position, tokenIndex
					if buffer[position] != rune('%') {
						goto l452
					}
					position++
					goto l453
				l452:
					position, tokenIndex = position452, tokenIndex452
				}
			l453:
			l454:
				{
					position455, tokenIndex455 := position, tokenIndex
					{
						position456, tokenIndex456 := position, tokenIndex
						if c := buffer[position]; c < rune('0') || c > rune('9') {
							goto l457
						}
						position++
						goto l456
					l457:
						position, tokenIndex = position456, tokenIndex456
						if c := buffer[position]; c < rune('a') || c > rune('z') {
							goto l455
						}
						position++
					}
				l456:
					goto l454
				l455:
					position, tokenIndex = position455, tokenIndex455
				}
				if buffer[position] != rune('}') {
					goto l448
				}
				position++
				add(ruleAVX512Token, position449)
			}
			return true
		l448:
			position, tokenIndex = position448, tokenIndex448
			return false
		},
		/* 33 TOCRefHigh <- <('.' 'T' 'O' 'C' '.' '-' (('0' 'b') / ('.' 'L' ([a-z] / [A-Z] / '_' / [0-9])+)) ('@' ('h' / 'H') ('a' / 'A')))> */
		func() bool {
			position458, tokenIndex458 := position, tokenIndex
			{
				position459 := position
				if buffer[position] != rune('.') {
					goto l458
				}
				position++
				if buffer[position] != rune('T') {
					goto l458
				}
				position++
				if buffer[position] != rune('O') {
					goto l458
				}
				position++
				if buffer[position] != rune('C') {
					goto l458
				}
				position++
				if buffer[position] != rune('.') {
					goto l458
				}
				position++
				if buffer[position] != rune('-') {
					goto l458
				}
				position++
				{
					position460, tokenIndex460 := position, tokenIndex
					if buffer[position] != rune('0') {
						goto l461
					}
					position++
					if buffer[position] != rune('b') {
						goto l461
					}
					position++
					goto l460
				l461:
					position, tokenIndex = position460, tokenIndex460
					if buffer[position] != rune('.') {
						goto l458
					}
					position++
					if buffer[position] != rune('L') {
						goto l458
					}
					position++
					{
						position464, tokenIndex464 := position, tokenIndex
						if c := buffer[position]; c < rune('a') || c > rune('z') {
							goto l465
						}
						position++
						goto l464
					l465:
						position, tokenIndex = position464, tokenIndex464
						if c := buffer[position]; c < rune('A') || c > rune('Z') {
							goto l466
						}
						position++
						goto l464
					l466:
						position, tokenIndex = position464, tokenIndex464
						if buffer[position] != rune('_') {
							goto l467
						}
						position++
						goto l464
					l467:
						position, tokenIndex = position464, tokenIndex464
						if c := buffer[position]; c < rune('0') || c > rune('9') {
							goto l458
						}
						position++
					}
				l464:
				l462:
					{
						position463, tokenIndex463 := position, tokenIndex
						{
							position468, tokenIndex468 := position, tokenIndex
							if c := buffer[position]; c < rune('a') || c > rune('z') {
								goto l469
							}
							position++
							goto l468
						l469:
							position, tokenIndex = position468, tokenIndex468
							if c := buffer[position]; c < rune('A') || c > rune('Z') {
								goto l470
							}
							position++
							goto l468
						l470:
							position, tokenIndex = position468, tokenIndex468
							if buffer[position] != rune('_') {
								goto l471
							}
							position++
							goto l468
						l471:
							position, tokenIndex = position468, tokenIndex468
							if c := buffer[position]; c < rune('0') || c > rune('9') {
								goto l463
							}
							position++
						}
					l468:
						goto l462
					l463:
						position, tokenIndex = position463, tokenIndex463
					}
				}
			l460:
				if buffer[position] != rune('@') {
					goto l458
				}
				position++
				{
					position472, tokenIndex472 := position, tokenIndex
					if buffer[position] != rune('h') {
						goto l473
					}
					position++
					goto l472
				l473:
					position, tokenIndex = position472, tokenIndex472
					if buffer[position] != rune('H') {
						goto l458
					}
					position++
				}
			l472:
				{
					position474, tokenIndex474 := position, tokenIndex
					if buffer[position] != rune('a') {
						goto l475
					}
					position++
					goto l474
				l475:
					position, tokenIndex = position474, tokenIndex474
					if buffer[position] != rune('A') {
						goto l458
					}
					position++
				}
			l474:
				add(ruleTOCRefHigh, position459)
			}
			return true
		l458:
			position, tokenIndex = position458, tokenIndex458
			return false
		},
		/* 34 TOCRefLow <- <('.' 'T' 'O' 'C' '.' '-' (('0' 'b') / ('.' 'L' ([a-z] / [A-Z] / '_' / [0-9])+)) ('@' ('l' / 'L')))> */
		func() bool {
			position476, tokenIndex476 := position, tokenIndex
			{
				position477 := position
				if buffer[position] != rune('.') {
					goto l476
				}
				position++
				if buffer[position] != rune('T') {
					goto l476
				}
				position++
				if buffer[position] != rune('O') {
					goto l476
				}
				position++
				if buffer[position] != rune('C') {
					goto l476
				}
				position++
				if buffer[position] != rune('.') {
					goto l476
				}
				position++
				if buffer[position] != rune('-') {
					goto l476
				}
				position++
				{
					position478, tokenIndex478 := position, tokenIndex
					if buffer[position] != rune('0') {
						goto l479
					}
					position++
					if buffer[position] != rune('b') {
						goto l479
					}
					position++
					goto l478
				l479:
					position, tokenIndex = position478, tokenIndex478
					if buffer[position] != rune('.') {
						goto l476
					}
					position++
					if buffer[position] != rune('L') {
						goto l476
					}
					position++
					{
						position482, tokenIndex482 := position, tokenIndex
						if c := buffer[position]; c < rune('a') || c > rune('z') {
							goto l483
						}
						position++
						goto l482
					l483:
						position, tokenIndex = position482, tokenIndex482
						if c := buffer[position]; c < rune('A') || c > rune('Z') {
							goto l484
						}
						position++
						goto l482
					l484:
						position, tokenIndex = position482, tokenIndex482
						if buffer[position] != rune('_') {
							goto l485
						}
						position++
						goto l482
					l485:
						position, tokenIndex = position482, tokenIndex482
						if c := buffer[position]; c < rune('0') || c > rune('9') {
							goto l476
						}
						position++
					}
				l482:
				l480:
					{
						position481, tokenIndex481 := position, tokenIndex
						{
							position486, tokenIndex486 := position, tokenIndex
							if c := buffer[position]; c < rune('a') || c > rune('z') {
								goto l487
							}
							position++
							goto l486
						l487:
							position, tokenIndex = position486, tokenIndex486
							if c := buffer[position]; c < rune('A') || c > rune('Z') {
								goto l488
							}
							position++
							goto l486
						l488:
							position, tokenIndex = position486, tokenIndex486
							if buffer[position] != rune('_') {
								goto l489
							}
							position++
							goto l486
						l489:
							position, tokenIndex = position486, tokenIndex486
							if c := buffer[position]; c < rune('0') || c > rune('9') {
								goto l481
							}
							position++
						}
					l486:
						goto l480
					l481:
						position, tokenIndex = position481, tokenIndex481
					}
				}
			l478:
				if buffer[position] != rune('@') {
					goto l476
				}
				position++
				{
					position490, tokenIndex490 := position, tokenIndex
					if buffer[position] != rune('l') {
						goto l491
					}
					position++
					goto l490
				l491:
					position, tokenIndex = position490, tokenIndex490
					if buffer[position] != rune('L') {
						goto l476
					}
					position++
				}
			l490:
				add(ruleTOCRefLow, position477)
			}
			return true
		l476:
			position, tokenIndex = position476, tokenIndex476
			return false
		},
		/* 35 IndirectionIndicator <- <'*'> */
		func() bool {
			position492, tokenIndex492 := position, tokenIndex
			{
				position493 := position
				if buffer[position] != rune('*') {
					goto l492
				}
				position++
				add(ruleIndirectionIndicator, position493)
			}
			return true
		l492:
			position, tokenIndex = position492, tokenIndex492
			return false
		},
		/* 36 RegisterOrConstant <- <((('%' ([a-z] / [A-Z]) ([a-z] / [A-Z] / ([0-9] / [0-9]))*) / ('$'? ((Offset Offset) / Offset)) / ('#' Offset ('*' [0-9]+ ('-' [0-9] [0-9]*)?)?) / ('#' '~'? '(' [0-9] WS? ('<' '<') WS? [0-9] ')') / ARMRegister) !('f' / 'b' / ':' / '(' / '+' / '-'))> */
		func() bool {
			position494, tokenIndex494 := position, tokenIndex
			{
				position495 := position
				{
					position496, tokenIndex496 := position, tokenIndex
					if buffer[position] != rune('%') {
						goto l497
					}
					position++
					{
						position498, tokenIndex498 := position, tokenIndex
						if c := buffer[position]; c < rune('a') || c > rune('z') {
							goto l499
						}
						position++
						goto l498
					l499:
						position, tokenIndex = position498, tokenIndex498
						if c := buffer[position]; c < rune('A') || c > rune('Z') {
							goto l497
						}
						position++
					}
				l498:
				l500:
					{
						position501, tokenIndex501 := position, tokenIndex
						{
							position502, tokenIndex502 := position, tokenIndex
							if c := buffer[position]; c < rune('a') || c > rune('z') {
								goto l503
							}
							position++
							goto l502
						l503:
							position, tokenIndex = position502, tokenIndex502
							if c := buffer[position]; c < rune('A') || c > rune('Z') {
								goto l504
							}
							position++
							goto l502
						l504:
							position, tokenIndex = position502, tokenIndex502
							{
								position505, tokenIndex505 := position, tokenIndex
								if c := buffer[position]; c < rune('0') || c > rune('9') {
									goto l506
								}
								position++
								goto l505
							l506:
								position, tokenIndex = position505, tokenIndex505
								if c := buffer[position]; c < rune('0') || c > rune('9') {
									goto l501
								}
								position++
							}
						l505:
						}
					l502:
						goto l500
					l501:
						position, tokenIndex = position501, tokenIndex501
					}
					goto l496
				l497:
					position, tokenIndex = position496, tokenIndex496
					{
						position508, tokenIndex508 := position, tokenIndex
						if buffer[position] != rune('$') {
							goto l508
						}
						position++
						goto l509
					l508:
						position, tokenIndex = position508, tokenIndex508
					}
				l509:
					{
						position510, tokenIndex510 := position, tokenIndex
						if !_rules[ruleOffset]() {
							goto l511
						}
						if !_rules[ruleOffset]() {
							goto l511
						}
						goto l510
					l511:
						position, tokenIndex = position510, tokenIndex510
						if !_rules[ruleOffset]() {
							goto l507
						}
					}
				l510:
					goto l496
				l507:
					position, tokenIndex = position496, tokenIndex496
					if buffer[position] != rune('#') {
						goto l512
					}
					position++
					if !_rules[ruleOffset]() {
						goto l512
					}
					{
						position513, tokenIndex513 := position, tokenIndex
						if buffer[position] != rune('*') {
							goto l513
						}
						position++
						if c := buffer[position]; c < rune('0') || c > rune('9') {
							goto l513
						}
						position++
					l515:
						{
							position516, tokenIndex516 := position, tokenIndex
							if c := buffer[position]; c < rune('0') || c > rune('9') {
								goto l516
							}
							position++
							goto l515
						l516:
							position, tokenIndex = position516, tokenIndex516
						}
						{
							position517, tokenIndex517 := position, tokenIndex
							if buffer[position] != rune('-') {
								goto l517
							}
							position++
							if c := buffer[position]; c < rune('0') || c > rune('9') {
								goto l517
							}
							position++
						l519:
							{
								position520, tokenIndex520 := position, tokenIndex
								if c := buffer[position]; c < rune('0') || c > rune('9') {
									goto l520
								}
								position++
								goto l519
							l520:
								position, tokenIndex = position520, tokenIndex520
							}
							goto l518
						l517:
							position, tokenIndex = position517, tokenIndex517
						}
					l518:
						goto l514
					l513:
						position, tokenIndex = position513, tokenIndex513
					}
				l514:
					goto l496
				l512:
					position, tokenIndex = position496, tokenIndex496
					if buffer[position] != rune('#') {
						goto l521
					}
					position++
					{
						position522, tokenIndex522 := position, tokenIndex
						if buffer[position] != rune('~') {
							goto l522
						}
						position++
						goto l523
					l522:
						position, tokenIndex = position522, tokenIndex522
					}
				l523:
					if buffer[position] != rune('(') {
						goto l521
					}
					position++
					if c := buffer[position]; c < rune('0') || c > rune('9') {
						goto l521
					}
					position++
					{
						position524, tokenIndex524 := position, tokenIndex
						if !_rules[ruleWS]() {
							goto l524
						}
						goto l525
					l524:
						position, tokenIndex = position524, tokenIndex524
					}
				l525:
					if buffer[position] != rune('<') {
						goto l521
					}
					position++
					if buffer[position] != rune('<') {
						goto l521
					}
					position++
					{
						position526, tokenIndex526 := position, tokenIndex
						if !_rules[ruleWS]() {
							goto l526
						}
						goto l527
					l526:
						position, tokenIndex = position526, tokenIndex526
					}
				l527:
					if c := buffer[position]; c < rune('0') || c > rune('9') {
						goto l521
					}
					position++
					if buffer[position] != rune(')') {
						goto l521
					}
					position++
					goto l496
				l521:
					position, tokenIndex = position496, tokenIndex496
					if !_rules[ruleARMRegister]() {
						goto l494
					}
				}
			l496:
				{
					position528, tokenIndex528 := position, tokenIndex
					{
						position529, tokenIndex529 := position, tokenIndex
						if buffer[position] != rune('f') {
							goto l530
						}
						position++
						goto l529
					l530:
						position, tokenIndex = position529, tokenIndex529
						if buffer[position] != rune('b') {
							goto l531
						}
						position++
						goto l529
					l531:
						position, tokenIndex = position529, tokenIndex529
						if buffer[position] != rune(':') {
							goto l532
						}
						position++
						goto l529
					l532:
						position, tokenIndex = position529, tokenIndex529
						if buffer[position] != rune('(') {
							goto l533
						}
						position++
						goto l529
					l533:
						position, tokenIndex = position529, tokenIndex529
						if buffer[position] != rune('+') {
							goto l534
						}
						position++
						goto l529
					l534:
						position, tokenIndex = position529, tokenIndex529
						if buffer[position] != rune('-') {
							goto l528
						}
						position++
					}
				l529:
					goto l494
				l528:
					position, tokenIndex = position528, tokenIndex528
				}
				add(ruleRegisterOrConstant, position495)
			}
			return true
		l494:
			position, tokenIndex = position494, tokenIndex494
			return false
		},
		/* 37 ARMConstantTweak <- <(((('l' / 'L') ('s' / 'S') ('l' / 'L')) / (('s' / 'S') ('x' / 'X') ('t' / 'T') ('w' / 'W')) / (('u' / 'U') ('x' / 'X') ('t' / 'T') ('w' / 'W')) / (('u' / 'U') ('x' / 'X') ('t' / 'T') ('b' / 'B')) / (('l' / 'L') ('s' / 'S') ('r' / 'R')) / (('r' / 'R') ('o' / 'O') ('r' / 'R')) / (('a' / 'A') ('s' / 'S') ('r' / 'R'))) (WS '#' Offset)?)> */
		func() bool {
			position535, tokenIndex535 := position, tokenIndex
			{
				position536 := position
				{
					position537, tokenIndex537 := position, tokenIndex
					{
						position539, tokenIndex539 := position, tokenIndex
						if buffer[position] != rune('l') {
							goto l540
						}
						position++
						goto l539
					l540:
						position, tokenIndex = position539, tokenIndex539
						if buffer[position] != rune('L') {
							goto l538
						}
						position++
					}
				l539:
					{
						position541, tokenIndex541 := position, tokenIndex
						if buffer[position] != rune('s') {
							goto l542
						}
						position++
						goto l541
					l542:
						position, tokenIndex = position541, tokenIndex541
						if buffer[position] != rune('S') {
							goto l538
						}
						position++
					}
				l541:
					{
						position543, tokenIndex543 := position, tokenIndex
						if buffer[position] != rune('l') {
							goto l544
						}
						position++
						goto l543
					l544:
						position, tokenIndex = position543, tokenIndex543
						if buffer[position] != rune('L') {
							goto l538
						}
						position++
					}
				l543:
					goto l537
				l538:
					position, tokenIndex = position537, tokenIndex537
					{
						position546, tokenIndex546 := position, tokenIndex
						if buffer[position] != rune('s') {
							goto l547
						}
						position++
						goto l546
					l547:
						position, tokenIndex = position546, tokenIndex546
						if buffer[position] != rune('S') {
							goto l545
						}
						position++
					}
				l546:
					{
						position548, tokenIndex548 := position, tokenIndex
						if buffer[position] != rune('x') {
							goto l549
						}
						position++
						goto l548
					l549:
						position, tokenIndex = position548, tokenIndex548
						if buffer[position] != rune('X') {
							goto l545
						}
						position++
					}
				l548:
					{
						position550, tokenIndex550 := position, tokenIndex
						if buffer[position] != rune('t') {
							goto l551
						}
						position++
						goto l550
					l551:
						position, tokenIndex = position550, tokenIndex550
						if buffer[position] != rune('T') {
							goto l545
						}
						position++
					}
				l550:
					{
						position552, tokenIndex552 := position, tokenIndex
						if buffer[position] != rune('w') {
							goto l553
						}
						position++
						goto l552
					l553:
						position, tokenIndex = position552, tokenIndex552
						if buffer[position] != rune('W') {
							goto l545
						}
						position++
					}
				l552:
					goto l537
				l545:
					position, tokenIndex = position537, tokenIndex537
					{
						position555, tokenIndex555 := position, tokenIndex
						if buffer[position] != rune('u') {
							goto l556
						}
						position++
						goto l555
					l556:
						position, tokenIndex = position555, tokenIndex555
						if buffer[position] != rune('U') {
							goto l554
						}
						position++
					}
				l555:
					{
						position557, tokenIndex557 := position, tokenIndex
						if buffer[position] != rune('x') {
							goto l558
						}
						position++
						goto l557
					l558:
						position, tokenIndex = position557, tokenIndex557
						if buffer[position] != rune('X') {
							goto l554
						}
						position++
					}
				l557:
					{
						position559, tokenIndex559 := position, tokenIndex
						if buffer[position] != rune('t') {
							goto l560
						}
						position++
						goto l559
					l560:
						position, tokenIndex = position559, tokenIndex559
						if buffer[position] != rune('T') {
							goto l554
						}
						position++
					}
				l559:
					{
						position561, tokenIndex561 := position, tokenIndex
						if buffer[position] != rune('w') {
							goto l562
						}
						position++
						goto l561
					l562:
						position, tokenIndex = position561, tokenIndex561
						if buffer[position] != rune('W') {
							goto l554
						}
						position++
					}
				l561:
					goto l537
				l554:
					position, tokenIndex = position537, tokenIndex537
					{
						position564, tokenIndex564 := position, tokenIndex
						if buffer[position] != rune('u') {
							goto l565
						}
						position++
						goto l564
					l565:
						position, tokenIndex = position564, tokenIndex564
						if buffer[position] != rune('U') {
							goto l563
						}
						position++
					}
				l564:
					{
						position566, tokenIndex566 := position, tokenIndex
						if buffer[position] != rune('x') {
							goto l567
						}
						position++
						goto l566
					l567:
						position, tokenIndex = position566, tokenIndex566
						if buffer[position] != rune('X') {
							goto l563
						}
						position++
					}
				l566:
					{
						position568, tokenIndex568 := position, tokenIndex
						if buffer[position] != rune('t') {
							goto l569
						}
						position++
						goto l568
					l569:
						position, tokenIndex = position568, tokenIndex568
						if buffer[position] != rune('T') {
							goto l563
						}
						position++
					}
				l568:
					{
						position570, tokenIndex570 := position, tokenIndex
						if buffer[position] != rune('b') {
							goto l571
						}
						position++
						goto l570
					l571:
						position, tokenIndex = position570, tokenIndex570
						if buffer[position] != rune('B') {
							goto l563
						}
						position++
					}
				l570:
					goto l537
				l563:
					position, tokenIndex = position537, tokenIndex537
					{
						position573, tokenIndex573 := position, tokenIndex
						if buffer[position] != rune('l') {
							goto l574
						}
						position++
						goto l573
					l574:
						position, tokenIndex = position573, tokenIndex573
						if buffer[position] != rune('L') {
							goto l572
						}
						position++
					}
				l573:
					{
						position575, tokenIndex575 := position, tokenIndex
						if buffer[position] != rune('s') {
							goto l576
						}
						position++
						goto l575
					l576:
						position, tokenIndex = position575, tokenIndex575
						if buffer[position] != rune('S') {
							goto l572
						}
						position++
					}
				l575:
					{
						position577, tokenIndex577 := position, tokenIndex
						if buffer[position] != rune('r') {
							goto l578
						}
						position++
						goto l577
					l578:
						position, tokenIndex = position577, tokenIndex577
						if buffer[position] != rune('R') {
							goto l572
						}
						position++
					}
				l577:
					goto l537
				l572:
					position, tokenIndex = position537, tokenIndex537
					{
						position580, tokenIndex580 := position, tokenIndex
						if buffer[position] != rune('r') {
							goto l581
						}
						position++
						goto l580
					l581:
						position, tokenIndex = position580, tokenIndex580
						if buffer[position] != rune('R') {
							goto l579
						}
						position++
					}
				l580:
					{
						position582, tokenIndex582 := position, tokenIndex
						if buffer[position] != rune('o') {
							goto l583
						}
						position++
						goto l582
					l583:
						position, tokenIndex = position582, tokenIndex582
						if buffer[position] != rune('O') {
							goto l579
						}
						position++
					}
				l582:
					{
						position584, tokenIndex584 := position, tokenIndex
						if buffer[position] != rune('r') {
							goto l585
						}
						position++
						goto l584
					l585:
						position, tokenIndex = position584, tokenIndex584
						if buffer[position] != rune('R') {
							goto l579
						}
						position++
					}
				l584:
					goto l537
				l579:
					position, tokenIndex = position537, tokenIndex537
					{
						position586, tokenIndex586 := position, tokenIndex
						if buffer[position] != rune('a') {
							goto l587
						}
						position++
						goto l586
					l587:
						position, tokenIndex = position586, tokenIndex586
						if buffer[position] != rune('A') {
							goto l535
						}
						position++
					}
				l586:
					{
						position588, tokenIndex588 := position, tokenIndex
						if buffer[position] != rune('s') {
							goto l589
						}
						position++
						goto l588
					l589:
						position, tokenIndex = position588, tokenIndex588
						if buffer[position] != rune('S') {
							goto l535
						}
						position++
					}
				l588:
					{
						position590, tokenIndex590 := position, tokenIndex
						if buffer[position] != rune('r') {
							goto l591
						}
						position++
						goto l590
					l591:
						position, tokenIndex = position590, tokenIndex590
						if buffer[position] != rune('R') {
							goto l535
						}
						position++
					}
				l590:
				}
			l537:
				{
					position592, tokenIndex592 := position, tokenIndex
					if !_rules[ruleWS]() {
						goto l592
					}
					if buffer[position] != rune('#') {
						goto l592
					}
					position++
					if !_rules[ruleOffset]() {
						goto l592
					}
					goto l593
				l592:
					position, tokenIndex = position592, tokenIndex592
				}
			l593:
				add(ruleARMConstantTweak, position536)
			}
			return true
		l535:
			position, tokenIndex = position535, tokenIndex535
			return false
		},
		/* 38 ARMRegister <- <((('s' / 'S') ('p' / 'P')) / (('x' / 'w' / 'd' / 'q' / 's') [0-9] [0-9]?) / (('x' / 'X') ('z' / 'Z') ('r' / 'R')) / (('w' / 'W') ('z' / 'Z') ('r' / 'R')) / ARMVectorRegister / ('{' WS? ARMVectorRegister (',' WS? ARMVectorRegister)* WS? '}' ('[' [0-9] ']')?))> */
		func() bool {
			position594, tokenIndex594 := position, tokenIndex
			{
				position595 := position
				{
					position596, tokenIndex596 := position, tokenIndex
					{
						position598, tokenIndex598 := position, tokenIndex
						if buffer[position] != rune('s') {
							goto l599
						}
						position++
						goto l598
					l599:
						position, tokenIndex = position598, tokenIndex598
						if buffer[position] != rune('S') {
							goto l597
						}
						position++
					}
				l598:
					{
						position600, tokenIndex600 := position, tokenIndex
						if buffer[position] != rune('p') {
							goto l601
						}
						position++
						goto l600
					l601:
						position, tokenIndex = position600, tokenIndex600
						if buffer[position] != rune('P') {
							goto l597
						}
						position++
					}
				l600:
					goto l596
				l597:
					position, tokenIndex = position596, tokenIndex596
					{
						position603, tokenIndex603 := position, tokenIndex
						if buffer[position] != rune('x') {
							goto l604
						}
						position++
						goto l603
					l604:
						position, tokenIndex = position603, tokenIndex603
						if buffer[position] != rune('w') {
							goto l605
						}
						position++
						goto l603
					l605:
						position, tokenIndex = position603, tokenIndex603
						if buffer[position] != rune('d') {
							goto l606
						}
						position++
						goto l603
					l606:
						position, tokenIndex = position603, tokenIndex603
						if buffer[position] != rune('q') {
							goto l607
						}
						position++
						goto l603
					l607:
						position, tokenIndex = position603, tokenIndex603
						if buffer[position] != rune('s') {
							goto l602
						}
						position++
					}
				l603:
					if c := buffer[position]; c < rune('0') || c > rune('9') {
						goto l602
					}
					position++
					{
						position608, tokenIndex608 := position, tokenIndex
						if c := buffer[position]; c < rune('0') || c > rune('9') {
							goto l608
						}
						position++
						goto l609
					l608:
						position, tokenIndex = position608, tokenIndex608
					}
				l609:
					goto l596
				l602:
					position, tokenIndex = position596, tokenIndex596
					{
						position611, tokenIndex611 := position, tokenIndex
						if buffer[position] != rune('x') {
							goto l612
						}
						position++
						goto l611
					l612:
						position, tokenIndex = position611, tokenIndex611
						if buffer[position] != rune('X') {
							goto l610
						}
						position++
					}
				l611:
					{
						position613, tokenIndex613 := position, tokenIndex
						if buffer[position] != rune('z') {
							goto l614
						}
						position++
						goto l613
					l614:
						position, tokenIndex = position613, tokenIndex613
						if buffer[position] != rune('Z') {
							goto l610
						}
						position++
					}
				l613:
					{
						position615, tokenIndex615 := position, tokenIndex
						if buffer[position] != rune('r') {
							goto l616
						}
						position++
						goto l615
					l616:
						position, tokenIndex = position615, tokenIndex615
						if buffer[position] != rune('R') {
							goto l610
						}
						position++
					}
				l615:
					goto l596
				l610:
					position, tokenIndex = position596, tokenIndex596
					{
						position618, tokenIndex618 := position, tokenIndex
						if buffer[position] != rune('w') {
							goto l619
						}
						position++
						goto l618
					l619:
						position, tokenIndex = position618, tokenIndex618
						if buffer[position] != rune('W') {
							goto l617
						}
						position++
					}
				l618:
					{
						position620, tokenIndex620 := position, tokenIndex
						if buffer[position] != rune('z') {
							goto l621
						}
						position++
						goto l620
					l621:
						position, tokenIndex = position620, tokenIndex620
						if buffer[position] != rune('Z') {
							goto l617
						}
						position++
					}
				l620:
					{
						position622, tokenIndex622 := position, tokenIndex
						if buffer[position] != rune('r') {
							goto l623
						}
						position++
						goto l622
					l623:
						position, tokenIndex = position622, tokenIndex622
						if buffer[position] != rune('R') {
							goto l617
						}
						position++
					}
				l622:
					goto l596
				l617:
					position, tokenIndex = position596, tokenIndex596
					if !_rules[ruleARMVectorRegister]() {
						goto l624
					}
					goto l596
				l624:
					position, tokenIndex = position596, tokenIndex596
					if buffer[position] != rune('{') {
						goto l594
					}
					position++
					{
						position625, tokenIndex625 := position, tokenIndex
						if !_rules[ruleWS]() {
							goto l625
						}
						goto l626
					l625:
						position, tokenIndex = position625, tokenIndex625
					}
				l626:
					if !_rules[ruleARMVectorRegister]() {
						goto l594
					}
				l627:
					{
						position628, tokenIndex628 := position, tokenIndex
						if buffer[position] != rune(',') {
							goto l628
						}
						position++
						{
							position629, tokenIndex629 := position, tokenIndex
							if !_rules[ruleWS]() {
								goto l629
							}
							goto l630
						l629:
							position, tokenIndex = position629, tokenIndex629
						}
					l630:
						if !_rules[ruleARMVectorRegister]() {
							goto l628
						}
						goto l627
					l628:
						position, tokenIndex = position628, tokenIndex628
					}
					{
						position631, tokenIndex631 := position, tokenIndex
						if !_rules[ruleWS]() {
							goto l631
						}
						goto l632
					l631:
						position, tokenIndex = position631, tokenIndex631
					}
				l632:
					if buffer[position] != rune('}') {
						goto l594
					}
					position++
					{
						position633, tokenIndex633 := position, tokenIndex
						if buffer[position] != rune('[') {
							goto l633
						}
						position++
						if c := buffer[position]; c < rune('0') || c > rune('9') {
							goto l633
						}
						position++
						if buffer[position] != rune(']') {
							goto l633
						}
						position++
						goto l634
					l633:
						position, tokenIndex = position633, tokenIndex633
					}
				l634:
				}
			l596:
				add(ruleARMRegister, position595)
			}
			return true
		l594:
			position, tokenIndex = position594, tokenIndex594
			return false
		},
		/* 39 ARMVectorRegister <- <(('v' / 'V') [0-9] [0-9]? ('.' [0-9]* ('b' / 's' / 'd' / 'h' / 'q') ('[' [0-9] [0-9]? ']')?)?)> */
		func() bool {
			position635, tokenIndex635 := position, tokenIndex
			{
				position636 := position
				{
					position637, tokenIndex637 := position, tokenIndex
					if buffer[position] != rune('v') {
						goto l638
					}
					position++
					goto l637
				l638:
					position, tokenIndex = position637, tokenIndex637
					if buffer[position] != rune('V') {
						goto l635
					}
					position++
				}
			l637:
				if c := buffer[position]; c < rune('0') || c > rune('9') {
					goto l635
				}
				position++
				{
					position639, tokenIndex639 := position, tokenIndex
					if c := buffer[position]; c < rune('0') || c > rune('9') {
						goto l639
					}
					position++
					goto l640
				l639:
					position, tokenIndex = position639, tokenIndex639
				}
			l640:
				{
					position641, tokenIndex641 := position, tokenIndex
					if buffer[position] != rune('.') {
						goto l641
					}
					position++
				l643:
					{
						position644, tokenIndex644 := position, tokenIndex
						if c := buffer[position]; c < rune('0') || c > rune('9') {
							goto l644
						}
						position++
						goto l643
					l644:
						position, tokenIndex = position644, tokenIndex644
					}
					{
						position645, tokenIndex645 := position, tokenIndex
						if buffer[position] != rune('b') {
							goto l646
						}
						position++
						goto l645
					l646:
						position, tokenIndex = position645, tokenIndex645
						if buffer[position] != rune('s') {
							goto l647
						}
						position++
						goto l645
					l647:
						position, tokenIndex = position645, tokenIndex645
						if buffer[position] != rune('d') {
							goto l648
						}
						position++
						goto l645
					l648:
						position, tokenIndex = position645, tokenIndex645
						if buffer[position] != rune('h') {
							goto l649
						}
						position++
						goto l645
					l649:
						position, tokenIndex = position645, tokenIndex645
						if buffer[position] != rune('q') {
							goto l641
						}
						position++
					}
				l645:
					{
						position650, tokenIndex650 := position, tokenIndex
						if buffer[position] != rune('[') {
							goto l650
						}
						position++
						if c := buffer[position]; c < rune('0') || c > rune('9') {
							goto l650
						}
						position++
						{
							position652, tokenIndex652 := position, tokenIndex
							if c := buffer[position]; c < rune('0') || c > rune('9') {
								goto l652
							}
							position++
							goto l653
						l652:
							position, tokenIndex = position652, tokenIndex652
						}
					l653:
						if buffer[position] != rune(']') {
							goto l650
						}
						position++
						goto l651
					l650:
						position, tokenIndex = position650, tokenIndex650
					}
				l651:
					goto l642
				l641:
					position, tokenIndex = position641, tokenIndex641
				}
			l642:
				add(ruleARMVectorRegister, position636)
			}
			return true
		l635:
			position, tokenIndex = position635, tokenIndex635
			return false
		},
		/* 40 MemoryRef <- <((SymbolRef BaseIndexScale) / SymbolRef / Low12BitsSymbolRef / (Offset* BaseIndexScale) / (SegmentRegister Offset BaseIndexScale) / (SegmentRegister BaseIndexScale) / (SegmentRegister Offset) / ARMBaseIndexScale / BaseIndexScale)> */
		func() bool {
			position654, tokenIndex654 := position, tokenIndex
			{
				position655 := position
				{
					position656, tokenIndex656 := position, tokenIndex
					if !_rules[ruleSymbolRef]() {
						goto l657
					}
					if !_rules[ruleBaseIndexScale]() {
						goto l657
					}
					goto l656
				l657:
					position, tokenIndex = position656, tokenIndex656
					if !_rules[ruleSymbolRef]() {
						goto l658
					}
					goto l656
				l658:
					position, tokenIndex = position656, tokenIndex656
					if !_rules[ruleLow12BitsSymbolRef]() {
						goto l659
					}
					goto l656
				l659:
					position, tokenIndex = position656, tokenIndex656
				l661:
					{
						position662, tokenIndex662 := position, tokenIndex
						if !_rules[ruleOffset]() {
							goto l662
						}
						goto l661
					l662:
						position, tokenIndex = position662, tokenIndex662
					}
					if !_rules[ruleBaseIndexScale]() {
						goto l660
					}
					goto l656
				l660:
					position, tokenIndex = position656, tokenIndex656
					if !_rules[ruleSegmentRegister]() {
						goto l663
					}
					if !_rules[ruleOffset]() {
						goto l663
					}
					if !_rules[ruleBaseIndexScale]() {
						goto l663
					}
					goto l656
				l663:
					position, tokenIndex = position656, tokenIndex656
					if !_rules[ruleSegmentRegister]() {
						goto l664
					}
					if !_rules[ruleBaseIndexScale]() {
						goto l664
					}
					goto l656
				l664:
					position, tokenIndex = position656, tokenIndex656
					if !_rules[ruleSegmentRegister]() {
						goto l665
					}
					if !_rules[ruleOffset]() {
						goto l665
					}
					goto l656
				l665:
					position, tokenIndex = position656, tokenIndex656
					if !_rules[ruleARMBaseIndexScale]() {
						goto l666
					}
					goto l656
				l666:
					position, tokenIndex = position656, tokenIndex656
					if !_rules[ruleBaseIndexScale]() {
						goto l654
					}
				}
			l656:
				add(ruleMemoryRef, position655)
			}
			return true
		l654:
			position, tokenIndex = position654, tokenIndex654
			return false
		},
		/* 41 SymbolRef <- <((Offset* '+')? (LocalSymbol / SymbolName) Offset* ('@' Section Offset*)?)> */
		func() bool {
			position667, tokenIndex667 := position, tokenIndex
			{
				position668 := position
				{
					position669, tokenIndex669 := position, tokenIndex
				l671:
					{
						position672, tokenIndex672 := position, tokenIndex
						if !_rules[ruleOffset]() {
							goto l672
						}
						goto l671
					l672:
						position, tokenIndex = position672, tokenIndex672
					}
					if buffer[position] != rune('+') {
						goto l669
					}
					position++
					goto l670
				l669:
					position, tokenIndex = position669, tokenIndex669
				}
			l670:
				{
					position673, tokenIndex673 := position, tokenIndex
					if !_rules[ruleLocalSymbol]() {
						goto l674
					}
					goto l673
				l674:
					position, tokenIndex = position673, tokenIndex673
					if !_rules[ruleSymbolName]() {
						goto l667
					}
				}
			l673:
			l675:
				{
					position676, tokenIndex676 := position, tokenIndex
					if !_rules[ruleOffset]() {
						goto l676
					}
					goto l675
				l676:
					position, tokenIndex = position676, tokenIndex676
				}
				{
					position677, tokenIndex677 := position, tokenIndex
					if buffer[position] != rune('@') {
						goto l677
					}
					position++
					if !_rules[ruleSection]() {
						goto l677
					}
				l679:
					{
						position680, tokenIndex680 := position, tokenIndex
						if !_rules[ruleOffset]() {
							goto l680
						}
						goto l679
					l680:
						position, tokenIndex = position680, tokenIndex680
					}
					goto l678
				l677:
					position, tokenIndex = position677, tokenIndex677
				}
			l678:
				add(ruleSymbolRef, position668)
			}
			return true
		l667:
			position, tokenIndex = position667, tokenIndex667
			return false
		},
		/* 42 Low12BitsSymbolRef <- <(':' ('l' / 'L') ('o' / 'O') '1' '2' ':' (LocalSymbol / SymbolName) Offset?)> */
		func() bool {
			position681, tokenIndex681 := position, tokenIndex
			{
				position682 := position
				if buffer[position] != rune(':') {
					goto l681
				}
				position++
				{
					position683, tokenIndex683 := position, tokenIndex
					if buffer[position] != rune('l') {
						goto l684
					}
					position++
					goto l683
				l684:
					position, tokenIndex = position683, tokenIndex683
					if buffer[position] != rune('L') {
						goto l681
					}
					position++
				}
			l683:
				{
					position685, tokenIndex685 := position, tokenIndex
					if buffer[position] != rune('o') {
						goto l686
					}
					position++
					goto l685
				l686:
					position, tokenIndex = position685, tokenIndex685
					if buffer[position] != rune('O') {
						goto l681
					}
					position++
				}
			l685:
				if buffer[position] != rune('1') {
					goto l681
				}
				position++
				if buffer[position] != rune('2') {
					goto l681
				}
				position++
				if buffer[position] != rune(':') {
					goto l681
				}
				position++
				{
					position687, tokenIndex687 := position, tokenIndex
					if !_rules[ruleLocalSymbol]() {
						goto l688
					}
					goto l687
				l688:
					position, tokenIndex = position687, tokenIndex687
					if !_rules[ruleSymbolName]() {
						goto l681
					}
				}
			l687:
				{
					position689, tokenIndex689 := position, tokenIndex
					if !_rules[ruleOffset]() {
						goto l689
					}
					goto l690
				l689:
					position, tokenIndex = position689, tokenIndex689
				}
			l690:
				add(ruleLow12BitsSymbolRef, position682)
			}
			return true
		l681:
			position, tokenIndex = position681, tokenIndex681
			return false
		},
		/* 43 ARMBaseIndexScale <- <('[' ARMRegister (',' WS? (('#' Offset ('*' [0-9]+)?) / ARMGOTLow12 / Low12BitsSymbolRef / ARMRegister) (',' WS? ARMConstantTweak)?)? ']' ARMPostincrement?)> */
		func() bool {
			position691, tokenIndex691 := position, tokenIndex
			{
				position692 := position
				if buffer[position] != rune('[') {
					goto l691
				}
				position++
				if !_rules[ruleARMRegister]() {
					goto l691
				}
				{
					position693, tokenIndex693 := position, tokenIndex
					if buffer[position] != rune(',') {
						goto l693
					}
					position++
					{
						position695, tokenIndex695 := position, tokenIndex
						if !_rules[ruleWS]() {
							goto l695
						}
						goto l696
					l695:
						position, tokenIndex = position695, tokenIndex695
					}
				l696:
					{
						position697, tokenIndex697 := position, tokenIndex
						if buffer[position] != rune('#') {
							goto l698
						}
						position++
						if !_rules[ruleOffset]() {
							goto l698
						}
						{
							position699, tokenIndex699 := position, tokenIndex
							if buffer[position] != rune('*') {
								goto l699
							}
							position++
							if c := buffer[position]; c < rune('0') || c > rune('9') {
								goto l699
							}
							position++
						l701:
							{
								position702, tokenIndex702 := position, tokenIndex
								if c := buffer[position]; c < rune('0') || c > rune('9') {
									goto l702
								}
								position++
								goto l701
							l702:
								position, tokenIndex = position702, tokenIndex702
							}
							goto l700
						l699:
							position, tokenIndex = position699, tokenIndex699
						}
					l700:
						goto l697
					l698:
						position, tokenIndex = position697, tokenIndex697
						if !_rules[ruleARMGOTLow12]() {
							goto l703
						}
						goto l697
					l703:
						position, tokenIndex = position697, tokenIndex697
						if !_rules[ruleLow12BitsSymbolRef]() {
							goto l704
						}
						goto l697
					l704:
						position, tokenIndex = position697, tokenIndex697
						if !_rules[ruleARMRegister]() {
							goto l693
						}
					}
				l697:
					{
						position705, tokenIndex705 := position, tokenIndex
						if buffer[position] != rune(',') {
							goto l705
						}
						position++
						{
							position707, tokenIndex707 := position, tokenIndex
							if !_rules[ruleWS]() {
								goto l707
							}
							goto l708
						l707:
							position, tokenIndex = position707, tokenIndex707
						}
					l708:
						if !_rules[ruleARMConstantTweak]() {
							goto l705
						}
						goto l706
					l705:
						position, tokenIndex = position705, tokenIndex705
					}
				l706:
					goto l694
				l693:
					position, tokenIndex = position693, tokenIndex693
				}
			l694:
				if buffer[position] != rune(']') {
					goto l691
				}
				position++
				{
					position709, tokenIndex709 := position, tokenIndex
					if !_rules[ruleARMPostincrement]() {
						goto l709
					}
					goto l710
				l709:
					position, tokenIndex = position709, tokenIndex709
				}
			l710:
				add(ruleARMBaseIndexScale, position692)
			}
			return true
		l691:
			position, tokenIndex = position691, tokenIndex691
			return false
		},
		/* 44 ARMGOTLow12 <- <(':' ('g' / 'G') ('o' / 'O') ('t' / 'T') '_' ('l' / 'L') ('o' / 'O') '1' '2' ':' SymbolName)> */
		func() bool {
			position711, tokenIndex711 := position, tokenIndex
			{
				position712 := position
				if buffer[position] != rune(':') {
					goto l711
				}
				position++
				{
					position713, tokenIndex713 := position, tokenIndex
					if buffer[position] != rune('g') {
						goto l714
					}
					position++
					goto l713
				l714:
					position, tokenIndex = position713, tokenIndex713
					if buffer[position] != rune('G') {
						goto l711
					}
					position++
				}
			l713:
				{
					position715, tokenIndex715 := position, tokenIndex
					if buffer[position] != rune('o') {
						goto l716
					}
					position++
					goto l715
				l716:
					position, tokenIndex = position715, tokenIndex715
					if buffer[position] != rune('O') {
						goto l711
					}
					position++
				}
			l715:
				{
					position717, tokenIndex717 := position, tokenIndex
					if buffer[position] != rune('t') {
						goto l718
					}
					position++
					goto l717
				l718:
					position, tokenIndex = position717, tokenIndex717
					if buffer[position] != rune('T') {
						goto l711
					}
					position++
				}
			l717:
				if buffer[position] != rune('_') {
					goto l711
				}
				position++
				{
					position719, tokenIndex719 := position, tokenIndex
					if buffer[position] != rune('l') {
						goto l720
					}
					position++
					goto l719
				l720:
					position, tokenIndex = position719, tokenIndex719
					if buffer[position] != rune('L') {
						goto l711
					}
					position++
				}
			l719:
				{
					position721, tokenIndex721 := position, tokenIndex
					if buffer[position] != rune('o') {
						goto l722
					}
					position++
					goto l721
				l722:
					position, tokenIndex = position721, tokenIndex721
					if buffer[position] != rune('O') {
						goto l711
					}
					position++
				}
			l721:
				if buffer[position] != rune('1') {
					goto l711
				}
				position++
				if buffer[position] != rune('2') {
					goto l711
				}
				position++
				if buffer[position] != rune(':') {
					goto l711
				}
				position++
				if !_rules[ruleSymbolName]() {
					goto l711
				}
				add(ruleARMGOTLow12, position712)
			}
			return true
		l711:
			position, tokenIndex = position711, tokenIndex711
			return false
		},
		/* 45 ARMPostincrement <- <'!'> */
		func() bool {
			position723, tokenIndex723 := position, tokenIndex
			{
				position724 := position
				if buffer[position] != rune('!') {
					goto l723
				}
				position++
				add(ruleARMPostincrement, position724)
			}
			return true
		l723:
			position, tokenIndex = position723, tokenIndex723
			return false
		},
		/* 46 BaseIndexScale <- <('(' RegisterOrConstant? WS? (',' WS? RegisterOrConstant WS? (',' [0-9]+)?)? ')')> */
		func() bool {
			position725, tokenIndex725 := position, tokenIndex
			{
				position726 := position
				if buffer[position] != rune('(') {
					goto l725
				}
				position++
				{
					position727, tokenIndex727 := position, tokenIndex
					if !_rules[ruleRegisterOrConstant]() {
						goto l727
					}
					goto l728
				l727:
					position, tokenIndex = position727, tokenIndex727
				}
			l728:
				{
					position729, tokenIndex729 := position, tokenIndex
					if !_rules[ruleWS]() {
						goto l729
					}
					goto l730
				l729:
					position, tokenIndex = position729, tokenIndex729
				}
			l730:
				{
					position731, tokenIndex731 := position, tokenIndex
					if buffer[position] != rune(',') {
						goto l731
					}
					position++
					{
						position733, tokenIndex733 := position, tokenIndex
						if !_rules[ruleWS]() {
							goto l733
						}
						goto l734
					l733:
						position, tokenIndex = position733, tokenIndex733
					}
				l734:
					if !_rules[ruleRegisterOrConstant]() {
						goto l731
					}
					{
						position735, tokenIndex735 := position, tokenIndex
						if !_rules[ruleWS]() {
							goto l735
						}
						goto l736
					l735:
						position, tokenIndex = position735, tokenIndex735
					}
				l736:
					{
						position737, tokenIndex737 := position, tokenIndex
						if buffer[position] != rune(',') {
							goto l737
						}
						position++
						if c := buffer[position]; c < rune('0') || c > rune('9') {
							goto l737
						}
						position++
					l739:
						{
							position740, tokenIndex740 := position, tokenIndex
							if c := buffer[position]; c < rune('0') || c > rune('9') {
								goto l740
							}
							position++
							goto l739
						l740:
							position, tokenIndex = position740, tokenIndex740
						}
						goto l738
					l737:
						position, tokenIndex = position737, tokenIndex737
					}
				l738:
					goto l732
				l731:
					position, tokenIndex = position731, tokenIndex731
				}
			l732:
				if buffer[position] != rune(')') {
					goto l725
				}
				position++
				add(ruleBaseIndexScale, position726)
			}
			return true
		l725:
			position, tokenIndex = position725, tokenIndex725
			return false
		},
		/* 47 Operator <- <('+' / '-')> */
		func() bool {
			position741, tokenIndex741 := position, tokenIndex
			{
				position742 := position
				{
					position743, tokenIndex743 := position, tokenIndex
					if buffer[position] != rune('+') {
						goto l744
					}
					position++
					goto l743
				l744:
					position, tokenIndex = position743, tokenIndex743
					if buffer[position] != rune('-') {
						goto l741
					}
					position++
				}
			l743:
				add(ruleOperator, position742)
			}
			return true
		l741:
			position, tokenIndex = position741, tokenIndex741
			return false
		},
		/* 48 Offset <- <('+'? '-'? (('0' ('b' / 'B') ('0' / '1')+) / ('0' ('x' / 'X') ([0-9] / [0-9] / ([a-f] / [A-F]))+) / [0-9]+))> */
		func() bool {
			position745, tokenIndex745 := position, tokenIndex
			{
				position746 := position
				{
					position747, tokenIndex747 := position, tokenIndex
					if buffer[position] != rune('+') {
						goto l747
					}
					position++
					goto l748
				l747:
					position, tokenIndex = position747, tokenIndex747
				}
			l748:
				{
					position749, tokenIndex749 := position, tokenIndex
					if buffer[position] != rune('-') {
						goto l749
					}
					position++
					goto l750
				l749:
					position, tokenIndex = position749, tokenIndex749
				}
			l750:
				{
					position751, tokenIndex751 := position, tokenIndex
					if buffer[position] != rune('0') {
						goto l752
					}
					position++
					{
						position753, tokenIndex753 := position, tokenIndex
						if buffer[position] != rune('b') {
							goto l754
						}
						position++
						goto l753
					l754:
						position, tokenIndex = position753, tokenIndex753
						if buffer[position] != rune('B') {
							goto l752
						}
						position++
					}
				l753:
					{
						position757, tokenIndex757 := position, tokenIndex
						if buffer[position] != rune('0') {
							goto l758
						}
						position++
						goto l757
					l758:
						position, tokenIndex = position757, tokenIndex757
						if buffer[position] != rune('1') {
							goto l752
						}
						position++
					}
				l757:
				l755:
					{
						position756, tokenIndex756 := position, tokenIndex
						{
							position759, tokenIndex759 := position, tokenIndex
							if buffer[position] != rune('0') {
								goto l760
							}
							position++
							goto l759
						l760:
							position, tokenIndex = position759, tokenIndex759
							if buffer[position] != rune('1') {
								goto l756
							}
							position++
						}
					l759:
						goto l755
					l756:
						position, tokenIndex = position756, tokenIndex756
					}
					goto l751
				l752:
					position, tokenIndex = position751, tokenIndex751
					if buffer[position] != rune('0') {
						goto l761
					}
					position++
					{
						position762, tokenIndex762 := position, tokenIndex
						if buffer[position] != rune('x') {
							goto l763
						}
						position++
						goto l762
					l763:
						position, tokenIndex = position762, tokenIndex762
						if buffer[position] != rune('X') {
							goto l761
						}
						position++
					}
				l762:
					{
						position766, tokenIndex766 := position, tokenIndex
						if c := buffer[position]; c < rune('0') || c > rune('9') {
							goto l767
						}
						position++
						goto l766
					l767:
						position, tokenIndex = position766, tokenIndex766
						if c := buffer[position]; c < rune('0') || c > rune('9') {
							goto l768
						}
						position++
						goto l766
					l768:
						position, tokenIndex = position766, tokenIndex766
						{
							position769, tokenIndex769 := position, tokenIndex
							if c := buffer[position]; c < rune('a') || c > rune('f') {
								goto l770
							}
							position++
							goto l769
						l770:
							position, tokenIndex = position769, tokenIndex769
							if c := buffer[position]; c < rune('A') || c > rune('F') {
								goto l761
							}
							position++
						}
					l769:
					}
				l766:
				l764:
					{
						position765, tokenIndex765 := position, tokenIndex
						{
							position771, tokenIndex771 := position, tokenIndex
							if c := buffer[position]; c < rune('0') || c > rune('9') {
								goto l772
							}
							position++
							goto l771
						l772:
							position, tokenIndex = position771, tokenIndex771
							if c := buffer[position]; c < rune('0') || c > rune('9') {
								goto l773
							}
							position++
							goto l771
						l773:
							position, tokenIndex = position771, tokenIndex771
							{
								position774, tokenIndex774 := position, tokenIndex
								if c := buffer[position]; c < rune('a') || c > rune('f') {
									goto l775
								}
								position++
								goto l774
							l775:
								position, tokenIndex = position774, tokenIndex774
								if c := buffer[position]; c < rune('A') || c > rune('F') {
									goto l765
								}
								position++
							}
						l774:
						}
					l771:
						goto l764
					l765:
						position, tokenIndex = position765, tokenIndex765
					}
					goto l751
				l761:
					position, tokenIndex = position751, tokenIndex751
					if c := buffer[position]; c < rune('0') || c > rune('9') {
						goto l745
					}
					position++
				l776:
					{
						position777, tokenIndex777 := position, tokenIndex
						if c := buffer[position]; c < rune('0') || c > rune('9') {
							goto l777
						}
						position++
						goto l776
					l777:
						position, tokenIndex = position777, tokenIndex777
					}
				}
			l751:
				add(ruleOffset, position746)
			}
			return true
		l745:
			position, tokenIndex = position745, tokenIndex745
			return false
		},
		/* 49 Section <- <([a-z] / [A-Z] / '@')+> */
		func() bool {
			position778, tokenIndex778 := position, tokenIndex
			{
				position779 := position
				{
					position782, tokenIndex782 := position, tokenIndex
					if c := buffer[position]; c < rune('a') || c > rune('z') {
						goto l783
					}
					position++
					goto l782
				l783:
					position, tokenIndex = position782, tokenIndex782
					if c := buffer[position]; c < rune('A') || c > rune('Z') {
						goto l784
					}
					position++
					goto l782
				l784:
					position, tokenIndex = position782, tokenIndex782
					if buffer[position] != rune('@') {
						goto l778
					}
					position++
				}
			l782:
			l780:
				{
					position781, tokenIndex781 := position, tokenIndex
					{
						position785, tokenIndex785 := position, tokenIndex
						if c := buffer[position]; c < rune('a') || c > rune('z') {
							goto l786
						}
						position++
						goto l785
					l786:
						position, tokenIndex = position785, tokenIndex785
						if c := buffer[position]; c < rune('A') || c > rune('Z') {
							goto l787
						}
						position++
						goto l785
					l787:
						position, tokenIndex = position785, tokenIndex785
						if buffer[position] != rune('@') {
							goto l781
						}
						position++
					}
				l785:
					goto l780
				l781:
					position, tokenIndex = position781, tokenIndex781
				}
				add(ruleSection, position779)
			}
			return true
		l778:
			position, tokenIndex = position778, tokenIndex778
			return false
		},
		/* 50 SegmentRegister <- <('%' ([c-g] / 's') ('s' ':'))> */
		func() bool {
			position788, tokenIndex788 := position, tokenIndex
			{
				position789 := position
				if buffer[position] != rune('%') {
					goto l788
				}
				position++
				{
					position790, tokenIndex790 := position, tokenIndex
					if c := buffer[position]; c < rune('c') || c > rune('g') {
						goto l791
					}
					position++
					goto l790
				l791:
					position, tokenIndex = position790, tokenIndex790
					if buffer[position] != rune('s') {
						goto l788
					}
					position++
				}
			l790:
				if buffer[position] != rune('s') {
					goto l788
				}
				position++
				if buffer[position] != rune(':') {
					goto l788
				}
				position++
				add(ruleSegmentRegister, position789)
			}
			return true
		l788:
			position, tokenIndex = position788, tokenIndex788
			return false
		},
	}
	p.rules = _rules
}
