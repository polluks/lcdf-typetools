/* t1reencode.cc -- driver for reencoding Type 1 fonts
 *
 * Copyright (c) 2005 Eddie Kohler
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version. This program is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 * Public License for more details.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include <efont/t1font.hh>
#include <efont/t1item.hh>
#include <efont/t1rw.hh>
#include <efont/psres.hh>
#include <lcdf/error.hh>
#include <lcdf/clp.h>
#include <ctype.h>
#include <errno.h>
#include "util.hh"
#include "md5.h"
#ifdef HAVE_CTIME
# include <time.h>
#endif
#if defined(_MSDOS) || defined(_WIN32)
# include <fcntl.h>
# include <io.h>
#endif
#ifdef WIN32
/* According to Fabrice Popineau MSVCC doesn't handle std::min correctly. */
# define std /* */
#endif

using namespace Efont;

#define VERSION_OPT		301
#define HELP_OPT		302
#define OUTPUT_OPT		303
#define ENCODING_OPT		304
#define ENCODING_TEXT_OPT	305
#define PFA_OPT			306
#define PFB_OPT			307
#define FONTNAME_OPT		308
#define FULLNAME_OPT		309

Clp_Option options[] = {
    { "help", 'h', HELP_OPT, 0, 0 },
    { "output", 'o', OUTPUT_OPT, Clp_ArgString, 0 },
    { "pfa", 'a', PFA_OPT, 0, 0 },
    { "pfb", 'b', PFA_OPT, 0, 0 },
    { "name", 'n', FONTNAME_OPT, Clp_ArgString, 0 },
    { "fullname", 'N', FULLNAME_OPT, Clp_ArgString, 0 },
    { "full-name", 'N', FULLNAME_OPT, Clp_ArgString, 0 },
    { "encoding", 'e', ENCODING_OPT, Clp_ArgString, 0 },
    { "encoding-text", 'E', ENCODING_TEXT_OPT, Clp_ArgString, 0 },
    { "version", 0, VERSION_OPT, 0, 0 },
};


static const char *program_name;
static PermString::Initializer initializer;
static String::Initializer initializer2;
static HashMap<PermString, int> glyph_order(-1);
static String encoding_name;


void
usage_error(ErrorHandler *errh, const char *error_message, ...)
{
    va_list val;
    va_start(val, error_message);
    if (!error_message)
	errh->message("Usage: %s [OPTION]... FONT", program_name);
    else
	errh->verror(ErrorHandler::ERR_ERROR, String(), error_message, val);
    errh->message("Type %s --help for more information.", program_name);
    exit(1);
}

void
usage()
{
    printf("\
'T1testpage' creates a PostScript proof document for the specified Type 1\n\
font file and writes it to the standard output. The proof shows every\n\
glyph in the font, including its glyph name and encoding.\n\
\n\
Usage: %s [OPTION]... [FONT]\n\
\n\
FONT is either the name of a PFA or PFB font file. If omitted, t1testpage will\n\
read a font file from the standard input.\n\
\n\
Options:\n\
  -g, --glyph=GLYPH            Limit output to one or more GLYPHs.\n\
  -s, --smoke                  Print smoke proofs, one character per page.\n\
  -o, --output=FILE            Write output to FILE instead of standard out.\n\
  -h, --help                   Print this message and exit.\n\
      --version                Print version number and exit.\n\
\n\
Report bugs to <kohler@icir.org>.\n", program_name);
}


// FONT MANIPULATION

static void
kill_def(Type1Font* font, Type1Definition *t1d, int whichd)
{
    if (!t1d || font->dict(whichd, t1d->name()) != t1d)
	return;
  
    int icount = font->nitems();
    for (int i = font->first_dict_item(whichd); i < icount; i++)
	if (font->item(i) == t1d) {
	    StringAccum sa;
	    sa << '%';
	    t1d->gen(sa);
	    PermString name = t1d->name();
	    Type1CopyItem *t1ci = new Type1CopyItem(sa.take_string());
	    font->set_item(i, t1ci);
	    font->set_dict(whichd, name, 0);
	    return;
	}
  
    assert(0);
}

