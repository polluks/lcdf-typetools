#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#ifdef __GNUG__
# pragma implementation "afmw.hh"
#endif
#include "afmw.hh"


AfmWriter::AfmWriter(Metrics *m, FILE *f)
  : _m(m), _afm_xt((AfmMetricsXt *)m->find_xt("AFM")), _f(f)
{
}


void
AfmWriter::write()
{
  _m->pair_program()->unreverse();
  _m->pair_program()->optimize();
  
  fprintf(_f, "StartFontMetrics 4.1\n");
  if (_afm_xt)
    for (int i = 0; i < _afm_xt->opening_comments.count(); i++)
      fprintf(_f, "Comment %s\n", _afm_xt->opening_comments[i].cc());
  
  write_prologue();
  
  fprintf(_f, "StartCharMetrics %d\n", _m->glyph_count());
  
  GlyphIndex gi;
  for (int i = 0; i < 256; i++) //FIXME
    if ((gi = _m->find_code(i)) >= 0)
      write_char_metric_data(gi, i);
  for (gi = 0; gi < _m->glyph_count(); gi++)
    if (_m->code(gi) == -1)
      write_char_metric_data(gi, -1);
  
  fprintf(_f, "EndCharMetrics\n");
  
  write_kerns();
  
  fprintf(_f, "EndFontMetrics\n");
}


void
AfmWriter::write_prologue() const
{
  if (_m->font_name())
    fprintf(_f, "FontName %s\n", _m->font_name().cc());
  else
    fprintf(_f, "FontName No-Font-Name-Given\n");
  
  if (_m->full_name())
    fprintf(_f, "FullName %s\n", _m->full_name().cc());
  if (_m->family())
    fprintf(_f, "FamilyName %s\n", _m->family().cc());
  if (_m->weight())
    fprintf(_f, "Weight %s\n", _m->weight().cc());
  
  if (KNOWN(fd( fdItalicAngle )))
    fprintf(_f, "ItalicAngle %g\n", fd( fdItalicAngle ));
  
  fprintf(_f, "FontBBox %g %g %g %g\n",
	  fd( fdFontBBllx ), fd( fdFontBBlly ),
	  fd( fdFontBBurx ), fd( fdFontBBury ));
  
  if (KNOWN(fd( fdUnderlinePosition )))
    fprintf(_f, "UnderlinePosition %g\n", fd( fdUnderlinePosition ));
  if (KNOWN(fd( fdUnderlineThickness )))
    fprintf(_f, "UnderlineThickness %g\n", fd( fdUnderlineThickness ));
  
  if (_m->version())
    fprintf(_f, "Version %s\n", _m->version().cc());
  if (_afm_xt && _afm_xt->notice)
    fprintf(_f, "Notice %s\n", _afm_xt->notice.cc());
  
  if (_afm_xt && _afm_xt->encoding_scheme)
    fprintf(_f, "EncodingScheme %s\n", _afm_xt->encoding_scheme.cc());
  
  if (KNOWN(fd( fdCapHeight )))
    fprintf(_f, "CapHeight %g\n", fd( fdCapHeight ));
  if (KNOWN(fd( fdXHeight )))
    fprintf(_f, "XHeight %g\n", fd( fdXHeight ));
  if (KNOWN(fd( fdAscender )))
    fprintf(_f, "Ascender %g\n", fd( fdAscender ));
  if (KNOWN(fd( fdDescender )))
    fprintf(_f, "Descender %g\n", fd( fdDescender ));
  if (KNOWN(fd( fdStdHW )))
    fprintf(_f, "StdHW %g\n", fd( fdStdHW ));
  if (KNOWN(fd( fdStdVW )))
    fprintf(_f, "StdVW %g\n", fd( fdStdVW ));
}


void
AfmWriter::write_char_metric_data(GlyphIndex gi, int e) const
{
  if (e >= -1 && e < 256)
    fprintf(_f, "C %d ;", e);
  else
    fprintf(_f, "CH <%04X> ;", e);
  
  double w = _m->wd(gi);
  if (KNOWN(w))
    fprintf(_f, " WX %g ;", w);
  else
    w = 0;
  
  fprintf(_f, " N %s ;", _m->name(gi).cc());
  
  if (KNOWN(_m->lf(gi)))
    fprintf(_f, " B %g %g %g %g ;",
	    _m->lf(gi), _m->bt(gi), _m->rt(gi), _m->tp(gi));
  
  // Run through the ligature/kern program to find ligatures.
  PairProgram &pairp = *_m->pair_program();
  PairOpIndex opi = pairp.find_left(gi);
  while (opi >= 0) {
    PairOp &op = pairp.op(opi);
    if (op.is_lig()) {
      if (op.lig_kind() == opLigSimple)
	fprintf(_f, " L %s %s ;",
		_m->name( op.right() ).cc(),
		_m->name( op.result() ).cc());
      //else
      //warning("strange ligature combination not supported by AFM");
    }
    opi = op.next_left();
  }
  
  fputc('\n', _f);
}


void
AfmWriter::write_kerns() const
{
  PairProgram &pairp = *_m->pair_program();
  
  // Damn. First we have to count how many kerning pairs there are.
  int numkerns = 0;
  for (PairOpIndex opi = 0; opi < pairp.op_count(); opi++)
    if (pairp.op(opi).is_kern())
      numkerns++;
  
  if (numkerns == 0) return;
  
  fprintf(_f, "StartKernData\n");
  fprintf(_f, "StartKernPairs %d\n", numkerns);
  for (GlyphIndex gi = 0; gi < _m->glyph_count(); gi++) {
    PairOpIndex opi = pairp.find_left(gi);
    while (opi >= 0) {
      PairOp &op = pairp.op(opi);
      if (op.is_kern())
	fprintf(_f, "KPX %s %s %g\n",
		_m->name( gi ).cc(),
		_m->name( op.right() ).cc(),
		_m->kv( op.value() ));
      opi = op.next_left();
    }
  }
  fprintf(_f, "EndKernPairs\n");
  fprintf(_f, "EndKernData\n");
}
