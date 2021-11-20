#include <c64/charwin.h>
#include <c64/kernalio.h>
#include <string.h>
#include <stdlib.h>
#include <setjmp.h>
#include <math.h>

#pragma region(main, 0x0a00, 0xd000, , , {code, data, bss, heap, stack} )

CharWin	InputWindow, OutputWindow, StatusWindow;
char	InputBuffer[160];
jmp_buf	MainLoop;

enum CellType
{
	CT_SYMBOL,	// Must be zero to act as a terminator during search
	CT_CONS,
	CT_NUMBER,
	CT_BUILTIN,
	CT_LAMBDA,
	CT_QUOTE,
	CT_FREE
};

struct Cell;

typedef struct Cell * (* Builtin)(struct Cell ** scope, struct Cell * cell);

struct Cell
{
	CellType	type;
	union 
	{
		struct Cell	* car, * cdr;
		float		 value;
		Builtin		 builtin;
	}	u;
};

#define NUM_CELLS	2000

struct Cell		CellStorage[NUM_CELLS];
struct Cell	*	FreeCell, * GlobalScope, * TrueCell;
int				NumFreeCells;

char		SymbolStore[4096];
char		Propeller[4] = {192, 205, 221, 206};
char		PropIndex;

void fnumber(float f, char * buffer)
{
	char 	* 	sp = buffer;

	char	d = 0;

	if (f < 0.0)
	{
		f = -f;
		sp[d++] = '-';
	}
		
	int	exp = 0;

	bool fraction = false;
	if (f != floor(f) || f >= 10000000.0)
		fraction = true;

	if (f != 0.0)
	{
		while (f >= 1000.0)
		{
			f /= 1000;
			exp += 3;
		}

		while (f < 1.0)
		{
			f *= 1000;
			exp -= 3;
		}

		while (f >= 10.0)
		{
			f /= 10;
			exp ++;
		}
		
	}
	
	bool	fexp = false;
	char	fdigits = 0;
	
	if (fraction)
	{
		fexp = exp > 6 || exp < 0;
		if (fexp)
			fdigits = 6;
		else
			fdigits = 6 - exp;
	}

	char 	digits = fdigits + 1;

	if (!fexp)
	{		
		while (exp < 0)
		{
			f /= 10.0;
			exp++;
		}
		digits = fdigits + exp + 1;

		float	r = 0.5;
		for(char i=1; i<digits; i++)
			r /= 10.0;
		f += r;
		if (f >= 10.0)
		{
			f /= 10.0;
			fdigits--;
		}		
	}
	else
	{
		float	r = 0.5;
		for(char i=0; i<fdigits; i++)
			r /= 10.0;
		f += r;
		if (f >= 10.0)
		{
			f /= 10.0;
			exp ++;
		}
	}
	

	char	pdigits = digits - fdigits;

	if (digits > 20)
		digits = 20;

	if (pdigits == 0)
		sp[d++] = '0';

	for(char i=0; i<digits; i++)
	{
		if (i == pdigits)
			sp[d++] = '.';
		int c = (int)f;
		f -= (float)c;
		f *= 10.0;
		sp[d++] = c + '0';
	}

	if (fexp)
	{
		sp[d++] = 'E';
		if (exp < 0)
		{
			sp[d++] = '-';
			exp = -exp;
		}
		else
			sp[d++] = '+';
		
		sp[d++] = exp / 10 + '0'; 
		sp[d++] = exp % 10 + '0';		
	}

	sp[d++] = 0;
}

#pragma native(fnumber)

void checkScroll(CharWin * win)
{
	if (win->cy == win->wy)
	{
		win->cy--;
		cwin_scroll_up(win, 1);
		cwin_fill_rect(win, 0, win->wy - 1, win->wx, 1, ' ', 1);
	}		
}

void printLn(CharWin * win)
{
	win->cy++;
	win->cx = 0;
	checkScroll(win);
}

void print(CharWin * win, const char * str, char color)
{	
	char len = strlen(str);
	if (win->cx + len > 40)
		printLn(win);

	cwin_put_string(win, str, color);
	checkScroll(win);
}

void error(const char * msg)
{
	printLn(&OutputWindow);
	print(&OutputWindow, msg, 0);
	printLn(&OutputWindow);
	longjmp(MainLoop, 1);	
}

