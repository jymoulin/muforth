/*
 * This file is part of muFORTH: http://muforth.nimblemachines.com/
 *
 * Copyright (c) 2002-2012 David Frech. All rights reserved, and all wrongs
 * reversed. (See the file COPYRIGHT for details.)
 */

/* Nuts and bolts of threaded-code compiler & execution engine */

#include "muforth.h"

void mu_execute() { EXECUTE; }

/*
 * This is where all the magic happens. Any time we have the address of a
 * word to execute (an execution token, or XTK) - either because we looked
 * it up in the dictionary, or because we fetched it out of a variable - we
 * call execute_xtk. It is most commonly invoked by EXECUTE, which POPs an
 * xtk off the D stack, and calls execute_xtk.
 *
 * We need magic here because of the hybrid nature of the system. It is
 * *not* a truly indirect-threaded Forth, where you set up IP and go, and
 * never look back. We're executing C code, then Forth, then C... The Forth
 * inner interpreter never runs "free"; in fact, we run it in this routine!
 *
 * How do we know if the XTK refers to a Forth word or to a C routine? We
 * don't...until we run it. If it's Forth, the first thing it will do is
 * NEST - push the caller's IP - and then it will set IP to point to its
 * own code. In fact, that's *all* Forth words do when you execute them!
 *
 * So we save RP, then call the word. Then we loop *while* the current RP
 * is *strictly* less than the saved. If we didn't call a Forth word, this
 * will not be true, and we'll exit. But the word we called - the C routine
 * - will have run its course and done all its work.
 *
 * If it *was* a Forth word, it NESTED, and RP < rp_saved. So now we run
 * NEXT - a "constrained" threaded interpreter - until RP >= rp_saved, which
 * will happen precisely when the called (Forth) word executes UNNEST
 * (EXIT).
 *
 * That's all there is to it!
 *
 * Thanks to Michael Pruemm for the idea of comparing RP to rp_saved as a
 * way to see when we're "done".
 */
void execute_xtk(xtk x)
{
    val *rp_saved;

    rp_saved = RP;

    CALL(x);
    while (RP < rp_saved)
        NEXT;
}

/* The most important "word" of all: */
static void mu_do_colon()
{
    NEST;              /* entering a new word; push IP */
    IP = (xtk *)&W[1]; /* new IP is address of parameter field */
}

/* The basis of create/does>. */
static void mu_do_does()
{
    NEST;              /* entering a new word; push IP */
    IP = (xtk *)W[1];  /* new IP is stored in the parameter field */
    PUSH_ADDR(&W[2]);  /* push the address of the word's body */
}

void mu_set_colon_code() { *ph++ = (addr)&mu_do_colon; }
void mu_set_does_code()  { *ph++ = (addr)&mu_do_does; }

/* Normal exit */
void mu_exit()      { UNNEST; }

/* Push an inline literal */
void mu_lit_()  { PUSH(*(cell *)IP++); }


/*
 * These are the control structure runtime workhorses. They are static
 * because we don't export them directly to muforth; instead we export
 * words that *compile* them.
 */
void mu_branch_()            { BRANCH; }
void mu_equal_zero_branch_() { if (TOP == 0) BRANCH; else IP++; }
void mu_zero_branch_()       { mu_equal_zero_branch_(); DROP(1); }

/*
 * for, ?for, next
 *
 * for simply compiles "push"; it pairs with next.
 *
 * ?for compiles (?for); it has to be matched with "next" and "then".  At
 * run-time (ie when (?for) executes), if TOP is zero we skip the entire
 * loop by jumping to "then"; otherwise we could be looping for a long time
 * - 2^(#bits in CELL)!
 */

void mu_qfor_()
{
    if (TOP == 0) { BRANCH; DROP(1); }   /* take branch, pop stack */
    else          { IP++; RPUSH(POP); }  /* skip branch, push count onto R */
}

void mu_next_()
{
    cell rtop = RP[0];                  /* counter on top of R stack */

    if (--rtop == 0)
        { IP++; RP++; }                 /* skip branch, pop counter */
    else
        { RP[0] = rtop; BRANCH; }       /* update index, branch back */
}

