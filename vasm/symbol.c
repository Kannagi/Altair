/* symbol.c - manage all kinds of symbols */
/* (c) in 2014 by Volker Barthelmann and Frank Wille */

#include "vasm.h"


symbol *first_symbol=NULL;
static symbol *saved_symbol=NULL;

static char *last_global_label=emptystr;

#ifndef SYMHTABSIZE
#define SYMHTABSIZE 0x10000
#endif
static hashtable *symhash;
static hashtable *regsymhash;


static void print_type(FILE *f,symbol *p)
{
  static const char *typename[] = {"???","obj","func","sect","file"};

  if (p == NULL)
    ierror(0);
  fprintf(f,"type=%s ",typename[TYPE(p)]);
}


void print_symbol(FILE *f,symbol *p)
{
  if (p==NULL)
    ierror(0);	/* this is usually an error in a cpu-backend, don't crash! */

  fprintf(f,"%s ",p->name);

  if (p->type==LABSYM)
    fprintf(f,"LAB (0x%llx) ",UNS_TADDR(p->pc));
  if (p->type==IMPORT)
    fprintf(f,"IMP ");
  if (p->type==EXPRESSION){
    fprintf(f,"EXPR(");
    print_expr(f,p->expr);
    fprintf(f,") ");
  }
  if (p->flags&VASMINTERN)
    fprintf(f,"INTERNAL ");
  if (p->flags&EXPORT)
    fprintf(f,"EXPORT ");
  if (p->flags&COMMON)
    fprintf(f,"COMMON ");
  if (p->flags&WEAK)
    fprintf(f,"WEAK ");
  if (TYPE(p))
    print_type(f,p);
  if (p->size){
    fprintf(f,"size=");
    print_expr(f,p->size);
    fprintf(f," ");
  }
  if (p->align)
    fprintf(f,"align=%lu ",(unsigned long)p->align);
  if (p->sec)
    fprintf(f,"sec=%s ",p->sec->name);
}


void add_symbol(symbol *p)
{
  hashdata data;

  p->next = first_symbol;
  first_symbol = p;
  data.ptr = p;
  add_hashentry(symhash,p->name,data);
}


symbol *find_symbol(char *name)
{
  hashdata data;
  if (!find_name(symhash,name,&data))
    return 0;
  return data.ptr;
}


void refer_symbol(symbol *sym,char *refname)
/* refer to an existing symbol with an additional name */
{
  hashdata data;
  data.ptr=sym;
  add_hashentry(symhash,refname,data);
}


void save_symbols(void)
/* remember current list of symbols to be restored later */
{
  saved_symbol = first_symbol;
}


void restore_symbols(void)
/* restore to a previously saved symbol list */
{
  symbol *firstprot=NULL, *lastprot=NULL;
  symbol *symp;

  if (saved_symbol) {
    while (first_symbol != saved_symbol) {
      symp = first_symbol;
      first_symbol = symp->next;
      if (symp->flags & PROTECTED) {
        /* keep this symbol */
        symp->next = firstprot;
        firstprot = symp;
        if (!lastprot)
          lastprot = symp;
      }
      else {
        rem_hashentry(symhash,symp->name,nocase);
        /* myfree(symp->name);  could be dangerous? */
        myfree(symp);
      }
    }
    if (firstprot) {
      /* add protected symbols to the list again */
      lastprot->next = first_symbol;
      first_symbol = firstprot;
    }
    saved_symbol = NULL;
  }
}


char *make_local_label(char *glob,int glen,char *loc,int llen)
/* construct a local label of the form:
   " " + global_label_name + " " + local_label_name */
{
  char *name,*p;

  if (glen == 0) {
    /* use the last defined global label */
    glob = last_global_label;
    glen = strlen(last_global_label);
  }
  p = name = mymalloc(llen+glen+3);
  *p++ = ' ';
  if (glen) {
    memcpy(p,glob,glen);
    p += glen;
  }
  *p++ = ' ';
  memcpy(p,loc,llen);
  *(p + llen) = '\0';
  return name;
}


symbol *new_abs(char *name,expr *tree)
{
  symbol *new = find_symbol(name);
  int add;

  if (new) {
    if (new->type!=IMPORT && new->type!=EXPRESSION)
      general_error(5,name);
    add=0;
  }
  else {
    new = mymalloc(sizeof(*new));
    new->name = mystrdup(name);
    add = 1;
  }

  new->type = EXPRESSION;
  new->sec = 0;
  new->expr = tree;

  if (add) {
    add_symbol(new);
    new->flags = 0;
    new->size = 0;
    new->align = 0;
  }
  return new;
}