static void
adjust_font_definitions(Type1Font* font, Type1Encoding* encoding, String new_name, String new_full_name)
{
    // prepare an MD5 digest over the encoding
    StringAccum etext;
    for (int i = 0; i < 256; i++)
	etext << encoding->elt(i) << ' ';
    MD5_CONTEXT md5;
    md5_init(&md5);
    md5_update(&md5, (const unsigned char*) etext.data(), etext.length() - 1);
    
    // save UniqueID, then kill its definition
    int uniqueid;
    Type1Definition *t1d = font->dict("UniqueID");
    bool have_uniqueid = (t1d && t1d->value_int(uniqueid));
    kill_def(font, t1d, Type1Font::dFont);
    kill_def(font, font->p_dict("UniqueID"), Type1Font::dPrivate);
    
    // prepare XUID
    t1d = font->dict("XUID");
    Vector<double> xuid;
    if (!t1d || !t1d->value_numvec(xuid)) {
	if (have_uniqueid) {
	    t1d = font->ensure(Type1Font::dFont, "XUID");
	    xuid.clear();
	    xuid.push_back(1);
	    xuid.push_back(uniqueid);
	} else if (t1d) {
	    kill_def(font, t1d, Type1Font::dFont);
	    t1d = 0;
	}
    }
    if (t1d) {
	uint32_t digest[MD5_DIGEST_SIZE / sizeof(uint32_t) + 1];
	md5_final((unsigned char *) digest, &md5);
	
	// append digest to XUID
	for (unsigned i = 0; i < MD5_DIGEST_SIZE / sizeof(uint32_t); i++)
	    xuid.push_back(digest[i]);
	t1d->set_numvec(xuid);
    }

    // prepare new font name
    if (!encoding_name) {
	char text_digest[MD5_TEXT_DIGEST_SIZE + 1];
	md5_final_text(text_digest, &md5);
	encoding_name = "AutoEnc_" + String(text_digest);
    }
    
    t1d = font->dict("FontName");
    PermString name;
    if (t1d && t1d->value_name(name)) {
	if (!new_name)
	    new_name = name + String("-") + encoding_name;
	t1d->set_name(new_name.c_str());
	font->uncache_defs();	// remove cached font name
    }

    // add a FullName too
    String full_name;
    t1d = font->fi_dict("FullName");
    if (t1d && t1d->value_string(full_name)) {
	if (!new_full_name)
	    new_full_name = full_name + " " + encoding_name + " Enc";
	t1d->set_string(new_full_name.c_str());
    }

    // add header comments
    {
	StringAccum sa;
#if HAVE_CTIME
	time_t cur_time = time(0);
	char *time_str = ctime(&cur_time);
	sa << "%% Created by t1reencode-" VERSION " on " << time_str;
	sa.pop_back();
#else
	sa << "%% Created by t1reencode-" VERSION "."; 
#endif

	font->add_header_comment(sa.take_string().c_str());
	font->add_header_comment("%% T1reencode is free software.  See <http://www.lcdf.org/type/>.");
    }
}


// ENCODING READER

static String
tokenize(const String &s, int &pos_in, int &line)
{
    const char *data = s.data();
    int len = s.length();
    int pos = pos_in;
    while (1) {
	// skip whitespace
	while (pos < len && isspace(data[pos])) {
	    if (data[pos] == '\n')
		line++;
	    else if (data[pos] == '\r' && (pos + 1 == len || data[pos+1] != '\n'))
		line++;
	    pos++;
	}
	
	if (pos >= len) {
	    pos_in = len;
	    return String();
	} else if (data[pos] == '%') {
	    for (pos++; pos < len && data[pos] != '\n' && data[pos] != '\r'; pos++)
		/* nada */;
	} else if (data[pos] == '[' || data[pos] == ']' || data[pos] == '{' || data[pos] == '}') {
	    pos_in = pos + 1;
	    return s.substring(pos, 1);
	} else if (data[pos] == '(') {
	    int first = pos, nest = 0;
	    for (pos++; pos < len && (data[pos] != ')' || nest); pos++)
		switch (data[pos]) {
		  case '(': nest++; break;
		  case ')': nest--; break;
		  case '\\':
		    if (pos + 1 < len)
			pos++;
		    break;
		  case '\n': line++; break;
		  case '\r':
		    if (pos + 1 == len || data[pos+1] != '\n')
			line++;
		    break;
		}
	    pos_in = (pos < len ? pos + 1 : len);
	    return s.substring(first, pos_in - first);
	} else {
	    int first = pos;
	    while (pos < len && data[pos] == '/')
		pos++;
	    while (pos < len && data[pos] != '/' && !isspace(data[pos]) && data[pos] != '[' && data[pos] != ']' && data[pos] != '%' && data[pos] != '(' && data[pos] != '{' && data[pos] != '}')
		pos++;
	    pos_in = pos;
	    return s.substring(first, pos - first);
	}
    }
}

static String
landmark(const String &filename, int line)
{
    return filename + String::stable_string(":", 1) + String(line);
}

Type1Encoding *
parse_encoding(String s, String filename, ErrorHandler *errh)
{
    filename = printable_filename(filename);
    int pos = 0, line = 1;

    // parse text
    String token = tokenize(s, pos, line);
    if (!token || token[0] != '/') {
	errh->lerror(landmark(filename, line), "parse error, expected name");
	return 0;
    }
    encoding_name = token.substring(1);

    if (tokenize(s, pos, line) != "[") {
	errh->lerror(landmark(filename, line), "parse error, expected [");
	return 0;
    }

    Type1Encoding *t1e = new Type1Encoding;
    int e = 0;
    while ((token = tokenize(s, pos, line)) && token[0] == '/') {
	if (e > 255) {
	    errh->lwarning(landmark(filename, line), "more than 256 characters in encoding");
	    break;
	}
	t1e->put(e, token.substring(1));
	e++;
    }
    return t1e;
}



// MAIN

/*****
 * MAIN PROGRAM
 **/