/*
 * I decided it can't hurt to support do/loop/+loop. Lots of people
 * understand it, and folks might have "legacy" code that they don't want
 * to rewrite into for/next.
 *
 * do/loop isn't a bad construct. I think really the only reason that Chuck
 * deprecated it - starting in cmForth - was that it didn't map neatly to
 * his chip! The runtime words (do) (loop) and (+loop) need two - or three!
 * - items on the stack. His original stack computer could only see the top
 * slot of the return stack. Hence, for/next, and simple count-down loops.
 *
 * We are going to implement a "zero-crossing" version. For some reason
 * many Forths choose to offset the start and limit values so that the loop
 * exits when the index "crosses" from the most negative number (8000...
 * hex) to the most positive (7fff... hex) - ie, when arithmetic overflow
 * occurs. I can't remember why this seemed like a good idea. I'm going to
 * implement a version that offsets the index so that the limit is always
 * 0. We'll see how that works.
 */

void mu_do_()   /* (do)  ( limit start) */
{
    RPUSH((addr)*IP++); /* push following branch address for (leave) */
    RPUSH(ST1);         /* limit */
    RPUSH(TOP - ST1);   /* index = start - limit */
    DROP(2);
}

void mu_loop_()
{
    val rtop = RP[0];

    rtop++;                             /* increment index */
    if (rtop == 0)
        { IP++; RP += 3; }              /* skip branch, pop R stack */
    else
        { RP[0] = rtop; BRANCH; }       /* update index, branch back */
}

void mu_plus_loop_()    /* (+loop)  ( incr) */
{
    val rtop = RP[0];
    val prev = rtop;

    rtop += TOP;                /* increment index */
    if ((rtop ^ prev) < 0)      /* current & prev index have opposite signs */
        { IP++; RP += 3; }              /* skip branch, pop R stack */
    else
        { RP[0] = rtop; BRANCH; }       /* update index, branch back */
    DROP(1);
}

/* leave the do loop early */
void mu_leave()
{
    IP = (xtk *)RP[2];     /* jump to address saved on R stack */
    RP += 3;                    /* pop "do" context */
}

/* conditionally leave */
void mu_qleave()
{
    if (POP) mu_leave();
}

/*
 * Calculate current loop indices for current (i), enclosing (j), and
 * third-level (k) do-loops
 */

void mu_i() { PUSH(RP[0] + RP[1]); }
void mu_j() { PUSH(RP[3] + RP[4]); }
void mu_k() { PUSH(RP[6] + RP[7]); }

/* R stack functions */
void mu_to_r()   { RPUSH(POP); }
void mu_r_from() { PUSH(RPOP); }
void mu_rfetch() { PUSH(RP[0]); }

#ifdef CHUMOO
/* These are Chuck's newfangled names for >r and r> */
void mu_push()   { mu_to_r(); }
void mu_pop()    { mu_r_from(); }
#endif

/* shunt is shorthand for r> drop */
void mu_shunt()  { RP++; }


#if 0
/*
 * Using these two words - r@+ and ?^ - it would be easy to implement the
 * branch runtime words in Forth rather than in C.
 */

/* Whether positive or negative logic can easily be changed; whether it
 * consumes TOP can be changed as well, depending on what seems to work
 * best. However, unnest-if-false should probably be called -?^ */

void mu_qexit()  { if (TOP) UNNEST; DROP(1); }

/* So we can easily define compile and (lit) in Forth. Fetches and pushes
 * to D stack top value on R stack; increments that value by sizeof(cell).
 * That yield an IP pointer on D stack; to get the _value_ we need to
 * fetch. This fetch could be built into the word, and maybe renamed to
 * r@+@ or something equally ungainly. Or maybe it could get a more
 * mnemonic (what it means), rather than functional (what it does) name.
 */
static void mu_rfetch_plus()  { PUSH(RP[0]); RP[0]++; }
#endif

