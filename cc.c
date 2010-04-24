#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "out.h"
#include "tok.h"

#define MAXLOCALS	(1 << 10)
#define print(s)	write(2, (s), strlen(s));

static struct local {
	char name[NAMELEN];
	long addr;
} locals[MAXLOCALS];
static int nlocals;

static void local_add(char *name, long addr)
{
	strcpy(locals[nlocals].name, tok_id());
	locals[nlocals].addr = addr;
	nlocals++;
}

static void die(char *s)
{
	print(s);
	exit(1);
}

static int tok_jmp(int tok)
{
	if (tok_see() != tok)
		return 1;
	tok_get();
	return 0;
}

static void tok_expect(int tok)
{
	if (tok_get() != tok)
		die("syntax error\n");
}

static void readexpr(void);

static int readtype(void)
{
	int found = 0;
	while (!found) {
		switch (tok_see()) {
		case TOK_VOID:
		case TOK_INT:
		case TOK_CHAR:
			found = 1;
			tok_get();
			break;
		case TOK_SHORT:
		case TOK_LONG:
		case TOK_UNSIGNED:
			found = 1;
			tok_get();
			break;
		case TOK_ENUM:
		case TOK_STRUCT:
			found = 1;
			tok_get();
			tok_expect(TOK_NAME);
			break;
		case TOK_EOF:
		default:
			return 1;
		}
	}
	while (!tok_jmp('*'))
		;
	return 0;
}

static void readprimary(void)
{
	int i;
	if (!tok_jmp(TOK_NUM)) {
		o_num(atoi(tok_id()));
		return;
	}
	if (!tok_jmp(TOK_NAME)) {
		for (i = 0; i < nlocals; i++) {
			if (!strcmp(locals[i].name, tok_id())) {
				o_local(locals[i].addr);
				return;
			}
		}
		o_symaddr(tok_id());
		return;
	}
	if (!tok_jmp('(')) {
		readexpr();
		tok_expect(')');
		return;
	}
}

static void readpost(void)
{
	readprimary();
	if (!tok_jmp('[')) {
		readexpr();
		tok_expect(']');
		return;
	}
	if (!tok_jmp('(')) {
		int argc = 0;
		if (tok_see() != ')') {
			readexpr();
			argc++;
		}
		while (!tok_jmp(',')) {
			readexpr();
			argc++;
		}
		tok_expect(')');
		o_call(argc);
	}
}

static void readadd(void)
{
	readpost();
	if (!tok_jmp('+')) {
		readpost();
		o_add();
	}
	if (!tok_jmp('-')) {
		readpost();
		o_sub();
	}
}

static void readexpr(void)
{
	readadd();
	if (!tok_jmp('=')) {
		readexpr();
		o_assign();
	}
}

static void readstmt(void)
{
	if (!tok_jmp('{')) {
		while (tok_jmp('}'))
			readstmt();
		return;
	}
	if (!readtype()) {
		tok_expect(TOK_NAME);
		local_add(tok_id(), o_mklocal());
		/* initializer */
		if (!tok_jmp('=')) {
			o_local(locals[nlocals - 1].addr);
			readexpr();
			o_assign();
			tok_expect(';');
		}
		return;
	}
	if (!tok_jmp(TOK_IF)) {
		long l1, l2;
		tok_expect('(');
		readexpr();
		tok_expect(')');
		l1 = o_stubjz();
		readstmt();
		if (!tok_jmp(TOK_ELSE)) {
			l2 = o_stubjz();
			o_filljz(l1);
				readstmt();
			o_filljz(l2);
		} else {
			o_filljz(l1);
		}
		return;
	}
	if (!tok_jmp(TOK_WHILE)) {
		long l1, l2;
		l1 = o_mklabel();
		tok_expect('(');
		readexpr();
		tok_expect(')');
		l2 = o_stubjz();
		readstmt();
		o_jz(l1);
		o_filljz(l2);
		return;
	}
	if (!tok_jmp(TOK_RETURN)) {
		int ret = tok_see() != ';';
		if (ret)
			readexpr();
		tok_expect(';');
		o_ret(ret);
		return;
	}
	readexpr();
	tok_expect(';');
}

#define MAXARGS			(1 << 5)

static void readdecl(void)
{
	char name[NAMELEN];
	readtype();
	tok_expect(TOK_NAME);
	strcpy(name, tok_id());
	if (!tok_jmp(';'))
		return;
	if (!tok_jmp('(')) {
		/* read args */
		char args[MAXARGS][NAMELEN];
		int nargs = 0;
		int i;
		while (tok_see() != ')') {
			readtype();
			if (!tok_jmp(TOK_NAME))
				strcpy(args[nargs++], tok_id());
			if (tok_jmp(','))
				break;
		}
		tok_expect(')');
		if (!tok_jmp(';'))
			return;
		o_func_beg(name);
		for (i = 0; i < nargs; i++)
			local_add(args[i], o_arg(i));
		readstmt();
		o_func_end();
		return;
	}
	die("syntax error\n");
}

static void parse(void)
{
	while (tok_see() != TOK_EOF)
		readdecl();
}

int main(int argc, char *argv[])
{
	char obj[128];
	char *src = argv[1];
	int ifd, ofd;
	ifd = open(src, O_RDONLY);
	tok_init(ifd);
	close(ifd);
	out_init();
	parse();

	strcpy(obj, src);
	obj[strlen(obj) - 1] = 'o';
	ofd = open(obj, O_WRONLY | O_TRUNC | O_CREAT, 0600);
	out_write(ofd);
	close(ofd);
	return 0;
}