symbol *new_import(char *name)
{
  symbol *new = find_symbol(name);

  if (new)
    return new;

  new = mymalloc(sizeof(*new));
  new->type = IMPORT;
  new->flags = 0;
  new->name = mystrdup(name);
  new->sec = 0;
  new->pc = 0;
  new->size = 0;
  new->align = 0;
  add_symbol(new);
  return new;
}


symbol *new_labsym(section *sec,char *name)
{
  symbol *new;
  int add;

  if (!sec) {
    sec = default_section();
    if (!sec) {
      general_error(3);
      return new_import(name);
    }
  }

  sec->flags |= HAS_SYMBOLS;

  if (sec->flags&LABELS_ARE_LOCAL)
    name = make_local_label(sec->name,strlen(sec->name),name,strlen(name));

  if (new = find_symbol(name)) {
    if (new->type!=IMPORT) {
      symbol *old = new;

      new = mymalloc(sizeof(*new));
      *new = *old;
      general_error(5,name);
    }
    add = 0;
  }
  else {
    new = mymalloc(sizeof(*new));
    if (sec->flags&LABELS_ARE_LOCAL)
      new->name = name;
    else
      new->name = mystrdup(name);
    add = 1;
  }

  new->type = LABSYM;
  new->sec = sec;
  new->pc = sec->pc;

  if (add) {
    add_symbol(new);
    new->flags = 0;
    new->size = 0;
    new->align = 0;
  }
  if (*name != ' ')
    last_global_label = new->name;
  return new;
}


symbol *new_tmplabel(section *sec)
{
  static unsigned long tmplabcnt = 0;
  char tmpnam[16];

  sprintf(tmpnam," *tmp%09lu*",tmplabcnt++);
  return new_labsym(sec,tmpnam);
}


symbol *internal_abs(char *name)
{
  symbol *new = find_symbol(name);

  if (new) {
    if (new->type!=EXPRESSION || (new->flags&(EXPORT|COMMON|WEAK)))
      general_error(37,name);  /* internal symbol redefined by user */
  }
  else {
    new = new_abs(name,number_expr(0));
    new->flags |= VASMINTERN;
  }
  return new;
}


expr *set_internal_abs(char *name,taddr newval)
{
  symbol *sym = internal_abs(name);
  expr *oldexpr = sym->expr;
  taddr oldval;

  if (oldexpr == NULL)
    ierror(0);
  eval_expr(oldexpr,&oldval,NULL,0);
  if (newval != oldval)
    sym->expr = number_expr(newval);
  return oldexpr;
}


#ifdef HAVE_REGSYMS
void add_regsym(regsym *rsym)
{
  hashdata data;

  data.ptr = rsym;
  add_hashentry(regsymhash,rsym->reg_name,data);
}


regsym *find_regsym(char *name,int len)
{
  hashdata data;

  if (find_namelen(regsymhash,name,len,&data))
    return data.ptr;
  return NULL;
}


regsym *find_regsym_nc(char *name,int len)
{
  hashdata data;

  if (find_namelen_nc(regsymhash,name,len,&data))
    return data.ptr;
  return NULL;
}


regsym *new_regsym(int no_case,char *name,int type,
                   unsigned int flags,unsigned int num)
{
  int len = strlen(name);
  regsym *rsym;

  /* check if register symbol already exists */
  if ((no_case!=0 && (rsym = find_regsym_nc(name,len))!=NULL) ||
      (no_case==0 && (rsym = find_regsym(name,len))!=NULL)) {
    general_error(58,name);  /* register symbol redefined */
    return rsym;
  }

  rsym = mymalloc(sizeof(regsym));
  rsym->reg_name = mystrdup(name);
  rsym->reg_type = type;
  rsym->reg_flags = flags;
  rsym->reg_num = num;
  add_regsym(rsym);

  return rsym;
}
#endif /* HAVE_REGSYMS */


int init_symbol(void)
{
  symhash = new_hashtable(SYMHTABSIZE);
#ifdef HAVE_REGSYMS
  regsymhash = new_hashtable(REGSYMHTSIZE);
#endif
  return 1;
}
