/*
 * Hans — resources.
 */
#include "Types.r"
#include "SysTypes.r"
#include "Menus.r"
#include "Dialogs.r"
#include "Processes.r"
#include "Finder.r"

/* ---------------- Menus ---------------- */

resource 'MBAR' (128) {
    { 128, 129, 130, 133, 131, 132 }
};

resource 'MENU' (128, "Apple") {
    128, textMenuProc, 0x7FFFFFFD, enabled, apple,
    {
        "About Hans\0xC9", noIcon, noKey, noMark, plain;
        "-", noIcon, noKey, noMark, plain;
    }
};

resource 'MENU' (129, "File") {
    129, textMenuProc, allEnabled, enabled, "File",
    {
        "New Note", noIcon, "N", noMark, plain;
        "New Folder", noIcon, noKey, noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "Open Library\0xC9", noIcon, noKey, noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "Save", noIcon, "S", noMark, plain;
        "Rename\0xC9", noIcon, noKey, noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "Preview", noIcon, "P", noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "Quit", noIcon, "Q", noMark, plain;
    }
};

resource 'MENU' (130, "Edit") {
    130, textMenuProc, allEnabled, enabled, "Edit",
    {
        "Undo", noIcon, "Z", noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "Cut", noIcon, "X", noMark, plain;
        "Copy", noIcon, "C", noMark, plain;
        "Paste", noIcon, "V", noMark, plain;
        "Clear", noIcon, noKey, noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "Select All", noIcon, "A", noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "Preferences\0xC9", noIcon, noKey, noMark, plain;
    }
};

resource 'MENU' (133, "View") {
    133, textMenuProc, allEnabled, enabled, "View",
    {
        "Hide Library", noIcon, "\\", noMark, plain;
    }
};

resource 'MENU' (131, "Format") {
    131, textMenuProc, allEnabled, enabled, "Format",
    {
        "Geneva", noIcon, noKey, noMark, plain;
        "New York", noIcon, noKey, noMark, plain;
        "Monaco", noIcon, noKey, noMark, plain;
        "Charcoal", noIcon, noKey, noMark, plain;
        "Chicago", noIcon, noKey, noMark, plain;
        "Palatino", noIcon, noKey, noMark, plain;
        "Times", noIcon, noKey, noMark, plain;
        "Courier", noIcon, noKey, noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "9 Point", noIcon, noKey, noMark, plain;
        "10 Point", noIcon, noKey, noMark, plain;
        "12 Point", noIcon, noKey, noMark, plain;
        "14 Point", noIcon, noKey, noMark, plain;
        "18 Point", noIcon, noKey, noMark, plain;
    }
};

resource 'MENU' (132, "Tools") {
    132, textMenuProc, allEnabled, enabled, "Tools",
    {
        "Check Style", noIcon, "K", noMark, plain;
        "Edit User Word List\0xC9", noIcon, noKey, noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "Writing Assistant\0xC9", noIcon, "L", noMark, plain;
    }
};

/* ---------------- Alerts ---------------- */

resource 'ALRT' (128, "About") {
    { 60, 80, 210, 460 },
    128,
    { OK, visible, sound1; OK, visible, sound1; OK, visible, sound1; OK, visible, sound1 },
    centerMainScreen
};

resource 'DITL' (128) {
    {
        { 116, 290, 136, 360 }, Button { enabled, "OK" };
        { 10, 20, 42, 360 }, StaticText { disabled, "Hans 1.0b1" };
        { 46, 20, 108, 360 }, StaticText { disabled,
            "A quiet place to write. Markdown editing, style checking, "
            "and a writing assistant \0xD1 for Mac OS 9." };
    }
};

resource 'ALRT' (129, "Error") {
    { 80, 100, 190, 480 },
    129,
    { OK, visible, sound1; OK, visible, sound1; OK, visible, sound1; OK, visible, sound1 },
    centerMainScreen
};

resource 'DITL' (129) {
    {
        { 76, 300, 96, 370 }, Button { enabled, "OK" };
        { 10, 20, 68, 370 }, StaticText { disabled, "^0 ^1" };
    }
};

resource 'ALRT' (132, "Confirm") {
    { 80, 100, 190, 480 },
    132,
    { OK, visible, sound1; OK, visible, sound1; OK, visible, sound1; OK, visible, sound1 },
    centerMainScreen
};

resource 'DITL' (132) {
    {
        { 76, 300, 96, 370 }, Button { enabled, "OK" };
        { 76, 210, 96, 290 }, Button { enabled, "Cancel" };
        { 10, 20, 68, 370 }, StaticText { disabled, "^0 ^1" };
    }
};

/* ---------------- Preferences dialog ---------------- */

resource 'DLOG' (130, "Preferences") {
    { 76, 90, 356, 522 },
    movableDBoxProc,
    visible,
    noGoAway,
    0,
    130,
    "Preferences",
    centerMainScreen
};

