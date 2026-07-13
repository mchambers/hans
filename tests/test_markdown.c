/*
 * Native (host-side) regression tests for the Markdown line parser that
 * drives Hans's in-situ editor styling and the preview renderer.
 *
 * markdown.c is pure logic; we compile it directly into this test by
 * pre-defining HANS_H so the real hans.h (full of Mac Toolbox includes)
 * is skipped, and supplying the few types the parser needs.
 *
 *   cc -o test_markdown test_markdown.c && ./test_markdown
 */
#include <stdio.h>
#include <string.h>

/* ---- stub of the bits of hans.h that markdown.c uses ---- */
#define HANS_H                      /* make the real hans.h a no-op */
typedef unsigned char Boolean;
#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 202311L
enum { false = 0, true = 1 };   /* C23 makes these keywords */
#endif

enum {
    kMDBold     = 0x0001,
    kMDItalic   = 0x0002,
    kMDCode     = 0x0004,
    kMDMarker   = 0x0008,
    kMDQuote    = 0x0010,
    kMDLink     = 0x0020,
    kMDRule     = 0x0040
};

typedef struct {
    long start, end;
    unsigned short flags;
} MDSpan;

#define kMaxSpans 64

short MDParseLine(const char* p, long len, MDSpan* spans, unsigned char* headLevel);

#include "../src/markdown.c"

/* ---- tiny test harness ---- */
static int gFailures = 0;
static int gChecks = 0;

static MDSpan gSpans[kMaxSpans];
static short gN;
static unsigned char gHead;

static void parse(const char* line)
{
    gN = MDParseLine(line, (long)strlen(line), gSpans, &gHead);
}

static void check(const char* what, int cond)
{
    gChecks++;
    if (!cond) {
        gFailures++;
        printf("FAIL: %s\n", what);
    }
}

/* find a span with exactly these bounds and flags */
static int has_span(long s, long e, unsigned short flags)
{
    short i;
    for (i = 0; i < gN; i++)
        if (gSpans[i].start == s && gSpans[i].end == e && gSpans[i].flags == flags)
            return 1;
    return 0;
}

int main(void)
{
    /* --- headings --- */
    parse("# Title");
    check("h1 level", gHead == 1);
    check("h1 marker span '# '", has_span(0, 2, kMDMarker));

    parse("### Sub");
    check("h3 level", gHead == 3);
    check("h3 marker span '### '", has_span(0, 4, kMDMarker));

    parse("####### seven");
    check("7 hashes is not a heading", gHead == 0);

    parse("#nospace");
    check("hash without space is not a heading", gHead == 0);

    parse("##");
    check("bare hashes at EOL are a heading", gHead == 2);

    /* --- bold / italic --- */
    parse("**bold** x");
    check("bold body", has_span(2, 6, kMDBold));
    check("bold open marker", has_span(0, 2, kMDMarker));
    check("bold close marker", has_span(6, 8, kMDMarker));

    parse("__bold__");
    check("underscore bold", has_span(2, 6, kMDBold));

    parse("*ital*");
    check("italic body", has_span(1, 5, kMDItalic));
    check("italic markers", has_span(0, 1, kMDMarker) && has_span(5, 6, kMDMarker));

    parse("_ital_");
    check("underscore italic", has_span(1, 5, kMDItalic));

    /* --- emphasis flanking rules --- */
    parse("2 * 3 * 4");
    check("stars with spaces around are not emphasis", gN == 0);

    parse("snake_case_name");
    check("intra-word underscores are not emphasis", gN == 0);

    parse("a __private__ name");
    check("boundary underscores still bold", has_span(4, 11, kMDBold));

    parse("mid__word__stars");
    check("mid-word double underscore is not bold", gN == 0);

    parse("*a *b*");
    check("space-preceded star cannot close", has_span(1, 5, kMDItalic));

    parse("**unclosed");
    check("unclosed bold produces no bold span", gN == 0);

    parse("**");
    check("empty strong markers alone: no spans", gN == 0);

    parse("a **b** and *c*");
    check("mixed: bold at 4..5", has_span(4, 5, kMDBold));
    check("mixed: italic at 13..14", has_span(13, 14, kMDItalic));

    /* --- inline code --- */
    parse("see `code` here");
    check("code body", has_span(5, 9, kMDCode));
    check("code markers", has_span(4, 5, kMDMarker) && has_span(9, 10, kMDMarker));

    parse("`unclosed");
    check("unclosed backtick: no code span", gN == 0);

    /* --- links --- */
    parse("[text](url)");
    check("link text span", has_span(1, 5, kMDLink));
    check("link open marker", has_span(0, 1, kMDMarker));
    check("link url+parens marker", has_span(5, 11, kMDMarker));

    parse("[text](unclosed");
    check("unclosed link: no link span", gN == 0);

    /* --- blockquote --- */
    parse("> quoted");
    check("quote marker '> '", has_span(0, 2, kMDMarker | kMDQuote));

    parse(">tight");
    check("quote marker without space", has_span(0, 1, kMDMarker | kMDQuote));

    parse("> # Quoted heading");
    check("heading inside quote", gHead == 1);
    check("quote marker kept", has_span(0, 2, kMDMarker | kMDQuote));

    /* --- lists --- */
    parse("- item");
    check("dash bullet marker", has_span(0, 2, kMDMarker));

    parse("* item");
    check("star bullet marker", has_span(0, 2, kMDMarker));

    parse("+ item");
    check("plus bullet marker", has_span(0, 2, kMDMarker));

    parse("- **bold item**");
    check("bullet + inline bold", has_span(0, 2, kMDMarker) && has_span(4, 13, kMDBold));

    parse("12. numbered");
    check("ordered marker '12. '", has_span(0, 4, kMDMarker));

    parse("3) paren style");
    check("ordered marker '3) '", has_span(0, 3, kMDMarker));

    parse("-not a list");
    check("dash without space is not a list", !has_span(0, 2, kMDMarker));

    /* --- horizontal rules --- */
    parse("---");
    check("dash rule", gN == 1 && gSpans[0].flags == (kMDRule | kMDMarker));

    parse("* * *");
    check("spaced star rule", gN == 1 && (gSpans[0].flags & kMDRule));

    parse("___");
    check("underscore rule", gN == 1 && (gSpans[0].flags & kMDRule));

    parse("--");
    check("two dashes are not a rule", gN == 0 || !(gSpans[0].flags & kMDRule));

    parse("- - x");
    check("dashes with text are not a rule",
          gN == 0 || !(gSpans[0].flags & kMDRule));

    /* --- plain / empty --- */
    parse("just plain prose.");
    check("plain prose: no spans, no heading", gN == 0 && gHead == 0);

    gHead = 99;
    gN = MDParseLine("", 0, gSpans, &gHead);
    check("empty line: no spans, headLevel reset", gN == 0 && gHead == 0);

    /* --- span-count cap --- */
    {
        char line[512];
        int i;
        line[0] = 0;
        for (i = 0; i < 60; i++) strcat(line, "*a* ");
        parse(line);
        check("many spans stay within kMaxSpans", gN <= kMaxSpans);
    }

    printf("%d checks, %d failure(s)\n", gChecks, gFailures);
    return gFailures ? 1 : 0;
}