bool checkStack(void)
{
	__asm
	{
		ldx	#0
		stx accu + 1
		lda	__sp + 1
		cmp #$90
		bcs w1
		inx
	w1:	stx accu		
	}
}

#pragma native(checkStack)

void propeller(void)
{
	cwin_putat_char(&StatusWindow, 1, 0, Propeller[(PropIndex++) &3], 14);
}

void initCells(void)
{
	for(int i=0; i<NUM_CELLS; i++)
	{
		CellStorage[i].u.car = CellStorage + i + 1;
		CellStorage[i].type = CT_FREE;
	}
	CellStorage[NUM_CELLS-1].u.car = nullptr;
	FreeCell = CellStorage;
	NumFreeCells = NUM_CELLS;
}

struct Cell * allocCell(CellType type)
{
	if (!FreeCell)
		error("*** OUT OF MEMORY ***");
	
	NumFreeCells--;
	Cell	*	cell = FreeCell;
	FreeCell = cell->u.car;
	cell->type = type;
	return cell;
}

void gcMark(Cell * cell)
{
	CellType	type = cell->type;
	
	if (type && !(type & 0x80))
	{
		cell->type |= 0x80;
		switch (type)
		{
			case CT_CONS:
			case CT_LAMBDA:
				gcMark(cell->u.car);
				gcMark(cell->u.cdr);
				break;
			case CT_QUOTE:
				gcMark(cell->u.car);
				break;				
		}
	}
}

void gcSweep(void)
{
	struct Cell * cell = CellStorage, * free = nullptr;
	int num = 0;	
		
	for(int i=0; i<NUM_CELLS; i++)
	{
		if (cell->type & 0x80)
			cell->type &= 0x7f;
		else
		{
			cell->type = CT_FREE;
			cell->u.car = free;
			free = cell;
			num++;
		}
		cell++;
	}	
	
	FreeCell = free;
	NumFreeCells = num;
}

#pragma native(gcSweep)

void garbageCollect(void)
{
	gcMark(GlobalScope);
	gcSweep();
	
}


struct Cell * findSymbol(const char * sym)
{
	char * sp = SymbolStore + 1;
	while (sp[0])
	{
		char i = 0;
		while (sym[i] && sp[i] == sym[i])
			i++;
		if (sym[i] == sp[i])
			return (Cell *)(sp - 1);
		while (sp[i])
			i++;
		sp += i + 1;
	}
	strcpy(sp, sym);
	return (Cell *)(sp - 1);
}

struct Cell * llookup(struct Cell * scope, struct Cell * symbol)
{
	while (scope)
	{
		struct Cell	*	e = scope->u.car;
		if (e->u.car == symbol)
			return e;
		scope = scope->u.cdr;
	}
	return nullptr;
}

#pragma native(llookup)

struct Cell * lookup(struct Cell * scope, struct Cell * symbol)
{
	Cell	*	e = llookup(scope, symbol);
	if (e)
		return e->u.cdr;
	else
		return symbol;
}

inline struct Cell * cons(struct Cell * car, struct Cell * cdr)
{
	struct Cell * c = allocCell(CT_CONS);
	c->u.car = car;
	c->u.cdr = cdr;
	return c;
}

inline struct Cell * car(struct Cell * cell)
{
	if (cell && cell->type == CT_CONS)
		return cell->u.car;
	else
		return nullptr;
}

inline struct Cell * cdr(struct Cell * cell)
{
	if (cell && cell->type == CT_CONS)
		return cell->u.cdr;
	else
		return nullptr;
}

inline float number(struct Cell * cell)
{
	if (cell && cell->type == CT_NUMBER)
		return cell->u.value;
	else
		return 0.0;
}

inline struct Cell * allocNumber(float value)
{
	struct Cell * cell = allocCell(CT_NUMBER);
	cell->u.value = value;
	return cell;
}

int InputPos;

struct float parseNumber(void)
{
	float value = 0, scale = 1;
	
	char ch = InputBuffer[InputPos];
	while (ch >= '0' && ch <= '9')
	{
		value = value * 10 + (ch - '0');
		ch = InputBuffer[++InputPos];
	}
	if (ch == '.')
	{
		ch = InputBuffer[++InputPos];				
		while (ch >= '0' && ch <= '9')
		{
			value = value * 10 + (float)(ch - '0');
			scale *= 10;
			ch = InputBuffer[++InputPos];
		}
		
		value /= scale;
	}
	
	return value;
}