static Type1Font *
do_file(const char *filename, PsresDatabase *psres, ErrorHandler *errh)
{
    FILE *f;
    if (!filename || strcmp(filename, "-") == 0) {
	f = stdin;
	filename = "<stdin>";
#if defined(_MSDOS) || defined(_WIN32)
	_setmode(_fileno(f), _O_BINARY);
#endif
    } else
	f = fopen(filename, "rb");
    
    if (!f) {
	// check for PostScript name
	Filename fn = psres->filename_value("FontOutline", filename);
	f = fn.open_read();
    }
  
    if (!f)
	errh->fatal("%s: %s", filename, strerror(errno));
  
    Type1Reader *reader;
    int c = getc(f);
    ungetc(c, f);
    if (c == EOF)
	errh->fatal("%s: empty file", filename);
    if (c == 128)
	reader = new Type1PFBReader(f);
    else
	reader = new Type1PFAReader(f);
  
    Type1Font *font = new Type1Font(*reader);
    if (!font->ok())
	errh->fatal("%s: no glyphs in font", filename);

    delete reader;
    return font;
}

int
main(int argc, char *argv[])
{
    PsresDatabase *psres = new PsresDatabase;
    psres->add_psres_path(getenv("PSRESOURCEPATH"), 0, false);
  
    Clp_Parser *clp =
	Clp_NewParser(argc, (const char * const *)argv, sizeof(options) / sizeof(options[0]), options);
    program_name = Clp_ProgramName(clp);
  
    ErrorHandler *errh = ErrorHandler::static_initialize(new FileErrorHandler(stderr));
    const char *input_file = 0;
    const char *output_file = 0;
    const char *encoding_file = 0;
    const char *encoding_text = 0;
    const char *new_font_name = 0;
    const char *new_full_name = 0;
    bool binary = true;
    Vector<String> glyph_patterns;
  
    while (1) {
	int opt = Clp_Next(clp);
	switch (opt) {

	  case ENCODING_OPT:
	    if (encoding_file || encoding_text)
		errh->fatal("encoding already specified");
	    encoding_file = clp->arg;
	    break;

	  case ENCODING_TEXT_OPT:
	    if (encoding_file || encoding_text)
		errh->fatal("encoding already specified");
	    encoding_text = clp->arg;
	    break;

	  case FONTNAME_OPT:
	    if (new_font_name)
		errh->fatal("font name already specified");
	    new_font_name = clp->arg;
	    break;

	  case FULLNAME_OPT:
	    if (new_full_name)
		errh->fatal("full name already specified");
	    new_full_name = clp->arg;
	    break;
	    
	  case OUTPUT_OPT:
	    if (output_file)
		errh->fatal("output file already specified");
	    output_file = clp->arg;
	    break;

	  case PFA_OPT:
	    binary = false;
	    break;

	  case PFB_OPT:
	    binary = true;
	    break;
      
	  case VERSION_OPT:
	    printf("t1reencode (LCDF typetools) %s\n", VERSION);
	    printf("Copyright (C) 1999-2005 Eddie Kohler\n\
This is free software; see the source for copying conditions.\n\
There is NO warranty, not even for merchantability or fitness for a\n\
particular purpose.\n");
	    exit(0);
	    break;
      
	  case HELP_OPT:
	    usage();
	    exit(0);
	    break;
      
	  case Clp_NotOption:
	    if (input_file)
		errh->fatal("too many arguments");
	    input_file = clp->arg;
	    break;
      
	  case Clp_Done:
	    goto done;
      
	  case Clp_BadOption:
	    usage_error(errh, 0);
	    break;
      
	  default:
	    break;
      
	}
    }
  
  done:
    // read the font
    Type1Font *font = do_file(input_file, psres, errh);
    if (!input_file || strcmp(input_file, "-") == 0)
	input_file = "<stdin>";

    // read the encoding
    if (!encoding_file && !encoding_text)
	errh->fatal("missing '-e ENCODING' argument");
    Type1Encoding *t1e = 0;
    if (encoding_file == "StandardEncoding")
	t1e = Type1Encoding::standard_encoding();
    else {
	String text;
	if (encoding_text)
	    text = String::stable_string(encoding_text), encoding_file = "<argument>";
	else if ((text = read_file(encoding_file, errh)), errh->nerrors() > 0)
	    exit(1);
	if (!(t1e = parse_encoding(text, encoding_file, errh)))
	    exit(1);
    }

    // set the encoding
    font->add_type1_encoding(t1e);

    // adjust definitions
    adjust_font_definitions(font, t1e, new_font_name, new_full_name);

    // write it to output
    FILE *outf;
    if (!output_file || strcmp(output_file, "-") == 0)
	outf = stdout;
    else {
	outf = fopen(output_file, "w");
	if (!outf)
	    errh->fatal("%s: %s", output_file, strerror(errno));
    }
    if (binary) {
#if defined(_MSDOS) || defined(_WIN32)
	_setmode(_fileno(outf), _O_BINARY);
#endif
	Type1PFBWriter w(outf);
	font->write(w);
    } else {
	Type1PFAWriter w(outf);
	font->write(w);
    }
    
    return (errh->nerrors() == 0 ? 0 : 1);
}