resource 'DITL' (130) {
    {
        { 246, 342, 266, 420 }, Button { enabled, "OK" };
        { 246, 252, 266, 330 }, Button { enabled, "Cancel" };
        { 13, 12, 29, 126 },   StaticText { disabled, "Proxy Address:" };
        { 13, 132, 29, 310 },  EditText { enabled, "" };
        { 41, 12, 57, 126 },   StaticText { disabled, "Proxy Port:" };
        { 41, 132, 57, 196 },  EditText { enabled, "" };
        { 69, 12, 85, 126 },   StaticText { disabled, "API Key:" };
        { 69, 132, 85, 420 },  EditText { enabled, "" };
        { 97, 12, 113, 126 },  StaticText { disabled, "Model:" };
        { 97, 132, 113, 310 }, EditText { enabled, "" };
        { 168, 12, 232, 420 }, StaticText { disabled,
            "Hans talks to the OpenAI API through the Hans Proxy running on a "
            "modern machine on your network. The API key may be left blank if "
            "the proxy is configured with one." };
        { 130, 12, 148, 420 }, CheckBox { enabled,
            "Enable the writing assistant (Max)" };
    }
};

/* ---------------- Name dialog (new note / new folder / rename) -------- */

resource 'DLOG' (131, "Name") {
    { 100, 120, 212, 436 },
    movableDBoxProc,
    visible,
    noGoAway,
    0,
    131,
    "Name",
    centerMainScreen
};

resource 'DITL' (131) {
    {
        { 78, 226, 98, 304 }, Button { enabled, "OK" };
        { 78, 136, 98, 214 }, Button { enabled, "Cancel" };
        { 10, 12, 26, 304 },  StaticText { disabled, "^0" };
        { 38, 12, 54, 304 },  EditText { enabled, "" };
    }
};

/* ---------------- Style-check word lists ----------------
   One 'TEXT' resource per category, one phrase per line. */

data 'TEXT' (300, "Cliches and Idioms") {
    "at the end of the day\n"
    "low-hanging fruit\n"
    "think outside the box\n"
    "move the needle\n"
    "in this day and age\n"
    "the fact of the matter\n"
    "when all is said and done\n"
    "par for the course\n"
    "begs the question\n"
    "back to the drawing board\n"
    "ballpark figure\n"
    "barking up the wrong tree\n"
    "best of both worlds\n"
    "bite the bullet\n"
    "the big picture\n"
    "hit the ground running\n"
    "it goes without saying\n"
    "last but not least\n"
    "leave no stone unturned\n"
    "level playing field\n"
    "light at the end of the tunnel\n"
    "needle in a haystack\n"
    "nip it in the bud\n"
    "no-brainer\n"
    "on the same page\n"
    "paradigm shift\n"
    "push the envelope\n"
    "raise the bar\n"
    "silver bullet\n"
    "the bottom line\n"
    "tip of the iceberg\n"
    "touch base\n"
    "up in the air\n"
    "win-win\n"
    "game changer\n"
    "perfect storm\n"
    "double-edged sword\n"
    "elephant in the room\n"
    "boils down to\n"
    "food for thought\n"
    "few and far between\n"
    "crystal clear\n"
    "easier said than done\n"
    "in the nick of time\n"
    "second nature\n"
    "set in stone\n"
    "time will tell\n"
    "trials and tribulations\n"
    "uphill battle\n"
    "vicious cycle\n"
    "sea change\n"
    "deep dive\n"
    "circle back\n"
    "synergy\n"
};

data 'TEXT' (301, "Fillers and Overused") {
    "very\n"
    "really\n"
    "actually\n"
    "basically\n"
    "literally\n"
    "quite\n"
    "rather\n"
    "simply\n"
    "totally\n"
    "definitely\n"
    "certainly\n"
    "virtually\n"
    "extremely\n"
    "incredibly\n"
    "absolutely\n"
    "essentially\n"
    "utilize\n"
    "leverage\n"
    "impactful\n"
    "amazing\n"
    "awesome\n"
    "interesting\n"
    "stuff\n"
    "things\n"
    "a lot\n"
    "kind of\n"
    "sort of\n"
    "in order to\n"
    "due to the fact that\n"
    "at this point in time\n"
    "for all intents and purposes\n"
    "needless to say\n"
    "it is important to note\n"
    "as a matter of fact\n"
    "in terms of\n"
    "with regard to\n"
    "obviously\n"
    "clearly\n"
    "of course\n"
    "arguably\n"
};