struct Cell * parseAtom(bool list)
{
	propeller();
	
	char	buffer[30];
	char	ch;
	struct Cell	*	cell = nullptr;
	
	ch = InputBuffer[InputPos];
	while (ch == ' ' || ch == 160)
		ch = InputBuffer[++InputPos];
	
	if (ch == '-')
	{
		ch = InputBuffer[++InputPos];
		if (ch >= '0' && ch <= '9')
			cell = allocNumber(- parseNumber());
		else
			cell = findSymbol("-");				
	}
	else if (ch >= '0' && ch <= '9')
		cell = allocNumber(parseNumber());
	else if (ch == '(')
	{
		++InputPos;
		cell = parseAtom(true);
	}	
	else if (ch == ')')
	{
		++InputPos;
		return nullptr;
	}
	else if (ch == ''')
	{
		++InputPos;
		cell = allocCell(CT_QUOTE);
		cell->u.car = parseAtom(false);
	}
	else if (ch)
	{
		char i = 0;
		do	{
			buffer[i++] = ch;
			ch = InputBuffer[++InputPos];
		} while (ch && ch != '(' && ch != ')' && ch != ' ' && ch != 160 && ch != ''');
		buffer[i] = 0;
		cell = findSymbol(buffer);		
	}
	else
		return nullptr;
	
	if (list)
		cell = cons(cell, parseAtom(true));
	
	return cell;
}


struct Cell * evalLambda(struct Cell * lambda, struct Cell ** scope, struct Cell * cell);

struct Cell * evalAtom(struct Cell ** scope, struct Cell * cell)
{
	switch (cell->type)
	{
		case CT_SYMBOL:			
			return lookup(*scope, cell);

		case CT_QUOTE:
			return cell->u.car;
			
		case CT_CONS:
		{
			struct Cell * f = evalAtom(scope, cell->u.car);
			if (f->type == CT_BUILTIN)
				return f->u.builtin(scope, cell->u.cdr);
			else if (f->type == CT_LAMBDA)
				return evalLambda(f, scope, cell->u.cdr);
			else
				return cell;				
		}
		
		default:
			return cell;
	}
}

struct Cell * evalLambda(struct Cell * lambda, struct Cell ** scope, struct Cell * cell)
{
	if (checkStack())
		error("*** STACK UNDERFLOW ***");

	propeller();
	
	struct Cell	*	nscope = *scope;
	
	struct Cell	*	args = lambda->u.car;
	
	while (args)
	{
		nscope = cons(cons(args->u.car, evalAtom(scope, car(cell))), nscope);
		args = args->u.cdr;
		cell = cdr(cell);
	}
	
	Cell	*	stmt = lambda->u.cdr;
	cell = nullptr;
	while (stmt)
	{		
		cell = evalAtom(&nscope, stmt->u.car);
		stmt = stmt->u.cdr;
	}
	
	return cell;	
}

float evalNumber(struct Cell ** scope, struct Cell * cell)
{
	return number(evalAtom(scope, car(cell)));
}

float evalNumber2(struct Cell ** scope, struct Cell * cell)
{
	return number(evalAtom(scope, car(cdr(cell))));
}

struct Cell * builtinAdd(struct Cell ** scope, struct Cell * cell)
{
	float	sum = evalNumber(scope, cell);
	cell = cell->u.cdr;
	
	while (cell)
	{
		sum += evalNumber(scope, cell);
		cell = cell->u.cdr;
	}
	
	return allocNumber(sum);
}

struct Cell * builtinMul(struct Cell ** scope, struct Cell * cell)
{
	float	sum = evalNumber(scope, cell);
	cell = cell->u.cdr;
	
	while (cell)
	{
		sum *= evalNumber(scope, cell);
		cell = cell->u.cdr;
	}
	
	return allocNumber(sum);
}

struct Cell * builtinSub(struct Cell ** scope, struct Cell * cell)
{
	float	sum = evalNumber(scope, cell);
	cell = cell->u.cdr;
	
	while (cell)
	{
		sum -= evalNumber(scope, cell);
		cell = cell->u.cdr;
	}
	
	return allocNumber(sum);
}

struct Cell * builtinDiv(struct Cell ** scope, struct Cell * cell)
{
	float	sum = evalNumber(scope, cell);
	cell = cell->u.cdr;
	
	while (cell)
	{
		sum /= evalNumber(scope, cell);
		cell = cell->u.cdr;
	}
	
