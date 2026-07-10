/*
 * Hans — a small, forgiving Markdown line parser.
 *
 * It works one logical line at a time (no trailing CR) and produces a list
 * of spans describing which byte ranges should be styled and how. Offsets
 * are relative to the start of the line. The editor turns spans into MLTE
 * type-attribute runs; the preview renderer uses the same spans to strip
 * syntax and apply styles.
 *
 * Supported: ATX headings, bold (double star or double underscore),
 * italic (single star or underscore), inline code (backticks),
 * blockquotes, unordered and ordered lists, horizontal rules, and
 * links written as [text](url).
 */
#include "hans.h"

static Boolean IsSpace(char c) { return c == ' ' || c == '\t'; }

static short AddSpan(MDSpan* spans, short n, long start, long end,
                     unsigned short flags)
{
    if (n >= kMaxSpans) return n;
    if (end < start) return n;
    spans[n].start = start;
    spans[n].end = end;
    spans[n].flags = flags;
    return n + 1;
}

/* Is the whole line (after optional leading spaces) a horizontal rule? */
static Boolean IsHorizontalRule(const char* p, long len)
{
    long i = 0;
    char ch;
    long count = 0;
    while (i < len && IsSpace(p[i])) i++;
    if (i >= len) return false;
    ch = p[i];
    if (ch != '-' && ch != '*' && ch != '_') return false;
    for (; i < len; i++) {
        if (p[i] == ch) count++;
        else if (!IsSpace(p[i])) return false;
    }
    return count >= 3;
}

/* Parse inline markers (bold/italic/code/link) within [from,len). */
static short ParseInline(const char* p, long from, long len,
                         MDSpan* spans, short n)
{
    long i = from;
    while (i < len) {
        char c = p[i];

        if (c == '`') {
            long j = i + 1;
            while (j < len && p[j] != '`') j++;
            if (j < len) {
                n = AddSpan(spans, n, i, i + 1, kMDMarker);
                n = AddSpan(spans, n, i + 1, j, kMDCode);
                n = AddSpan(spans, n, j, j + 1, kMDMarker);
                i = j + 1;
                continue;
            }
        }
        else if ((c == '*' || c == '_') && i + 1 < len && p[i + 1] == c) {
            /* strong: **text** or __text__ */
            long j = i + 2;
            while (j + 1 < len && !(p[j] == c && p[j + 1] == c)) j++;
            if (j + 1 < len && p[j] == c && p[j + 1] == c && j > i + 2) {
                n = AddSpan(spans, n, i, i + 2, kMDMarker);
                n = AddSpan(spans, n, i + 2, j, kMDBold);
                n = AddSpan(spans, n, j, j + 2, kMDMarker);
                i = j + 2;
                continue;
            }
        }
        else if (c == '*' || c == '_') {
            /* emphasis: *text* or _text_ */
            long j = i + 1;
            while (j < len && p[j] != c) j++;
            if (j < len && j > i + 1) {
                n = AddSpan(spans, n, i, i + 1, kMDMarker);
                n = AddSpan(spans, n, i + 1, j, kMDItalic);
                n = AddSpan(spans, n, j, j + 1, kMDMarker);
                i = j + 1;
                continue;
            }
        }
        else if (c == '[') {
            /* link: [text](url) */
            long close = i + 1;
            while (close < len && p[close] != ']') close++;
            if (close < len && close + 1 < len && p[close + 1] == '(') {
                long paren = close + 2;
                while (paren < len && p[paren] != ')') paren++;
                if (paren < len) {
                    n = AddSpan(spans, n, i, i + 1, kMDMarker);
                    n = AddSpan(spans, n, i + 1, close, kMDLink);
                    n = AddSpan(spans, n, close, paren + 1, kMDMarker);
                    i = paren + 1;
                    continue;
                }
            }
        }
        i++;
    }
    return n;
}

short MDParseLine(const char* p, long len, MDSpan* spans, unsigned char* headLevel)
{
    short n = 0;
    long i = 0;

    *headLevel = 0;
    if (len <= 0) return 0;

    if (IsHorizontalRule(p, len)) {
        AddSpan(spans, 0, 0, len, kMDRule | kMDMarker);
        return 1;
    }

    /* Leading blockquote marker: '>' possibly preceded by spaces */
    while (i < len && IsSpace(p[i])) i++;
    if (i < len && p[i] == '>') {
        long m = i + 1;
        n = AddSpan(spans, n, i, (m < len && p[m] == ' ') ? m + 1 : m,
                    kMDMarker | kMDQuote);
        i = (m < len && p[m] == ' ') ? m + 1 : m;
    }

    /* ATX heading */
    {
        long h = i, hc = 0;
        while (h < len && p[h] == '#') { hc++; h++; }
        if (hc >= 1 && hc <= 6 && (h >= len || IsSpace(p[h]))) {
            long content = h;
            while (content < len && IsSpace(p[content])) content++;
            *headLevel = (unsigned char)hc;
            n = AddSpan(spans, n, i, content, kMDMarker);
            n = ParseInline(p, content, len, spans, n);
            return n;
        }
    }

    /* Unordered list: -, *, + followed by a space */
    if (i + 1 < len && (p[i] == '-' || p[i] == '*' || p[i] == '+')
        && p[i + 1] == ' ') {
        n = AddSpan(spans, n, i, i + 2, kMDMarker);
        n = ParseInline(p, i + 2, len, spans, n);
        return n;
    }

    /* Ordered list: digits followed by '.' or ')' and a space */
    {
        long d = i;
        while (d < len && p[d] >= '0' && p[d] <= '9') d++;
        if (d > i && d + 1 < len && (p[d] == '.' || p[d] == ')')
            && p[d + 1] == ' ') {
            n = AddSpan(spans, n, i, d + 2, kMDMarker);
            n = ParseInline(p, d + 2, len, spans, n);
            return n;
        }
    }

    n = ParseInline(p, i, len, spans, n);
    return n;
}