data 'TEXT' (302, "Redundancies") {
    "absolutely essential\n"
    "advance planning\n"
    "added bonus\n"
    "basic fundamentals\n"
    "close proximity\n"
    "completely eliminate\n"
    "end result\n"
    "exact same\n"
    "final outcome\n"
    "free gift\n"
    "future plans\n"
    "general consensus\n"
    "join together\n"
    "new innovation\n"
    "past history\n"
    "personal opinion\n"
    "revert back\n"
    "true fact\n"
    "unexpected surprise\n"
    "each and every\n"
    "first and foremost\n"
    "full and complete\n"
    "various different\n"
    "combined together\n"
    "repeat again\n"
    "still remains\n"
    "brief summary\n"
    "mutual cooperation\n"
    "plan ahead\n"
};

data 'TEXT' (303, "Hedges and Qualifiers") {
    "I think\n"
    "I believe\n"
    "I feel\n"
    "it seems\n"
    "seems to\n"
    "perhaps\n"
    "maybe\n"
    "somewhat\n"
    "a bit\n"
    "a little\n"
    "possibly\n"
    "apparently\n"
    "presumably\n"
    "in my opinion\n"
    "to some extent\n"
    "more or less\n"
    "relatively\n"
    "fairly\n"
    "in some ways\n"
    "tend to\n"
    "could be\n"
    "might be\n"
    "generally speaking\n"
};

/* ---------------- Finder integration ---------------- */

type 'HANS' as 'STR ';
resource 'HANS' (0) { "Hans 1.0b1 \0xD1 a markdown editor for Mac OS 9" };

resource 'FREF' (128) { 'APPL', 0, "" };
resource 'FREF' (129) { 'TEXT', 1, "" };

/* 32x32 black & white icons + masks: a page with a folded corner; the
   application carries a bold H, documents carry text lines. */

data 'ICN#' (128, "Hans application") {
    $"00000000 00000000 07FFE000 04000800"
    $"04000400 04000200 04000100 04000080"
    $"04000040 04000FE0 04000020 04381C20"
    $"04381C20 04381C20 04381C20 04381C20"
    $"04381C20 04381C20 043FFC20 043FFC20"
    $"04381C20 04381C20 04381C20 04381C20"
    $"04381C20 04381C20 04381C20 04000020"
    $"04000020 07FFFFE0 00000000 00000000"
    $"00000000 00000000 07FFF000 07FFF800"
    $"07FFFC00 07FFFE00 07FFFF00 07FFFF80"
    $"07FFFFC0 07FFFFE0 07FFFFE0 07FFFFE0"
    $"07FFFFE0 07FFFFE0 07FFFFE0 07FFFFE0"
    $"07FFFFE0 07FFFFE0 07FFFFE0 07FFFFE0"
    $"07FFFFE0 07FFFFE0 07FFFFE0 07FFFFE0"
    $"07FFFFE0 07FFFFE0 07FFFFE0 07FFFFE0"
    $"07FFFFE0 07FFFFE0 00000000 00000000"
};

data 'ICN#' (129, "Hans note") {
    $"00000000 00000000 07FFE000 04000800"
    $"04000400 04000200 04000100 04000080"
    $"04000040 04000FE0 04000020 04FF8020"
    $"04000020 04000020 04FFFF20 04000020"
    $"04000020 04FFFC20 04000020 04000020"
    $"04FFFF20 04000020 04000020 04FFF020"
    $"04000020 04000020 04FE0020 04000020"
    $"04000020 07FFFFE0 00000000 00000000"
    $"00000000 00000000 07FFF000 07FFF800"
    $"07FFFC00 07FFFE00 07FFFF00 07FFFF80"
    $"07FFFFC0 07FFFFE0 07FFFFE0 07FFFFE0"
    $"07FFFFE0 07FFFFE0 07FFFFE0 07FFFFE0"
    $"07FFFFE0 07FFFFE0 07FFFFE0 07FFFFE0"
    $"07FFFFE0 07FFFFE0 07FFFFE0 07FFFFE0"
    $"07FFFFE0 07FFFFE0 07FFFFE0 07FFFFE0"
    $"07FFFFE0 07FFFFE0 00000000 00000000"
};

resource 'BNDL' (128) {
    'HANS', 0,
    {
        'FREF', { 0, 128; 1, 129 };
        'ICN#', { 0, 128; 1, 129 };
    }
};

resource 'vers' (1) {
    0x01, 0x00, beta, 0x01, verUS,
    "1.0b1",
    "Hans 1.0b1 \0xA9 2026"
};

/* ---------------- SIZE ---------------- */

resource 'SIZE' (-1) {
    reserved,
    acceptSuspendResumeEvents,
    reserved,
    canBackground,
    doesActivateOnFGSwitch,
    backgroundAndForeground,
    dontGetFrontClicks,
    ignoreChildDiedEvents,
    is32BitCompatible,
    notHighLevelEventAware,
    onlyLocalHLEvents,
    notStationeryAware,
    dontUseTextEditServices,
    reserved,
    reserved,
    reserved,
    6144 * 1024,
    3072 * 1024
};