	return allocNumber(sum);
}

struct Cell * builtinEqual(struct Cell ** scope, struct Cell * cell)
{
	if (evalNumber(scope, cell) == evalNumber2(scope, cell))
		return TrueCell;
	else
		return nullptr;
}

struct Cell * builtinNotEqual(struct Cell ** scope, struct Cell * cell)
{
	if (evalNumber(scope, cell) != evalNumber2(scope, cell))
		return TrueCell;
	else
		return nullptr;
}

struct Cell * builtinLess(struct Cell ** scope, struct Cell * cell)
{
	if (evalNumber(scope, cell) < evalNumber2(scope, cell))
		return TrueCell;
	else
		return nullptr;
}

struct Cell * builtinGreater(struct Cell ** scope, struct Cell * cell)
{
	if (evalNumber(scope, cell) > evalNumber2(scope, cell))
		return TrueCell;
	else
		return nullptr;
}

struct Cell * builtinLessEqual(struct Cell ** scope, struct Cell * cell)
{
	if (evalNumber(scope, cell) <= evalNumber2(scope, cell))
		return TrueCell;
	else
		return nullptr;
}

struct Cell * builtinGreaterEqual(struct Cell ** scope, struct Cell * cell)
{
	if (evalNumber(scope, cell) >= evalNumber2(scope, cell))
		return TrueCell;
	else
		return nullptr;
}


struct Cell * builtinCons(struct Cell ** scope, struct Cell * cell)
{	
	return cons(evalAtom(scope, car(cell)), evalAtom(scope, car(cdr(cell))));
}

struct Cell * builtinCar(struct Cell ** scope, struct Cell * cell)
{	
	return car(evalAtom(scope, car(cell)));
}

struct Cell * builtinCdr(struct Cell ** scope, struct Cell * cell)
{	
	return cdr(evalAtom(scope, car(cell)));
}

struct Cell * builtinSetq(struct Cell ** scope, struct Cell * cell)
{	
	Cell * val = evalAtom(scope, car(cdr(cell)));
	Cell * sym = car(cell);
	Cell * var = llookup(*scope, sym);
	
	if (var)
		var->u.cdr = val;
	else	
		*scope = cons(cons(sym, val), *scope);	
	return val;
}

struct Cell * builtinLambda(struct Cell ** scope, struct Cell * cell)
{	
	Cell	*	lambda = allocCell(CT_LAMBDA);
	lambda->u.car = car(cell);
	lambda->u.cdr = cdr(cell);
	
	return lambda;
}

struct Cell * builtinDefun(struct Cell ** scope, struct Cell * cell)
{	
	Cell * sym = car(cell);
	Cell * body = cdr(cell);

	Cell	*	lambda = allocCell(CT_LAMBDA);
	lambda->u.car = car(body);
	lambda->u.cdr = cdr(body);

	Cell * var = llookup(*scope, sym);	
	if (var)
		var->u.cdr = lambda;
	else	
		*scope = cons(cons(sym, lambda), *scope);	
	
	return lambda;	
}

struct Cell * builtinIf(struct Cell ** scope, struct Cell * cell)
{	
	Cell	*	cond = evalAtom(scope, car(cell));
	if (cond)
		return evalAtom(scope, car(cdr(cell)));
	else
		return evalAtom(scope, car(cdr(cdr(cell))));
}

struct Cell * builtinLet(struct Cell ** scope, struct Cell * cell)
{
	struct Cell	*	nscope = *scope;
	
	struct Cell	*	vars = car(cell);
	
	while (vars)
	{
		struct Cell * var = car(vars);
		
		nscope = cons(cons(var->u.car, evalAtom(scope, car(cdr(var)))), nscope);
		
		vars = cdr(vars);
	}
	
	Cell	*	stmt = cdr(cell);
	
	cell = nullptr;
	while (stmt)
	{		
		cell = evalAtom(&nscope, stmt->u.car);
		stmt = stmt->u.cdr;
	}
	
	return cell;	

}

void saveAtom(char fnum, struct Cell * cell)
{
	char buffer[20];
	
	switch (cell->type)
	{
		case CT_NUMBER:
			fnumber(cell->u.value, buffer);
			krnio_puts(fnum, buffer);
			break;
		case CT_SYMBOL:
			krnio_puts(fnum, (char *)cell + 1);
			break;
		case CT_CONS:
			krnio_puts(fnum, "(");
			while (cell)
			{
				if (cell->type == CT_CONS)
				{
					saveAtom(fnum, cell->u.car);				
					cell = cell->u.cdr;
				
					if (cell)
						krnio_puts(fnum, " ");
				}
				else
				{
					krnio_puts(fnum, ". ");
					saveAtom(fnum, cell);
					cell = nullptr;
				}
			}
			krnio_puts(fnum, ")");
			break;
		case CT_BUILTIN:
			krnio_puts(fnum, "$$");
			break;
		case CT_QUOTE:
			krnio_puts(fnum, "'");
			saveAtom(fnum, cell->u.car);
			break;
		case CT_LAMBDA:
			krnio_puts(fnum, "(LAMBDA ");
			while (cell)
			{
				saveAtom(fnum, cell->u.car);
				cell = cell->u.cdr;
				if (cell)
					krnio_puts(fnum, " ");
			}
			krnio_puts(fnum, ")");
			break;
	}
}

struct Cell * builtinSave(struct Cell ** scope, struct Cell * cell)
{
	char fnum = 3;
	
	krnio_setnam("@0:TESTLISP,S,W");
	if (krnio_open(fnum, 9, 3))
	{
		struct Cell * sc = *scope;
		while (sc)
		{
			struct Cell * e = sc->u.car;
			struct Cell * sym = e->u.car;
			struct Cell * value = e->u.cdr;
			
			switch (value->type)
			{
				case CT_LAMBDA:
					krnio_puts(fnum, "(DEFUN ");
					krnio_puts(fnum, (char *)sym + 1);
					krnio_puts(fnum, " ");
					while (value)
					{
						saveAtom(fnum, value->u.car);
						value = value->u.cdr;
						if (value)
							krnio_puts(fnum, " ");
					}
					krnio_puts(fnum, ")\n");
					break;
				case CT_CONS:
				case CT_NUMBER:
				case CT_SYMBOL:
				case CT_QUOTE:
					krnio_puts(fnum, "(SETQ ");
					krnio_puts(fnum, (char *)sym + 1);
					krnio_puts(fnum, " '");
					saveAtom(fnum, value);
					krnio_puts(fnum, ")\n");
					break;
				default:
					break;
			}
			
			sc = sc->u.cdr;
		}
		krnio_close(3);
		return TrueCell;
	}
	else
		return nullptr;
}

struct Cell * builtinLoad(struct Cell ** scope, struct Cell * cell)
{	
	char fnum = 2;
	
	krnio_setnam("0:TESTLISP,S,R");
	if (krnio_open(fnum, 9, 2))
	{
		while (krnio_gets(fnum, InputBuffer, 160) > 0)
		{
			InputPos = 0;
			struct Cell * cell = parseAtom(false);
			cell = evalAtom(&GlobalScope, cell);
		}
		krnio_close(fnum);
		return TrueCell;
	}

	return nullptr;
}

struct Cell * builtinList(struct Cell ** scope, struct Cell * cell)
{	
	Cell	*	r = nullptr;
	Cell	*	e = *scope;
	while (e)
	{
		r = cons(e->u.car->u.car, r);
		e = e->u.cdr;
	}
	return r;
}

void initBuiltins(void);

struct Cell * builtinReset(struct Cell ** scope, struct Cell * cell)
{
	initBuiltins();
	
	return nullptr;
}


void printAtom(CharWin * win, struct Cell * cell, char color)
{
	char buffer[20];
	
	switch (cell->type)
	{
		case CT_NUMBER:
			fnumber(cell->u.value, buffer);
			print(win, buffer, color);
			break;
		case CT_SYMBOL:
			print(win, (char *)cell + 1, color);
			break;
		case CT_CONS:
			print(win, "(", color);
			while (cell)
			{
				if (cell->type == CT_CONS)
				{
					printAtom(win, cell->u.car, color);				
					cell = cell->u.cdr;
				
					if (cell)
						print(win, " ", color);
				}
				else
				{
					print(win, ". ", color);
					printAtom(win, cell, color);
					cell = nullptr;
				}
			}
			print(win, ")", color);
			break;
		case CT_BUILTIN:
			print(win, "$$", color);
			break;
		case CT_QUOTE:
			print(win, "'", color);
			printAtom(win, cell->u.car, color);
			break;
		case CT_LAMBDA:
			print(win, "(LAMBDA ", color);
			while (cell)
			{
				printAtom(win, cell->u.car, color);				
				cell = cell->u.cdr;
				if (cell)
					print(win, " ", color);
			}
			print(win, ")", color);			
			break;
	}
}

struct Cell * builtinEdit(struct Cell ** scope, struct Cell * cell)
{	
	struct Cell * sym = car(cell);
	struct Cell * value = evalAtom(scope, sym);
	
	switch (value->type)
	{
		case CT_LAMBDA:
			print(&InputWindow, "(DEFUN ", 15);
			print(&InputWindow, (char *)sym + 1, 15);
			print(&InputWindow, " ", 15);
			while (value)
			{
				printAtom(&InputWindow, value->u.car, 15);
				value = value->u.cdr;
				if (value)
					print(&InputWindow, " ", 15);
			}
			print(&InputWindow, ")", 15);
			break;
		case CT_CONS:
		case CT_NUMBER:
		case CT_SYMBOL:
		case CT_QUOTE:
			print(&InputWindow, "(SETQ ", 15);
			print(&InputWindow, (char *)sym + 1, 15);
			print(&InputWindow, " '", 15);
			printAtom(&InputWindow, value, 15);
			print(&InputWindow, ")", 15);
			break;
		default:
			break;
	}
	
	return sym;
}

void addBuiltin(const char * symbol, Builtin builtin)
{
	struct Cell * bcell = allocCell(CT_BUILTIN);
	bcell->u.builtin = builtin;
	GlobalScope = cons(cons(findSymbol(symbol), bcell), GlobalScope);
}

void initBuiltins(void)
{
	TrueCell = findSymbol("T");
	
	GlobalScope = cons(cons(findSymbol("NIL"), nullptr), nullptr);

	addBuiltin("+", builtinAdd);
	addBuiltin("*", builtinMul);
	addBuiltin("-", builtinSub);
	addBuiltin("/", builtinDiv);

	addBuiltin("=", builtinEqual);
	addBuiltin("/=", builtinNotEqual);
	addBuiltin("<", builtinLess);
	addBuiltin(">", builtinGreater);
	addBuiltin("<=", builtinLessEqual);
	addBuiltin(">=", builtinGreaterEqual);

	addBuiltin("CONS", builtinCons);
	addBuiltin("CAR", builtinCar);
	addBuiltin("CDR", builtinCdr);
	addBuiltin("SETQ", builtinSetq);
	addBuiltin("LAMBDA", builtinLambda);
	addBuiltin("IF", builtinIf);
	addBuiltin("LET", builtinLet);
	addBuiltin("DEFUN", builtinDefun);

	addBuiltin(":LIST", builtinList);
	addBuiltin(":SAVE", builtinSave);
	addBuiltin(":LOAD", builtinLoad);	
	addBuiltin(":EDIT", builtinEdit);	
	addBuiltin(":RESET", builtinReset);	
}

void updateStatus(void)
{
	char	num[10];
	utoa(NumFreeCells, num, 10);
	strcat(num, "     ");
	cwin_putat_chars(&StatusWindow, 9, 0, num, 4, 14);	
}

int main(void)
{
	*(char *)0x01 = 0x36;
	
	cwin_init(&OutputWindow, (char *)0x0400, 0, 0, 40, 20);
	cwin_clear(&OutputWindow);

	cwin_init(&InputWindow, (char *)0x0400, 0, 21, 40, 4);
	cwin_clear(&InputWindow);

	cwin_init(&StatusWindow, (char *)0x0400, 0, 20, 40, 1);
	cwin_fill_rect(&StatusWindow, 0, 0, 40, 1, '-', 14);
	cwin_putat_string(&StatusWindow, 3, 0, "CELLS:", 14);

	initCells();
	initBuiltins();

	setjmp(MainLoop);
	
	for(;;)
	{
		garbageCollect();
		updateStatus();

		cwin_edit(&InputWindow);
		cwin_read_string(&InputWindow, InputBuffer);

		cwin_clear(&InputWindow);
		cwin_cursor_move(&InputWindow, 0, 0);

		InputPos = 0;
		struct Cell * cell = parseAtom(false);
		printAtom(&OutputWindow, cell, 1);
		printLn(&OutputWindow);

		cell = evalAtom(&GlobalScope, cell);
		printAtom(&OutputWindow, cell, 7);		
		printLn(&OutputWindow);		
	}

	return 0;
}


